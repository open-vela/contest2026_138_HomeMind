#!/usr/bin/env python3
"""HomeMind: 离线 ASR 判决 —— 哪种 PCM 重构能被正确听出来。

有真值（/tmp/decode_exp_map.json 记录了每个 dump 对应小爱念的那句话）。
对每句挑最响的一段，遍历多种"抽取因子 D + 声明速率 R"组合，直接调本地
faster-whisper，看哪种组合能把原句听回来。ASR 就是裁判。

背景：原始样点呈 `80 0 80 0 -197 0 -197 0 ...`（一个真实样点占 4 个 int16：
写两次、中间夹一个 0）。所以正确的重构大概率是 raw[0::4]。
但"真实速率是 4000 还是 8000"无法从静态分析判定，只能让 ASR 说话。

用法: python3 asr_sweep.py
"""
import glob
import json
import os
import re
import sys

import numpy as np

sys.path.insert(0, '/home/hfy/homemind-backend')

DUMP = '/tmp/voice_dump'
MAP = '/tmp/decode_exp_map.json'

VARIANTS = [
    ('raw  @16000  当前做法(=现状基线)', lambda r: r, 16000),
    ('raw  @8000                       ', lambda r: r, 8000),
    ('raw  @4000                       ', lambda r: r, 4000),
    ('d2   @16000                      ', lambda r: r[0::2], 16000),
    ('d2   @8000                       ', lambda r: r[0::2], 8000),
    ('d4   @16000                      ', lambda r: r[0::4], 16000),
    ('d4   @8000                       ', lambda r: r[0::4], 8000),
    ('d4   @4000                       ', lambda r: r[0::4], 4000),
    ('d4   @2000                       ', lambda r: r[0::4], 2000),
    ('d4b  @4000   (相位 2)             ', lambda r: r[2::4], 4000),
    ('d4b  @8000   (相位 2)             ', lambda r: r[2::4], 8000),
    ('d8   @2000                       ', lambda r: r[0::8], 2000),
]


def norm_text(t):
    return re.sub(r'[^\u4e00-\u9fff]', '', t or '')


def hit(text, truth):
    """真值句的任意连续 2 字以上子串出现在转写结果里就算命中。"""
    t = norm_text(text)
    tr = norm_text(truth)
    if not t:
        return False
    for i in range(len(tr) - 1):
        if tr[i:i + 2] in t:
            return True
    return False


def rms_of(raw):
    a = raw.astype(np.float32)
    return float(np.sqrt((a * a).mean())) if len(a) else 0.0


def main():
    m = json.load(open(MAP, encoding='utf-8'))
    from app.local_asr import transcribe

    picks = []
    for truth, files in m.items():
        best, bestr = None, -1
        for f in files:
            p = os.path.join(DUMP, f)
            if not os.path.exists(p):
                continue
            r = np.fromfile(p, dtype='<i2')
            v = rms_of(r)
            if v > bestr:
                best, bestr = p, v
        if best:
            picks.append((truth, best, bestr))
            print('选中 %-18s -> %s (rms=%.0f)' % (truth, os.path.basename(best), bestr),
                  flush=True)

    print()
    results = {}
    for truth, path, _ in picks:
        raw = np.fromfile(path, dtype='<i2')
        print('=' * 96)
        print('真值: %s   (%s, %d int16)' % (truth, os.path.basename(path), len(raw)))
        print('=' * 96)
        for name, fn, rate in VARIANTS:
            xs = fn(raw)
            pcm = xs.astype('<i2').tobytes()
            try:
                got = transcribe(pcm, rate, min_rms=0) or ''
            except Exception as e:
                got = '<异常 %s>' % e
            ok = hit(got, truth)
            results.setdefault(name, []).append(ok)
            print('  %s  %s  %s' % (name, 'HIT ' if ok else 'miss', got), flush=True)
        print()

    print('=' * 96)
    print('汇总（命中 / 总数）')
    print('=' * 96)
    for name, _, _ in VARIANTS:
        v = results.get(name, [])
        print('  %s  %d/%d' % (name, sum(v), len(v)))


if __name__ == '__main__':
    main()
