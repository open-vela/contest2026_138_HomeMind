#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""校准 whisper：带宽/速率到底哪个才是 ASR 糊的原因。

背景矛盾点：
  - 字节率实测 32000 B/s、每个 16 位样点占 8 字节 => 独立样点率约 4000/s（缓冲区 5 s）。
    若成立，内容带宽 ≤ 2 kHz，那是"很差的可懂度"。
  - 但 whisper 对同一段却能 100% 还原 "播放一首音乐"。
  => 必须先校准：把 2 kHz 低通后的同一段送给 whisper，看它是否仍能 100%。
     若仍 100%，则 2 kHz 带宽不影响判定，4000 Hz 结论成立；
     若掉成垃圾，则说明真实速率更高（缓冲区时长不足 5 s），"1.5×" 是 whisper 的偏好。

做法：取已知真值、已知能被 100% 还原的那段 dump，
      D = 局部去重复流；按候选真实速率 R 归一化到 16 kHz，再做候选截止的砖墙低通，
      送 whisper，统计字符级 LCS。另附 F0 估计（基频物理锚点）。

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


def brickwall_lp(x, rate, fc):
    """FFT 砖墙低通（只在正频率上乘 1，负频率镜像同步），返回同长度实数序列"""
    n = len(x)
    sp = np.fft.rfft(x.astype('f8'))
    fr = np.fft.rfftfreq(n, 1.0 / rate)
    sp[fr > fc] = 0.0
    return np.fft.irfft(sp, n=n)


def f0_est(x, rate, lo=60.0, hi=400.0):
    """自相关 F0（只用清音以外的帧），返回中位 F0"""
    x = x.astype('f8')
    x = x - x.mean()
    win = int(0.04 * rate)
    hop = int(0.02 * rate)
    vals = []
    for i in range(0, len(x) - win, hop):
        f = x[i:i + win]
        if np.sqrt((f ** 2).mean()) < 30:
            continue
        f = f * np.hanning(len(f))
        ac = np.correlate(f, f, 'full')[len(f) - 1:]
        if ac[0] <= 0:
            continue
        ac = ac / ac[0]
        a, b = int(rate / hi), int(rate / lo)
        if b >= len(ac):
            continue
        k = a + int(np.argmax(ac[a:b]))
        if ac[k] > 0.3:
            vals.append(rate / k)
    return float(np.median(vals)) if vals else float('nan')


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
    truth = '播放一首音乐。'
    cand = []
    for f in m[truth]:
        p = os.path.join(DUMP, f)
        X = np.frombuffer(open(p, 'rb').read(), dtype='<i2').astype(np.float64)
        cand.append((float(np.sqrt((X ** 2).mean())), p))
    cand.sort(reverse=True)
    p = cand[0][1]
    S, D = load(p)
    print('用 %s  真值 "%s"' % (os.path.basename(p), truth))
    print('S=%d 样点(含重复)  D=%d 样点(去重复)  若时长 5.0s 则 S=%.0f/s D=%.0f/s'
          % (len(S), len(D), len(S) / 5.0, len(D) / 5.0))
    print('F0: 以 D 自己的速率域估计 = %.1f 周期/千样点 -> 各候选速率下的 Hz:'
          % (f0_est(D, 1000.0)))
    for r in (4000, 6000, 8000, 12000):
        print('    若真实速率 %5d Hz -> F0 = %6.1f Hz' % (r, f0_est(D, float(r))))

    print('\n校准表：同一段音频，不同「假设真实速率 + 低通截止」下的 whisper 得分')
    print('%-10s %-12s %s' % ('假设速率', '低通截止', '得分 / 识别文本'))
    print('-' * 96)
    for rate in (4000, 6000, 8000, 12000):
        for fc in (99999, 3000, 2000, 1500):
            y = D if fc > 9999 else brickwall_lp(D, rate, fc)
            got = transcribe(np.asarray(y, dtype='<i2').tobytes(), rate, min_rms=0) or ''
            print('%-10d %-12s %5.1f%%  %s'
                  % (rate, ('无低通' if fc > 9999 else '%d Hz' % fc),
                     lcs_ratio(got, truth), got), flush=True)
        print()

    print('对照：把 D 直接按各速率送（等价于上面"无低通"行）已完成。')
    print('解读：若"4000 Hz + 2 kHz 低通"仍能高分，则 2 kHz 带宽不构成 ASR 障碍，')
    print('      即现场流真实速率约 4000 Hz，端侧现在按 16000 声明是错的。')


main()
