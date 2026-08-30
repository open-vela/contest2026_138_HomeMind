#!/usr/bin/env python3
"""HomeMind 家庭网关 agent（部署在家庭 Ubuntu 192.168.31.251）。

- 只主动出站：mqtts://api.hfy-ai.cloud:8883（TLS + 用户名/密码）
- 订阅 device/<DEVICE_ID>/cmd，经 /dev/ttyACM0 串口驱动 ESP32-S3-EYE
- 发布 device/<DEVICE_ID>/ack（acked/done/expired）与 device/<DEVICE_ID>/status
- 指数退避重连；绝不开放任何公网入站端口
"""
import os
import sys
import time
import json
import threading
import logging
import paho.mqtt.client as mqtt

try:
    import serial
except ImportError:
    serial = None

logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
logger = logging.getLogger("gateway-agent")


def _load_env(path=None):
    """加载同目录下的 .env（KEY=VALUE，不依赖 python-dotenv）。"""
    if path is None:
        path = os.path.join(os.path.dirname(os.path.abspath(__file__)), ".env")
    if not os.path.exists(path):
        return
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#") or "=" not in line:
                continue
            k, v = line.split("=", 1)
            os.environ.setdefault(k.strip(), v.strip())


_load_env()

# ── 配置（来自 .env / 环境变量，不硬编码）──
API_HOST = os.getenv("API_HOST", "api.hfy-ai.cloud")
API_PORT = int(os.getenv("API_PORT", "8883"))
MQTT_USER = os.getenv("MQTT_USER", "gateway")
MQTT_PASS = os.getenv("MQTT_PASS", "")
TLS_CA_PATH = os.getenv("TLS_CA_PATH", "")  # 自签证书路径；空=用系统 CA（Let's Encrypt）
DEVICE_ID = os.getenv("DEVICE_ID", "esp32s3-eye")
SERIAL_PORT = os.getenv("SERIAL_PORT", "/dev/ttyACM0")
BAUD = int(os.getenv("BAUD", "115200"))
CMD_TTL = int(os.getenv("CMD_TTL", "30"))
KEEPALIVE = int(os.getenv("KEEPALIVE", "60"))
CMD_MODE = os.getenv("CMD_MODE", "ask")  # ask=经 ai_agent 自然语言；direct=直接 JSON（设备端待支持）

TOPIC_CMD = f"device/{DEVICE_ID}/cmd"
TOPIC_ACK = f"device/{DEVICE_ID}/ack"
TOPIC_STATUS = f"device/{DEVICE_ID}/status"

PROMPT = os.getenv("DEVICE_PROMPT", "vela>")
ENTER_CMD = "ai_agent"  # 从 nsh> 进入 vela> 的命令

# 动作 -> 自然语言（与设备侧已验证本地工具对应）
ASK_MAP = {
    "led.on": "打开灯",
    "led.off": "关灯",
    "device.info": "上报设备信息",
}
LED_DESIRED = {"led.on": "on", "led.off": "off"}


class SerialExecutor:
    """串口执行器：把云端命令翻译成设备侧动作并读回结果。"""

    def __init__(self, port, baud, prompt, enter_cmd):
        if serial is None:
            raise RuntimeError("pyserial 未安装：pip install pyserial")
        self.port = port
        self.baud = baud
        self.prompt = prompt
        self.enter_cmd = enter_cmd
        self.ser = None
        self.led_state = "unknown"
        self.lock = threading.Lock()

    def open(self):
        self.ser = serial.Serial(self.port, self.baud, timeout=2)
        time.sleep(1.5)
        self.ser.reset_input_buffer()
        # 进入 ai_agent 会话
        self.ser.write((self.enter_cmd + "\n").encode())
        self._read_until_prompt(timeout=8)
        logger.info("串口会话已就绪（%s @ %d）", self.port, self.baud)

    def _read_until_prompt(self, timeout=10):
        buf = b""
        end = time.time() + timeout
        while time.time() < end:
            if self.ser.in_waiting:
                chunk = self.ser.read(self.ser.in_waiting)
                buf += chunk
                if self.prompt.encode() in buf.split(b"\n")[-1]:
                    break
            else:
                time.sleep(0.05)
        return buf.decode(errors="replace")

    def exec(self, action, timeout=20):
        with self.lock:
            if CMD_MODE == "direct":
                # 未来设备端若支持直接 JSON 命令通道，走这里
                line = json.dumps({"action": action}) + "\n"
            else:
                nl = ASK_MAP.get(action, action)
                line = f"ask {nl}\n"
            self.ser.reset_input_buffer()
            self.ser.write(line.encode())
            out = self._read_until_prompt(timeout=timeout)
            if action in LED_DESIRED:
                self.led_state = LED_DESIRED[action]
            return out

    def close(self):
        if self.ser:
            try:
                self.ser.close()
            except Exception:
                pass


