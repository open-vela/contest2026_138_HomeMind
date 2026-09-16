#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""速率判决 v2：局部相位跟踪去重复 + 声明速率扫描，用 whisper 当裁判。

结构事实（_raw_peek.py 已钉死）：
  现场流每个 32bit 字 = 一个 16 位样点 + 一个填 0 的半字；同一个样点占 **2 个连续字**；
  配对相位偶尔错位 → 全局固定相位去重复会错一半，必须局部跟踪（游程即配对）。

本脚本对每段 dump 生成三条流：
  A  S      = 每字取非零半（含重复）
  B  Sdedup = 游程去重复（局部相位跟踪）
  C  Sdedup2= 再去一层（若仍有成对）
然后按一串候选速率送 whisper，用字符级 LCS 打分，看峰值落在哪个速率。

必须用服务 venv 跑。
"""
import json
import os
import re
import sys

import numpy as np

sys.path.insert(0, '/home/hfy/homemind-backend')

DUMP = '/tmp/voice_dump'
MAP = '/tmp/decode_exp_map.json'

RATES = [4000, 6000, 8000, 10000, 12000, 14000, 16000, 20000, 24000]


def load(p):
    raw = open(p, 'rb').read()
    nb = len(raw) - len(raw) % 4
    W = np.frombuffer(raw[:nb], dtype='<i4').astype(np.int64)
    X = np.frombuffer(raw[:nb], dtype='<i2').astype(np.int64)
    lo, hi = X[0::2], X[1::2]
    S = np.where(lo != 0, lo, np.where(hi != 0, hi, 0)).astype(np.int64)
    # 局部相位跟踪去重复：相邻相等就连取一个（游程即配对）
    keep = np.ones(len(S), dtype=bool)
    i = 0
    while i < len(S) - 1:
        if S[i] == S[i + 1]:
            keep[i + 1] = False
            i += 2
        else:
            i += 1
    D = S[keep]
    # 再看是否还有一层重复
    keep2 = np.ones(len(D), dtype=bool)
    i = 0
    while i < len(D) - 1:
        if D[i] == D[i + 1]:
            keep2[i + 1] = False
            i += 2
        else:
            i += 1
    D2 = D[keep2]
    # 每次保留的样点代表多少原始样点 -> 用于把「真实速率」反推
    return dict(S=S, D=D, D2=D2, n_word=len(W),
                r_eq=100.0 * float((S[:-1] == S[1:]).mean()),
                r_eq2=100.0 * float((D[:-1] == D[1:]).mean()) if len(D) > 1 else 0.0)


def norm(t):
    return re.sub(r'[^\u4e00-\u9fff]', '', t or '')


def lcs_ratio(hyp, truth):
    a, b = norm(hyp), norm(truth)
    if not a or not b:
        return 0.0
    dp = [0] * (len(b) + 1)
    for i in range(1, len(a) + 1):
        prev = 0
        for j in range(1, len(b) + 1):
            cur = dp[j]
            dp[j] = prev + 1 if a[i - 1] == b[j - 1] else max(dp[j], dp[j - 1])
            prev = cur
    return 100.0 * dp[len(b)] / len(b)


def main():
    m = json.load(open(MAP, encoding='utf-8'))
    from app.local_asr import transcribe
    picks = []
    for truth, files in m.items():
        cand = []
        for f in files:
            p = os.path.join(DUMP, f)
            if not os.path.exists(p):
                continue
            X = np.frombuffer(open(p, 'rb').read(), dtype='<i2').astype(np.float64)
            cand.append((float(np.sqrt((X ** 2).mean())), p))
        cand.sort(reverse=True)
        if cand and cand[0][0] > 100:          # 只用有语音的
            picks.append((truth, cand[0][1], cand[0][0]))

    for truth, path, rms in sorted(picks, key=lambda t: -t[2]):
        d = load(path)
        print('=' * 104)
        print('真值 %s   %s  rms=%.0f' % (truth, os.path.basename(path)[-20:], rms))
        print('  字数 %d  S=%d (相邻相等 %.1f%%)  D=%d (相邻相等 %.1f%%)  D2=%d'
              % (d['n_word'], len(d['S']), d['r_eq'], len(d['D']), d['r_eq2'], len(d['D2'])))
        print('=' * 104)
        for tag, arr in (('S      ', d['S']), ('Sdedup ', d['D']), ('Sdedup2', d['D2'])):
            line = []
            for rate in RATES:
                pcm = np.asarray(arr, dtype='<i2').tobytes()
                try:
                    got = transcribe(pcm, rate, min_rms=0) or ''
                except Exception as e:
                    got = ''
                line.append((rate, lcs_ratio(got, truth), got))
            best = max(line, key=lambda t: t[1])
            print('  %s 最佳 %d Hz -> %5.1f%%  "%s"' % (tag, best[0], best[1], best[2]))
            print('      逐速率: %s' % '  '.join('%d:%.0f%%' % (r, s) for r, s, _ in line))
            print('      %s' % '  |  '.join('%d="%s"' % (r, t) for r, _, t in line if t))
        print()


main()
