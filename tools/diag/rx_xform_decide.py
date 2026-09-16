#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""决定端侧该怎么改：把「可在 C 里实现的几种变换」离线比一遍。

约束：云端 /v1/voice/utterance 只接受 rate 8000..48000，所以不能声明 4000。
候选（都保持正确的时间轴）：
  A  S   + ZOH x2  -> 16000   (只要每字取一个样点，代价最低)
  B  D   + ZOH x4  -> 16000   (去重复 + 保持)
  C  D   + LIN x4  -> 16000   (去重复 + 线性插值)
  D  D   + LIN x2  -> 8000
  E  S   + ZOH x1  -> 8000
  F  D   + ZOH x1  -> 4000    (对照，云端不收)
另附：D 用 FFT 精确重采样到 16000 作为上界参考。

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


def load(p):
    raw = open(p, 'rb').read()
    nb = len(raw) - len(raw) % 4
    X = np.frombuffer(raw[:nb], dtype='<i2').astype(np.int64)
    lo, hi = X[0::2], X[1::2]
    S = np.where(lo != 0, lo, np.where(hi != 0, hi, 0)).astype(np.int64)
    keep = np.ones(len(S), dtype=bool)
    i = 0
    while i < len(S) - 1:
        if S[i] == S[i + 1]:
            keep[i + 1] = False
            i += 2
        else:
            i += 1
    return S, S[keep]


def zoh(x, f):
    return np.repeat(x, f)


def lin(x, f):
    """线性插值 x[f] 倍（保持首尾）"""
    n = len(x)
    idx = np.arange(0, (n - 1) * f + 1, dtype=np.float64) / f
    i0 = np.floor(idx).astype(np.int64)
    i1 = np.minimum(i0 + 1, n - 1)
    w = idx - i0
    return (x[i0] * (1 - w) + x[i1] * w)


def fft_resample(x, f):
    n = len(x)
    sp = np.fft.rfft(x.astype('f8'))
    return np.fft.irfft(sp, n=n * f) * f


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
            X = np.frombuffer(open(p, 'rb').read(), dtype='<i2').astype(np.float64)
            cand.append((float(np.sqrt((X ** 2).mean())), p))
        cand.sort(reverse=True)
        if cand and cand[0][0] > 100:
            picks.append((truth, cand[0][1]))

    agg = {}

    def run(tag, arr, rate, truth):
        got = transcribe(np.asarray(arr, dtype='<i2').tobytes(), rate, min_rms=0) or ''
        s = lcs_ratio(got, truth)
        agg.setdefault(tag, []).append((s, got))
        return s, got

    for truth, path in picks:
        S, D = load(path)
        print('=' * 96)
        print('真值 %s   (%s)  S=%d D=%d' % (truth, os.path.basename(path)[-18:], len(S), len(D)))
        print('=' * 96)
        for tag, arr, rate in (
                ('A S+ZOH2->16000 ', zoh(S, 2), 16000),
                ('B D+ZOH4->16000 ', zoh(D, 4), 16000),
                ('C D+LIN4->16000 ', lin(D, 4), 16000),
                ('D D+LIN2->8000  ', lin(D, 2), 8000),
                ('E S+ZOH1->8000  ', zoh(S, 1), 8000),
                ('F D+ZOH1->4000  ', zoh(D, 1), 4000),
                ('G D+FFT4->16000 ', fft_resample(D, 4), 16000),
        ):
            s, got = run(tag, arr, rate, truth)
            print('  %s %5.1f%%  %s' % (tag, s, got), flush=True)
        print()

    print('=' * 96)
    print('汇总（字符级 LCS 均值）')
    print('=' * 96)
    for tag in sorted(agg, key=lambda k: -sum(x[0] for x in agg[k]) / len(agg[k])):
        v = [x[0] for x in agg[tag]]
        print('  %s  平均 %5.1f%%  (%s)' % (tag, sum(v) / len(v),
                                          ', '.join('%.0f%%' % x for x in v)))


main()
