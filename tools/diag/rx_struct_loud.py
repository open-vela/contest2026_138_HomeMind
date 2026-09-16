#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""对「有语音」的现场流做决定性判决：是不是「每个真样点重复 2 次」？

判据（互相独立，避免上一轮那种只看一个数的误判）：
  T1  S[0::2] == S[1::2] 精确相等率   —— 纯重复则 ~100%
  T2  S 的游程直方图（相邻相同值连续长度）
  T3  S 与 S2=S[0::2] 的功率谱分带能量 —— 重复会产生 ZOH 镜像
  T4  ZCR（过零率）：重复会把 ZCR 减半
  T5  低频端的自相关衰减形状
对照：Sep-12 干净录音（pos01.pcm）跑同一套。

用法: python _struct_loud.py <现场流> <对照流>
"""
import sys
import os
import numpy as np


def load_words(path):
    raw = open(path, "rb").read()
    nb = len(raw) - len(raw) % 4
    W = np.frombuffer(raw[:nb], dtype="<i4").astype(np.int64)
    lo = W & 0xFFFF
    lo = np.where(lo >= 32768, lo - 65536, lo)
    hi = ((W >> 16) & 0xFFFF).astype(np.int64)
    hi = np.where(hi >= 32768, hi - 65536, hi)
    return W, lo, hi


def recover(lo, hi):
    """每个 32bit 字取非零的那一半 -> 一个 16 位样点"""
    return np.where(lo != 0, lo, np.where(hi != 0, hi, 0)).astype(np.int64)


def runs(a, cap=10):
    if len(a) < 2:
        return {}
    eq = (a[:-1] == a[1:]).astype(np.int8)
    out, cur = {}, 1
    for e in eq:
        if e:
            cur += 1
        else:
            out[min(cur, cap)] = out.get(min(cur, cap), 0) + 1
            cur = 1
    out[min(cur, cap)] = out.get(min(cur, cap), 0) + 1
    tot = sum(out.values())
    return {k: (v, round(100.0 * v / tot, 1)) for k, v in sorted(out.items())}


def bands(a, rate, edges):
    a = a.astype("f8")
    a = a - a.mean()
    n = 1 << int(np.floor(np.log2(len(a)))) if len(a) > 256 else len(a)
    hop, win = n // 2, np.hanning(n)
    acc = None
    cnt = 0
    for i in range(0, len(a) - n, hop):
        sp = np.abs(np.fft.rfft(a[i:i + n] * win)) ** 2
        acc = sp if acc is None else acc + sp
        cnt += 1
    if acc is None:
        return None, None
    sp = acc / max(cnt, 1)
    fr = np.fft.rfftfreq(n, 1.0 / rate)
    tot = sp[1:].sum() + 1e-12
    res = []
    for a0, a1 in edges:
        res.append(round(100.0 * float(sp[(fr >= a0) & (fr < a1)].sum() / tot), 1))
    cen = float((fr * sp).sum() / (sp.sum() + 1e-12))
    return res, cen


def zcr(a):
    return float(((a[:-1] >= 0) != (a[1:] >= 0)).mean())


def report(tag, path, edges, as_loud):
    print("\n" + "=" * 92)
    print("%s   %s" % (tag, os.path.basename(path)))
    print("=" * 92)
    W, lo, hi = load_words(path)
    X = np.frombuffer(open(path, "rb").read()[:len(W) * 4], dtype="<i2").astype(np.int64)
    S = recover(lo, hi)

    print("整段: 字数 %d  int16 %d  |S| %d" % (len(W), len(X), len(S)))
    print("非零半字位置: 低半非零 %.1f%%  高半非零 %.1f%%  两半皆 0 %.1f%%  两半皆非 0 %.1f%%"
          % (100.0 * (lo != 0).mean(), 100.0 * (hi != 0).mean(),
             100.0 * ((lo == 0) & (hi == 0)).mean(),
             100.0 * ((lo != 0) & (hi != 0)).mean()))

    # 半字漂移：非零半字是否换边
    side = np.where(lo != 0, 0, np.where(hi != 0, 1, -1))
    v = side[side >= 0]
    flip = int((v[:-1] != v[1:]).sum())
    print("半字换边次数 %d 次 / %d 字 = 每 %.1f 字一次" %
          (flip, len(v), (len(v) / max(flip, 1))))

    print("\nT1 成对重复检验 (在还原流 S 上):")
    for dec in (2, 4):
        a, b = S[0::dec], S[1::dec]
        m = min(len(a), len(b))
        eq = float((a[:m] == b[:m]).mean())
        cor = float(np.corrcoef(a[:m].astype('f8'), b[:m].astype('f8'))[0, 1]) if a[:m].std() > 0 else float('nan')
        print("   S[0::%d] vs S[1::%d]: 精确相等 %.1f%%   皮尔逊相关 %.3f" %
              (dec, dec, 100 * eq, cor))
    m2 = min(len(S[0::2]), len(S[2::2]))
    print("   S[0::2] vs S[2::2] (隔一个字): 精确相等 %.1f%%   <- 对照"
          % (100.0 * float((S[0::2][:m2] == S[2::2][:m2]).mean())))

    print("\nT2 游程直方图 (长度:个数,占比%%):")
    for name, a in (("S   ", S), ("S/2 ", S[0::2]), ("S/4 ", S[0::4])):
        print("   %s %s" % (name, runs(a)))

    print("\nT3 功率谱分带能量 %% （按不同声明速率）:")
    for name, a, rate in (("S   @16000", S, 16000), ("S/2 @8000 ", S[0::2], 8000),
                          ("S/4 @4000 ", S[0::4], 4000)):
        e, cen = bands(a, rate, edges)
        print("   %s  %s   谱重心 %.0f Hz" % (name, e, cen))

    print("\nT4 过零率: S@16k %.4f   S/2@8k %.4f   S/4@4k %.4f   (重复会让 ZCR 成比例缩小)"
          % (zcr(S), zcr(S[0::2]), zcr(S[0::4])))
    print("T5 rms %.1f  peak %d  |  S 的 lag1..8 自相关 %s"
          % (float(np.sqrt((S.astype('f8') ** 2).mean())), int(np.abs(S).max()),
             [round(float(np.corrcoef(S[:-l], S[l:])[0, 1]), 3) for l in range(1, 9)]))


def main():
    loud = sys.argv[1] if len(sys.argv) > 1 else \
        "transfer/_dumps14/utt_1789563477451_160000.pcm"
    ref = sys.argv[2] if len(sys.argv) > 2 else "record/pcm/pos01.pcm"
    edges = [(0, 1000), (1000, 2000), (2000, 3000), (3000, 4000),
             (4000, 5000), (5000, 6000), (6000, 7000), (7000, 8000)]
    print("分带: " + " ".join("%d-%dk" % (a // 1000, b // 1000) for a, b in edges))
    report("现场流(有语音)", loud, edges, True)
    report("对照:Sep-12 干净录音", ref, [(0, 1000), (1000, 2000), (2000, 3000), (3000, 4000),
                                        (4000, 5000), (5000, 6000), (6000, 7000), (7000, 8000)], False)


main()
