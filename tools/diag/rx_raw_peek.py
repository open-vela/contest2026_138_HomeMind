#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""把现场流的原始样点直接打出来，钉死「重复发生在哪一级」。

用法: python _raw_peek.py <file.pcm> [起始字] [字数]
"""
import sys
import numpy as np


def main():
    p = sys.argv[1]
    w0 = int(sys.argv[2]) if len(sys.argv) > 2 else 4000
    nw = int(sys.argv[3]) if len(sys.argv) > 3 else 24
    raw = open(p, "rb").read()
    nb = len(raw) - len(raw) % 4
    W = np.frombuffer(raw[:nb], dtype="<i4").astype(np.int64)
    X = np.frombuffer(raw[:nb], dtype="<i2").astype(np.int64)

    print("file=%s  字数=%d" % (p, len(W)))
    print("\n[1] 第 %d 字起 %d 个字：word索引 | hex | 低半(int16) | 高半(int16)"
          % (w0, nw))
    for k in range(w0, min(w0 + nw, len(W))):
        lo = X[2 * k]
        hi = X[2 * k + 1]
        print("  %6d | %08x | %7d | %7d %s" %
              (k, int(W[k]) & 0xFFFFFFFF, lo, hi,
               "<== 低半" if lo != 0 else ("<== 高半" if hi != 0 else "<== 两半皆0")))

    # 非零样点序列（每字取非零半）
    lo = X[0::2]
    hi = X[1::2]
    S = np.where(lo != 0, lo, np.where(hi != 0, hi, 0)).astype(np.int64)
    print("\n[2] 该段「每字取非零半」得到的样点序列 S[%d:%d]:"
          % (w0, w0 + nw))
    print("   %s" % S[w0:w0 + nw].tolist())

    # 直接量重复：S 在 lag L 上的「非零样点精确相等率」
    print("\n[3] 重复级判定（只看样点非零的位置，避免静音把统计拉歪）")
    nz = np.flatnonzero(S != 0)
    for L in (1, 2, 3, 4, 8):
        a = nz[nz + L < len(S)]
        eq = float((S[a] == S[a + L]).mean())
        print("   S lag%d: 非零位置精确相等 %.1f%%  (n=%d)" % (L, 100 * eq, len(a)))

    # 字级重复
    print("\n[4] 字级：W[k] == W[k+1] 的奇偶分解")
    print("   (W[2k]  == W[2k+1]) = %.1f%%" % (100.0 * float((W[0:-1:2] == W[1::2]).mean())))
    print("   (W[2k+1]== W[2k+2]) = %.1f%%" % (100.0 * float((W[1:-1:2] == W[2::2]).mean())))

    # 32 位字里「非零半字」的滑移轨迹
    side = np.where(lo != 0, 0, np.where(hi != 0, 1, -1))
    fl = np.flatnonzero(side[1:] != side[:-1]) + 1
    print("\n[5] 非零半字换边位置 (前 20 个): %s" % (fl[:20] + 0).tolist())
    if len(fl) > 1:
        print("   换边间隔: 中位 %d  最小 %d  最大 %d" %
              (int(np.median(np.diff(fl))), int(np.min(np.diff(fl))), int(np.max(np.diff(fl)))))

    # 整段 S 的「相邻不同样点」相等率（对照量）
    d = np.diff(S)
    print("\n[6] S 相邻差: ==0 的比例 %.1f%% ；S 长度 %d" % (100.0 * float((d == 0).mean()), len(S)))
    # 用正确的成对提取（取每对第一个）后看是否还有重复
    print("    S[0::2] 相邻差==0 比例 %.1f%%" % (100.0 * float((np.diff(S[0::2]) == 0).mean())))
    print("    S[1::2] 相邻差==0 比例 %.1f%%" % (100.0 * float((np.diff(S[1::2]) == 0).mean())))


main()
