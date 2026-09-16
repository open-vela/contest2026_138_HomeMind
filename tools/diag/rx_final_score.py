#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""终审判分：端侧已把 PCM 归一化，这里直接对「上传件」做 ASR + 结构双判决。

判据：
  A 结构：零占比应 <12%、两半皆非零应 >85%（旧件是 51-56% / 0.0%）
  B ASR：字符级 LCS 相似度（不是 2-gram 子串），对已知真值打分
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


def struct(p):
    raw = open(p, 'rb').read()
    nb = len(raw) - len(raw) % 4
    W = np.frombuffer(raw[:nb], dtype='<i4').astype(np.int64)
    X = np.frombuffer(raw[:nb], dtype='<i2').astype(np.int64)
    lo = W & 0xFFFF
    lo = np.where(lo >= 32768, lo - 65536, lo)
    hi = ((W >> 16) & 0xFFFF).astype(np.int64)
    hi = np.where(hi >= 32768, hi - 65536, hi)
    rms = float(np.sqrt((X.astype('f8') ** 2).mean()))
    return dict(rms=rms, z=100.0 * float((X == 0).mean()),
                half0=100.0 * float((((lo == 0) & (hi != 0)) | ((lo != 0) & (hi == 0))).mean()),
                full=100.0 * float(((lo != 0) & (hi != 0)).mean()),
                n=len(X))


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
    print('%-18s %-22s %6s %6s %6s %6s | %5s %s' %
          ('真值', '文件', 'rms', '零%', '半0%', '两半%', 'LCS', '转写'))
    print('-' * 122)
    per = {}
    for truth, files in m.items():
        rows = []
        for f in files:
            p = os.path.join(DUMP, f)
            if not os.path.exists(p):
                continue
            st = struct(p)
            best = (0.0, '', 0)
            for rate in (8000, 16000, 12000):
                got = transcribe(open(p, 'rb').read(), rate, min_rms=0) or ''
                s = lcs_ratio(got, truth)
                if s > best[0]:
                    best = (s, got, rate)
            rows.append((st['rms'], f, st, best))
        rows.sort(reverse=True, key=lambda r: r[0])
        for rms, f, st, best in rows[:2]:
            per.setdefault(truth, []).append(best[0])
            print('%-18s %-22s %6.1f %6.1f %6.1f %6.1f | %5.1f %s' %
                  (truth, f[:22], st['rms'], st['z'], st['half0'], st['full'],
                   best[0], best[1][:40]))
    print()
    print('=' * 122)
    allv = [v for vs in per.values() for v in vs]
    for truth, vs in per.items():
        print('  %-18s 最好 %5.1f%%' % (truth, max(vs) if vs else 0))
    if allv:
        print('  --- 平均 %.1f%%   最高 %.1f%%   完全正确 %d/%d' %
              (sum(allv) / len(allv), max(allv),
               sum(1 for v in allv if v >= 99.9), len(allv)))


main()
