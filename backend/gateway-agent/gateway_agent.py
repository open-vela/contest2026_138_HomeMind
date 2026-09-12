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

from command_policy import CommandPolicy
from mihome_adapter import HomeAssistantMiHomeAdapter

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
TOPIC_SPEAK = f"device/{DEVICE_ID}/speak"

# ── 本地 MQTT（家庭侧迁移，工作包 B；配置 LOCAL_MQTT_HOST 则启用双连接）──
LOCAL_MQTT_HOST = os.getenv("LOCAL_MQTT_HOST", "")
LOCAL_MQTT_PORT = int(os.getenv("LOCAL_MQTT_PORT", "1883"))
LOCAL_MQTT_USER = os.getenv("LOCAL_MQTT_USER", "home-gateway")
LOCAL_MQTT_PASS = os.getenv("LOCAL_MQTT_PASS", "")

PROMPT = os.getenv("DEVICE_PROMPT", "vela>")
ENTER_CMD = "ai_agent"  # 从 nsh> 进入 vela> 的命令

# 动作 -> 自然语言（与设备侧已验证本地工具对应）
ASK_MAP = {
    "led.on": "打开灯",
    "led.off": "关灯",
    "device.info": "上报设备信息",
}
MIHOME_ACTIONS = {"mihome.set_power", "mihome.get_state"}
LED_DESIRED = {"led.on": "on", "led.off": "off"}
COMMAND_POLICY = CommandPolicy(
    tuple(ASK_MAP.keys()) + tuple(MIHOME_ACTIONS), max_ttl=max(CMD_TTL, 300)
)


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
            # 当前固件 ask 为异步：先打印 "Sent to agent" 并立刻回 vela>，
            # 工具/Agent 最终回复稍后以 "[Agent]: ..." 打出。必须等到 Agent 段。
            # 注意：pyserial.read(n) 会阻塞凑满 n 字节；这里用 in_waiting 非阻塞攒包。
            old_timeout = self.ser.timeout
            self.ser.timeout = 0.05
            try:
                deadline = time.time() + timeout
                buf = b""
                saw_sent = False
                while time.time() < deadline:
                    n = self.ser.in_waiting
                    chunk = self.ser.read(n if n else 1)
                    if chunk:
                        buf += chunk
                        text = buf.decode(errors="replace")
                        if (not saw_sent) and "Sent to agent" in text:
                            saw_sent = True
                        if saw_sent and "[Agent]:" in text:
                            if "vela>" in text.split("[Agent]:", 1)[1]:
                                # 再吃一点残留
                                n2 = self.ser.in_waiting
                                if n2:
                                    buf += self.ser.read(n2)
                                break
                    else:
                        time.sleep(0.02)
            finally:
                self.ser.timeout = old_timeout
            out = buf.decode(errors="replace")
            return out

    def close(self):
        if self.ser:
            try:
                self.ser.close()
            except Exception:
                pass