def main():
    executor = None
    try:
        executor = SerialExecutor(SERIAL_PORT, BAUD, PROMPT, ENTER_CMD)
        executor.open()
    except Exception as e:
        logger.error("串口初始化失败，将以『仅发布状态/转发』模式运行：%s", e)
        executor = None

    client = mqtt.Client()
    client.username_pw_set(MQTT_USER, MQTT_PASS)
    if TLS_CA_PATH:
        client.tls_set(ca_certs=TLS_CA_PATH, cert_reqs=mqtt.ssl.CERT_REQUIRED)
    else:
        client.tls_set(cert_reqs=mqtt.ssl.CERT_REQUIRED)  # 用系统 CA（Let's Encrypt）

    backoff = 2

    def on_connect(cli, userdata, flags, rc):
        nonlocal backoff
        if rc == 0:
            backoff = 2
            cli.subscribe(TOPIC_CMD)
            logger.info("已连接 MQTT 并订阅 %s", TOPIC_CMD)
            cli.publish(TOPIC_STATUS, json.dumps({"online": True,
                        "led": executor.led_state if executor else "unknown"}), qos=1)
        else:
            logger.error("MQTT 连接失败 rc=%s", rc)

    def on_message(cli, userdata, msg):
        try:
            payload = json.loads(msg.payload.decode())
        except Exception:
            return
        cid = payload.get("command_id")
        action = payload.get("action")
        if not cid or not action:
            return
        logger.info("收到命令 %s -> %s", cid, action)
        # 立即回 acked（已接收，且绝不在 MQTT 回调里做阻塞 IO）
        cli.publish(TOPIC_ACK, json.dumps({"command_id": cid, "status": "acked"}), qos=1)

        # 把串口执行放到独立线程：避免阻塞 MQTT 网络循环（否则会饿死 keepalive、
        # 导致连接掉线重连，且 acked/done 要等串口超时才能发出）。
        def _handle():
            if executor is None:
                cli.publish(TOPIC_ACK, json.dumps({"command_id": cid, "status": "expired"}), qos=1)
                return
            try:
                executor.exec(action, timeout=max(5, CMD_TTL - 5))
                cli.publish(TOPIC_ACK, json.dumps({"command_id": cid, "status": "done"}), qos=1)
                cli.publish(TOPIC_STATUS, json.dumps({"online": True, "led": executor.led_state}), qos=1)
            except Exception as e:
                logger.error("执行命令 %s 失败：%s", cid, e)
                cli.publish(TOPIC_ACK, json.dumps({"command_id": cid, "status": "expired"}), qos=1)

        threading.Thread(target=_handle, daemon=True).start()

    client.on_connect = on_connect
    client.on_message = on_message

    while True:
        try:
            client.connect(API_HOST, API_PORT, KEEPALIVE)
            client.loop_forever(retry_first_connection=True)
        except Exception as e:
            logger.error("MQTT 循环异常：%s，%ss 后重连", e, backoff)
            time.sleep(backoff)
            backoff = min(backoff * 2, 60)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        sys.exit(0)
