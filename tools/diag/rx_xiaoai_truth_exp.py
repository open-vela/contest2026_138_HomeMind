#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""定案实验：小爱当音源，麦克风接收，采多组互不相同的真值。

为什么要绕过网关直接打 HA：
  网关要独占串口。而 ESP32-S3 的 USB-JTAG 控制台在"没人读"的时候会把
  大约 845 字节的 FIFO 写满，NuttX 那一次控制台写就永久阻塞 —— 板子看起来
  像死了。所以整场实验由本进程一直攥着端口，小爱直接通过 HA 的
  notify.send_message 驱动，网关全程停着。

真值设计：4 句内容完全不同，且每句在播报里重复 4 遍（不依赖毫秒级对齐），
每句期间连采 3 个 5 秒窗口。只有真正正确的解码方式才可能 4 句全对。
"""
import json
import os
import re
import subprocess
import sys
import time
import urllib.request

import serial

ENV = "/home/hfy/homemind-backend/.env"
PORT = "/dev/homemind-esp32"
ESPPORT = "/dev/ttyACM1"
DUMP = "/tmp/voice_dump"
ANSI = re.compile(r"\x1b\[[0-9;?]*[A-Za-z]")
PROMPT = re.compile(r"(nsh|vela)>\s*$")
NOISE = ("[HM-TLS-TX]", "[HM-TLS-RX]")

PHRASES = [
    "打开客厅的灯。",
    "关闭卧室的灯。",
    "打开书房的空调。",
    "播放一首音乐。",
]

MAP_OUT = "/tmp/decode_exp_map.json"


def load_env():
    d = {}
    for line in open(ENV, encoding="utf-8"):
        line = line.strip()
        if line and not line.startswith("#") and "=" in line:
            k, v = line.split("=", 1)
            d[k.strip()] = v.strip().strip('"').strip("'")
    return d


E = load_env()
HA = E["HOMEMIND_HA_URL"].rstrip("/")
TOK = E["HOMEMIND_HA_TOKEN"]
SPK = "media_player.xiaomi_cn_2085562629_lx06"
NOTIFY = "notify.xiaomi_cn_2085562629_lx06_play_text_a_5_1"


def ha_post(path, payload):
    req = urllib.request.Request(
        HA + path, data=json.dumps(payload).encode(),
        headers={"Authorization": "Bearer " + TOK,
                 "Content-Type": "application/json"}, method="POST")
    with urllib.request.urlopen(req, timeout=25) as r:
        return r.status


def say(text, tag=""):
    try:
        st = ha_post("/api/services/notify/send_message",
                     {"entity_id": NOTIFY, "message": text})
        print("  [say] %s -> %s (%d 字)" % (tag, st, len(text)), flush=True)
    except Exception as e:
        print("  [say] %s 失败: %s" % (tag, e), flush=True)


def set_volume(level=1.0):
    try:
        st = ha_post("/api/services/media_player/volume_set",
                     {"entity_id": SPK, "volume_level": level})
        req = urllib.request.Request(HA + "/api/states/" + SPK,
                                     headers={"Authorization": "Bearer " + TOK})
        with urllib.request.urlopen(req, timeout=15) as r:
            stt = json.loads(r.read().decode())
        print("[prep] 音量 -> %s，当前 = %s" %
              (st, stt["attributes"].get("volume_level")), flush=True)
    except Exception as e:
        print("[prep] 音量设置失败: %s" % e, flush=True)


def do_reset():
    cmd = ["python3", "-m", "esptool", "--chip", "esp32s3", "-p", ESPPORT,
           "--after", "hard_reset", "read-flash", "0x0", "0x100", "/tmp/_junk.bin"]
    try:
        subprocess.run(cmd, capture_output=True, timeout=150, text=True)
    except Exception as e:
        print("[acq] reset 异常 %s" % e, flush=True)


def acquire(max_try=5, wait=32):
    for attempt in range(1, max_try + 1):
        print("[acq] 第 %d/%d 次：复位 + 附着" % (attempt, max_try), flush=True)
        do_reset()
        s = serial.Serial()
        s.port = PORT
        s.baudrate = 115200
        s.timeout = 0.2
        s.dtr = False
        s.rts = False
        s.open()
        t0 = time.time()
        tail = ""
        nbytes = 0
        tries = 0
        next_poke = t0 + 1.0
        while time.time() - t0 < wait:
            if time.time() >= next_poke:
                s.write(b"\r\n")
                s.flush()
                next_poke = time.time() + 3.0
            d = s.read(8192)
            if d:
                nbytes += len(d)
                tail = (tail + ANSI.sub("", d.decode("utf-8", "replace")).replace("\r", ""))[-600:]
                m = PROMPT.search(tail)
                if m:
                    if m.group(1) == "vela":
                        print("[acq] ✓ vela>（%d 字节）" % nbytes, flush=True)
                        return s
                    if tries < 3:
                        tries += 1
                        s.write(b"ai_agent\r\n")
                        s.flush()
                        tail = ""
        s.close()
        print("[acq] ✗ 未进 shell（%d 字节）" % nbytes, flush=True)
        time.sleep(1.0)
    return None


def drain(s, sec, quiet_after=0.0):
    """读取 sec 秒，返回（去噪后的行列表，原始字节数）。"""
    lines = []
    nbytes = 0
    buf = ""
    t0 = time.time()
    while time.time() - t0 < sec:
        d = s.read(8192)
        if d:
            nbytes += len(d)
            buf += ANSI.sub("", d.decode("utf-8", "replace")).replace("\r", "")
            while "\n" in buf:
                line, buf = buf.split("\n", 1)
                line = line.strip()
                if line and not any(line.startswith(p) for p in NOISE):
                    lines.append(line)
    return lines, nbytes


def run(s, cmd, sec):
    s.write(cmd.encode() + b"\r\n")
    s.flush()
    lines, nb = drain(s, sec)
    keep = [l for l in lines if any(k in l for k in
            ("Voice", "STRM", "[Agent]", "http=", "recogni", "reason", "retry",
             "plan", "ERR", "asr", "wake"))]
    print("  >>> %s  (收 %d 字节)" % (cmd, nb), flush=True)
    for l in keep:
        print("      %s" % l, flush=True)
    return "\n".join(lines)


def ls_dump():
    try:
        return sorted(f for f in os.listdir(DUMP) if f.endswith(".pcm"))
    except FileNotFoundError:
        return []


print("=" * 62)
print("定案实验：小爱当音源 / 麦克风接收 / 4 组互不相同的真值")
print("=" * 62)

set_volume(1.0)
os.makedirs(DUMP, exist_ok=True)

# 清空转储（用 sudo，目录可能属 root）
subprocess.run("echo 123456 | sudo -S rm -f %s/*.pcm" % DUMP, shell=True,
               capture_output=True, text=True)
print("[prep] 转储目录已清空，当前 %d 个文件" % len(ls_dump()), flush=True)

ser = acquire()
if ser is None:
    print("!!! 无法获取 shell，退出", flush=True)
    sys.exit(2)

print()
print("[warm] 预热一次 voice（吃掉首请求偶发 http=-1）", flush=True)
run(ser, "voice", 16)

mapping = {}
for i, ph in enumerate(PHRASES, 1):
    print()
    print("=" * 62)
    print("第 %d/%d 句  真值：%s" % (i, len(PHRASES), ph), flush=True)
    print("=" * 62)
    before = set(ls_dump())
    say(ph * 4, tag="第%d句×4" % i)
    print("  等 3.5 秒起播", flush=True)
    time.sleep(3.5)
    for k in range(3):
        print("  --- 录音窗口 %d ---" % (k + 1), flush=True)
        run(ser, "voice", 14)
    after = set(ls_dump())
    new = sorted(after - before)
    mapping[ph] = new
    print("  本句新增 PCM：%s" % (new or "无"), flush=True)
    time.sleep(1.0)

ser.close()
json.dump(mapping, open(MAP_OUT, "w"), ensure_ascii=False, indent=2)
print()
print("[done] 映射已写入 %s" % MAP_OUT)
for k, v in mapping.items():
    print("  %-16s -> %s" % (k, v))
print("[done] 转储目录共 %d 个文件" % len(ls_dump()))
