#!/usr/bin/env python3
"""HomeMind: 用"基频是否落在人声范围"定案真实采样率。

已知（原始样点肉眼可判）：`80 0 80 0 -197 0 -197 0 ...`
  = 真实音频的一个样点占了 4 个 int16（写两次 + 夹一个 0）。
所以存在一个正确的抽取因子 D 和相位 p，使得 x[p::D] 才是真音频。

判据（互相独立）：
  1. 基频 F0 = 自相关峰位置 -> 换算成 Hz。中文女声 TTS 正常在 170-280 Hz；
     若算出来 85-140 Hz（慢一倍）或 340-560 Hz（快一倍），说明 D 选错了。
  2. 折叠 mirror：D 正确时频谱不该在 Fs/4 折叠。
  3. ro95/Nyquist：正确时语音能量集中在低频，占比应明显 < 1。

用法: python3 pcm_rate_verdict.py <file.pcm> [...]
"""
import sys

import numpy as np

NFFT = 4096


def spec(x, fs):
    x = x - x.mean()
    win = np.hanning(NFFT)
    hop = NFFT // 2
    nfr = max(0, (len(x) - NFFT) // hop + 1)
    if nfr < 1:
        return None, None
    acc = np.zeros(NFFT // 2 + 1)
    for i in range(nfr):
        sp = np.fft.rfft(x[i * hop:i * hop + NFFT] * win)
        acc += sp.real ** 2 + sp.imag ** 2
    acc /= nfr
    return np.fft.rfftfreq(NFFT, 1.0 / fs), acc


def mirror_score(freqs, psd, fs):
    q = fs / 4
    m = (freqs > 100) & (freqs < q - 100)
    if not np.any(m):
        return 0.0
    a = np.log10(np.maximum(psd[m], 1e-30))
    b = np.interp(fs / 2 - freqs[m], freqs, np.log10(np.maximum(psd, 1e-30)))
    if a.std() < 1e-9 or b.std() < 1e-9:
        return 0.0
    return float(np.corrcoef(a, b)[0, 1])


def estimate_f0(x, fs):
    """自相关基频估计。返回 (f0_hz, 峰值强度 0-1)。只在 60-400 Hz 内找。"""
    x = x - x.mean()
    if len(x) < 512 or x.std() < 1e-9:
        return 0.0, 0.0
    # 先做能量门，只取有声段
    fl = 400
    nfr = len(x) // fl
    if nfr < 4:
        return 0.0, 0.0
    seg = x[:nfr * fl].reshape(nfr, fl)
    en = (seg ** 2).mean(axis=1)
    th = en.max() * 0.15
    vo = seg[en > th]
    if len(vo) < 8:
        vo = seg
    y = vo.reshape(-1) - vo.mean()
    if len(y) > 48000:
        y = y[:48000]
    y = y * np.hanning(len(y))
    ac = np.correlate(y, y, 'full')[len(y) - 1:]
    ac /= (ac[0] + 1e-30)
    lo = int(fs / 400.0)
    hi = int(fs / 60.0)
    hi = min(hi, len(ac) - 1)
    if hi <= lo:
        return 0.0, 0.0
    seg2 = ac[lo:hi]
    k = int(np.argmax(seg2)) + lo
    strength = float(ac[k])
    # 抛物线插值细化峰位
    if 0 < k < len(ac) - 1:
        y0, y1, y2 = ac[k - 1], ac[k], ac[k + 1]
        den = (y0 - 2 * y1 + y2)
        if abs(den) > 1e-12:
            k = k + 0.5 * (y0 - y2) / den
    return float(fs / k), strength


def main():
    for path in sys.argv[1:]:
        raw = np.fromfile(path, dtype='<i2')
        if len(raw) < 20000:
            print('%s 太短' % path)
            continue
        print()
        print('=' * 104)
        print(path, ' n_int16=%d  若 16000/s 则时长 %.2f s' % (len(raw), len(raw) / 16000.0))
        print('=' * 104)
        print('  原始窗: ' + ' '.join(str(v) for v in raw[24000:24064]))

        print()
        print('  %-14s %8s %10s %9s %10s %9s' %
              ('抽取', 'n', 'rms', 'F0(Hz)', '自相关峰', 'mirror'))
        best = []
        for D in (1, 2, 3, 4, 5, 6, 8):
            for p in range(D):
                if p >= D:
                    continue
                xs = raw[p::D].astype(np.float64)
                fs = 16000.0 / D
                if len(xs) < 8192:
                    continue
                f0, st = estimate_f0(xs, fs)
                freqs, psd = spec(xs, fs)
                ms = mirror_score(freqs, psd, fs) if freqs is not None else 0.0
                if D > 2 and p > 1:
                    continue
                flag = ''
                if 150 <= f0 <= 300 and st > 0.25 and ms < 0.5:
                    flag = '  <== 人声合理'
                print('  D=%d p=%d @%5.0fHz %8d %10.1f %9.1f %10.3f %9.3f%s' %
                      (D, p, fs, len(xs), np.sqrt(((xs - xs.mean()) ** 2).mean()),
                       f0, st, ms, flag))
                if 150 <= f0 <= 300:
                    best.append((D, p, f0, st, ms))

        # 单位面积检查：真实语音每帧能量变化 + 谱滚降
        print()
        print('  --- 各候选的谱滚降（ro95 / Nyquist）与分带 ---')
        for D, p in [(1, 0), (2, 0), (2, 1), (4, 0), (4, 2)]:
            xs = raw[p::D].astype(np.float64)
            fs = 16000.0 / D
            if len(xs) < 8192:
                continue
            freqs, psd = spec(xs, fs)
            c = np.cumsum(psd)
            ro = freqs[np.searchsorted(c, 0.95 * c[-1])] if c[-1] > 0 else 0
            print('  D=%d p=%d @%5.0fHz  ro95=%6.0f Hz  ro95/Nyq=%.2f' %
                  (D, p, fs, ro, ro / (fs / 2)))
            n = len(psd)
            print('      ' + ' '.join('%.0f-%.0f:%.0f' % (
                freqs[int(i * n / 6)], freqs[min(int((i + 1) * n / 6), n - 1)],
                10 * np.log10(max(psd[int(i * n / 6):max(int((i + 1) * n / 6), int(i * n / 6) + 1)].mean(),
                                  1e-30) / max(psd.max(), 1e-30)))
                for i in range(6)))

        # 去重后的原始样点形状（看是否是自然的连续样点）
        print()
        print('  --- 去重后前 40 个样点（D=4 p=0 / D=4 p=2 / D=2 p=0）---')
        for D, p in [(4, 0), (4, 2), (2, 0), (2, 1)]:
            xs = raw[p::D]
            print('   D=%d p=%d: %s' % (D, p, ' '.join(str(v) for v in xs[6000:6040])))

        print()
        if best:
            b = sorted(best, key=lambda t: -t[3])[0]
            print('  ==> 最可能: D=%d p=%d，真实速率约 %.0f Hz，F0=%.1f Hz' %
                  (b[0], b[1], 16000.0 / b[0], b[2]))
        else:
            print('  ==> 没有候选落在人声 F0 范围，需要另找解释')


if __name__ == '__main__':
    main()
