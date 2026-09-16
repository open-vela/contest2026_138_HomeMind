#!/usr/bin/env python3
"""HomeMind: 检查 PCM 里是否存在"整段重复播报"。

线索：本地 ASR 在同一段里把同一句话听了两遍
（如 "但我还想说你空调但我还想说你空调"），而小爱每句只念一次。
若真如此，说明采集/投递把一个区间重复写了 —— 这会让 ASR 直接崩。

判据：
  1. 半程互相关：x[0:N/2] 与 x[N/2:N] 在某个偏移处的相关峰。接近 1 即"重播"。
  2. 任意偏移的重播检测：在 lag 上滑动，找 x[t] 与 x[t+lag] 的最大相关。
  3. 能量包络：直接打印出来，用眼睛看有没有两个语音段。

用法: python3 pcm_replay_check.py <file.pcm> [...]
"""
import sys

import numpy as np

FR = 320            # 20 ms @16k 的 int16 数


def envelope(raw, n=60):
    nf = len(raw) // FR
    if nf < n:
        n = nf
    e = np.array([float(np.sqrt((raw[i * FR:(i + 1) * FR].astype(np.float64) ** 2).mean()))
                  for i in range(nf)])
    if e.max() <= 0:
        return ''
    q = e / e.max()
    chars = ' .:-=+*#%@'
    step = max(1, nf // n)
    return ''.join(chars[min(len(chars) - 1, int(v * (len(chars) - 1)))]
                   for v in q[::step])


def best_replay(x, lo_lag, hi_lag, block=4000):
    """在 [lo_lag, hi_lag] 内找最相关的偏移。返回 (lag, 相关值)。"""
    x = x - x.mean()
    n = len(x)
    best = (-1, 0.0)
    for lag in range(max(1, lo_lag), min(hi_lag, n - block)):
        a = x[:block]
        b = x[lag:lag + block]
        sa, sb = a.std(), b.std()
        if sa < 1e-9 or sb < 1e-9:
            continue
        c = float(np.corrcoef(a, b)[0, 1])
        if c > best[1]:
            best = (lag, c)
    return best


def main():
    for path in sys.argv[1:]:
        raw = np.fromfile(path, dtype='<i2')
        n = len(raw)
        print()
        print('=' * 100)
        print('%s  n=%d int16' % (path, n))
        print('=' * 100)
        print('  包络: %s' % envelope(raw))

        x = raw.astype(np.float64)

        # 1) 固定偏移的点检
        print('  固定偏移自相关（归一化，取前 4000 样点做窗）:')
        blk = 4000
        for lag in (FR, 2 * FR, 4 * FR, n // 4, n // 2 - 4, n // 2, 3 * n // 4):
            if lag + blk >= n or lag <= 0:
                continue
            a, b = x[:blk], x[lag:lag + blk]
            sa, sb = a.std(), b.std()
            c = 0.0 if sa < 1e-9 or sb < 1e-9 else float(np.corrcoef(a, b)[0, 1])
            print('    lag=%7d (%6.2f s)  corr=%+.4f' % (lag, lag / 16000.0, c))

        # 2) 最大重播相关
        lag, c = best_replay(x, n // 8, n // 2)
        print('  最大重播: lag=%d (%.2f s) corr=%+.4f  -> 若 corr>0.5 说明后半段是前半段的重播'
              % (lag, lag / 16000.0, c))

        # 3) 在整段里找"重复区间"：对每个 1 秒窗口，找其后最相似的窗口
        win = 16000
        if n >= 3 * win:
            print('  逐秒窗口的最强自我重复:')
            for i in range(0, min(5, n // win - 1)):
                seg = x[i * win:(i + 1) * win]
                best = (0, 0.0)
                for j in range(i + 1, n // win):
                    cand = x[j * win:(j + 1) * win]
                    ss, cs = seg.std(), cand.std()
                    if ss < 1e-9 or cs < 1e-9:
                        continue
                    cc = float(np.corrcoef(seg, cand)[0, 1])
                    if cc > best[1]:
                        best = (j, cc)
                print('    第 %d 秒 -> 第 %d 秒  corr=%+.3f' % (i, best[0], best[1]))


if __name__ == '__main__':
    main()
