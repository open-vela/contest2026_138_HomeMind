#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""把 /tmp/voice_dump 全部 14 段取回本地，并出「内容是否像语音」的对照表。

要点：上一轮只取了每组真值里 rms 最大的那段（3 段），且用 2-gram 子串做命中判定，
      结论不可靠。这里全量取回，逐段给客观指标。
用法: python _dump_sweep14.py
"""
import os
import numpy as np
import paramiko

HOST = "192.168.31.251"
RDIR = "/tmp/voice_dump"
OUT = "transfer/_dumps14"


def fetch():
    c = paramiko.SSHClient()
    c.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    c.connect(HOST, username="hfy", password="123456", timeout=20)
    sftp = c.open_sftp()
    os.makedirs(OUT, exist_ok=True)
    names = sorted(n for n in sftp.listdir(RDIR) if n.endswith(".pcm"))
    out = []
    for n in names:
        lp = os.path.join(OUT, n)
        if not os.path.exists(lp) or os.path.getsize(lp) != sftp.stat(RDIR + "/" + n).st_size:
            sftp.get(RDIR + "/" + n, lp)
        out.append(lp)
    sftp.close()
    c.close()
    return out


def metrics(path):
    raw = open(path, "rb").read()
    nb = len(raw) - len(raw) % 4
    W = np.frombuffer(raw[:nb], dtype="<i4").astype(np.int64)
    X = np.frombuffer(raw[:nb], dtype="<i2").astype(np.int64)
    lo = W & 0xFFFF
    lo = np.where(lo >= 32768, lo - 65536, lo)
    hi = ((W >> 16) & 0xFFFF).astype(np.int64)
    hi = np.where(hi >= 32768, hi - 65536, hi)

    rms = float(np.sqrt((X.astype("f8") ** 2).mean()))
    peak = int(np.abs(X).max())
    z = float((X == 0).mean())

    both0 = float(((lo == 0) & (hi == 0)).mean())
    one0 = float(((((lo == 0) & (hi != 0)) | ((lo != 0) & (hi == 0))).mean()))
    no0 = float(((lo != 0) & (hi != 0)).mean())

    w_eq1 = float((W[:-1] == W[1:]).mean())
    x_eq2 = float((X[:-2] == X[2:]).mean())
    loeqhi = float((lo == hi).mean())

    # 还原流：每字取非零半字
    S = np.where(lo != 0, lo, np.where(hi != 0, hi, 0)).astype("f8")
    S = S - S.mean()
    if len(S) > 64:
        sp = np.abs(np.fft.rfft(S * np.hanning(len(S)))) ** 2
        fr = np.fft.rfftfreq(len(S), 1.0 / 16000.0)
        tot = sp[1:].sum() + 1e-12
        band = lambda a, b: float(sp[(fr >= a) & (fr < b)].sum() / tot)
        b300 = band(300, 3400)     # 语音主能量带
        b0 = band(0, 60)           # DC/工频
        b4k = band(4000, 8000)
        # 谱重心
        cen = float((fr * sp).sum() / (sp.sum() + 1e-12))
    else:
        b300 = b0 = b4k = cen = float("nan")

    return dict(n=len(X), rms=rms, peak=peak, z=100 * z, both0=100 * both0,
                one0=100 * one0, no0=100 * no0, w_eq1=100 * w_eq1,
                x_eq2=100 * x_eq2, loeqhi=100 * loeqhi,
                b300=100 * b300, b0=100 * b0, b4k=100 * b4k, cen=cen)


def main():
    paths = fetch()
    print("\n取回 %d 段 -> %s\n" % (len(paths), OUT))
    hdr = ("%-34s %7s %7s %6s %7s | %6s %6s %6s | %6s %6s | %6s %6s %6s %7s"
           % ("file", "rms", "peak", "零%", "字级eq1", "两半0", "半0", "无0",
              "int16eq2", "lo=hi", "语带%", "DC%", "4-8k%", "谱重心"))
    print(hdr)
    print("-" * len(hdr))
    rows = []
    for p in paths:
        m = metrics(p)
        rows.append((m["rms"], p, m))
    for rms, p, m in sorted(rows, reverse=True):
        print("%-34s %7.1f %7d %6.1f %6.1f  | %6.1f %6.1f %6.1f | %6.1f %6.1f | %6.1f %6.1f %6.1f %7.0f"
              % (os.path.basename(p)[:34], m["rms"], m["peak"], m["z"], m["w_eq1"],
                 m["both0"], m["one0"], m["no0"], m["x_eq2"], m["loeqhi"],
                 m["b300"], m["b0"], m["b4k"], m["cen"]))
    print("\n判读：'两半0' 高 = 每个 32bit 字里两半都是 0（结构性填充）；"
          "'半0' 高 = 每字恰好一个样点；'无0' 高 = 字级满数据。")


main()