def main():
    mihome = HomeAssistantMiHomeAdapter.from_env()
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

    def on_connect(cli, userdata, flags, rc):
        if rc == 0:
            cli.subscribe(TOPIC_CMD)
            cli.subscribe(TOPIC_SPEAK)
            logger.info("已连接 MQTT 并订阅 %s、%s", TOPIC_CMD, TOPIC_SPEAK)
            cli.publish(TOPIC_STATUS, json.dumps({"online": True,
                        "led": executor.led_state if executor else "unknown",
                        "mihome": mihome.describe(),
                        "mihome_entities": mihome.list_entities()}), qos=1)
        else:
            logger.error("MQTT 连接失败 rc=%s", rc)

    def on_message(cli, userdata, msg):
        if msg.topic == TOPIC_SPEAK:
            _handle_speak(cli, msg)
            return
        try:
            payload = json.loads(msg.payload.decode())
        except Exception:
            return
        decision = COMMAND_POLICY.claim(payload)
        cid = payload.get("command_id") if isinstance(payload, dict) else None
        if not decision.accepted:
            if decision.reason == "duplicate" and cid:
                status = decision.status if decision.status in {"acked", "done", "expired"} else "acked"
                cli.publish(TOPIC_ACK, json.dumps({"command_id": cid, "status": status}), qos=1)
            elif isinstance(cid, str) and cid:
                cli.publish(TOPIC_ACK, json.dumps({"command_id": cid, "status": "expired"}), qos=1)
            logger.warning("拒绝命令 %s: %s", cid or "<missing>", decision.reason)
            return
        action = payload["action"]
        logger.info("收到命令 %s -> %s", cid, action)
        # 立即回 acked（已接收，且绝不在 MQTT 回调里做阻塞 IO）
        cli.publish(TOPIC_ACK, json.dumps({"command_id": cid, "status": "acked"}), qos=1)
        COMMAND_POLICY.update(cid, "acked")

        # 把串口执行放到独立线程：避免阻塞 MQTT 网络循环（否则会饿死 keepalive、
        # 导致连接掉线重连，且 acked/done 要等串口超时才能发出）。
        def _handle():
            try:
                ttl = int(payload["ttl"])
                if action in MIHOME_ACTIONS:
                    result = mihome.execute(action, payload.get("params"))
                    output = json.dumps(result)
                else:
                    if executor is None:
                        raise RuntimeError("serial executor unavailable")
                    result = None
                    output = executor.exec(action, timeout=max(2, min(CMD_TTL, ttl)))
                lowered = output.lower()
                if any(marker in lowered for marker in ("unknown command", "error:", "failed", "timeout")):
                    raise RuntimeError("device reported command failure")
                if time.time() - float(payload["ts"]) > ttl:
                    raise RuntimeError("command expired during execution")
                if action in LED_DESIRED and f'"led":"{LED_DESIRED[action]}"' not in output.replace(" ", ""):
                    raise RuntimeError("device did not confirm requested LED state")
                if action in LED_DESIRED:
                    executor.led_state = LED_DESIRED[action]
                COMMAND_POLICY.update(cid, "done")
                cli.publish(TOPIC_ACK, json.dumps({"command_id": cid, "status": "done"}), qos=1)
                status = {"online": True,
                          "led": executor.led_state if executor else "unknown",
                          "mihome": mihome.describe(),
                          "mihome_entities": mihome.list_entities()}
                if result is not None:
                    status["mihome_result"] = result
                cli.publish(TOPIC_STATUS, json.dumps(status), qos=1)
            except Exception as e:
                logger.error("执行命令 %s 失败：%s", cid, e)
                COMMAND_POLICY.update(cid, "expired")
                cli.publish(TOPIC_ACK, json.dumps({"command_id": cid, "status": "expired"}), qos=1)

        threading.Thread(target=_handle, daemon=True).start()

    def _handle_speak(cli, msg):
        """device/<id>/speak：{"text": "..."} -> HA notify 实体（小爱音箱播报）。"""
        try:
            payload = json.loads(msg.payload.decode())
        except Exception:
            return
        text = payload.get("text") if isinstance(payload, dict) else None
        if not isinstance(text, str) or not text.strip():
            logger.warning("speak 消息缺少有效 text")
            return
        entity = os.getenv("HOMEMIND_SPEAK_ENTITY", "")

        def _do():
            last_err = None
            for attempt in range(2):  # HA 首次调用新 notify 实体可能超时，重试一次
                try:
                    result = mihome.notify_text(entity, text)
                    logger.info("已播报（%s）：%s", entity, text[:60])
                    cli.publish(TOPIC_STATUS, json.dumps({
                        "online": True,
                        "led": executor.led_state if executor else "unknown",
                        "mihome": mihome.describe(),
                        "last_speak": text[:120]}), qos=1)
                    return
                except Exception as e:
                    last_err = e
                    logger.warning("播报第 %d 次失败：%s", attempt + 1, e)
                    time.sleep(1.5)
            logger.error("播报最终失败（%s）：%s", entity, last_err)

        threading.Thread(target=_do, daemon=True).start()

    client.on_connect = on_connect
    client.on_message = on_message

    def _run_loop(cli, host, port):
        """独立线程跑一个 MQTT 连接（云端/本地各自维护重连退避）。"""
        _backoff = 2
        while True:
            try:
                cli.connect(host, port, KEEPALIVE)
                cli.loop_forever(retry_first_connection=True)
            except Exception as e:
                logger.error("MQTT 循环异常（%s:%s）：%s，%ss 后重连", host, port, e, _backoff)
                time.sleep(_backoff)
                _backoff = min(_backoff * 2, 60)

    # 本地 broker（家庭侧，明文 1883 + 密码认证）：配置即启用
    if LOCAL_MQTT_HOST:
        local = mqtt.Client()
        local.username_pw_set(LOCAL_MQTT_USER, LOCAL_MQTT_PASS)
        local.on_connect = on_connect
        local.on_message = on_message
        threading.Thread(
            target=_run_loop, args=(local, LOCAL_MQTT_HOST, LOCAL_MQTT_PORT),
            daemon=True).start()
        logger.info("本地 MQTT 已启动：%s:%s（用户 %s）",
                    LOCAL_MQTT_HOST, LOCAL_MQTT_PORT, LOCAL_MQTT_USER)

    _run_loop(client, API_HOST, API_PORT)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        sys.exit(0)
