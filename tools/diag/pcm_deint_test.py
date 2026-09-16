#!/usr/bin/env python3
"""HomeMind: 判定 PCM 是否为"两路交织"。判决性实验。

已有证据：x[n]==x[n+2] 占 80.7%，x[n]==x[n+1] 仅 8.3%；
频谱关于 Fs/4(=4 kHz) 折叠，镜像相关 0.986，分带能量呈 V 形。
两者都指向 y = [a0,b0,a1,b1,...]（每帧 2 slot，两路各自是较低速率的信号）。
但"两路是什么"还没定。本脚本把偶/奇位拆开后逐一验证：

  H1 两路是同一路信号(8 kHz)交织 -> 拆开后各自是干净的 8 kHz 音频，
     在 Fs=8000 下频谱不再折叠(能量集中 0-4 kHz)
  H2 两路是同一路信号(16 kHz)交织 -> 拆开后各自在 Fs=16000 下正常，
     且偶奇两路高度相关(相关≈1)
  H3 偶位是真 16 kHz 音频、奇位是垃圾 -> 拆开后偶位在 Fs=16000 下正常，
     奇位能量极低或白噪
  H4 整条 16 kHz 流本来就是对的 -> 拆开后反而变糊(不成立方向)

用法: python3 pcm_deint_test.py <file.pcm> [...]
"""
import sys

import numpy as np

NF = 2048
BANDS16 = [(0, 500), (500, 1000), (1000, 2000), (2000, 3000), (3000, 4000),
           (4000, 5000), (5000, 6000), (6000, 7000), (7000, 8000)]


def welch(x, fs, nf=NF):
    win = np.hanning(nf)
    wnorm = np.sum(win ** 2)
    hop = nf // 2
    nfr = (len(x) - nf) // hop + 1
    if nfr < 1:
        return None, None
    acc = np.zeros(nf // 2 + 1)
    for i in range(nfr):
        seg = x[i * hop:i * hop + nf] * win
        sp = np.fft.rfft(seg)
        acc += sp.real ** 2 + sp.imag ** 2
    acc /= (nfr * wnorm)
    return np.fft.rfftfreq(nf, 1.0 / fs), acc


def mirror(freqs, psd, fs):
    """频谱关于 fs/4 的折叠程度。"""
    q = fs / 4
    lo = (freqs > 100) & (freqs < q - 100)
    f_lo = freqs[lo]
    p_lo = np.log10(np.maximum(psd[lo], 1e-30))
    p_mi = np.interp(fs / 2 - f_lo, freqs, np.log10(np.maximum(psd, 1e-30)))
    if p_lo.std() < 1e-9 or p_mi.std() < 1e-9:
        return 0.0
    return float(np.corrcoef(p_lo, p_mi)[0, 1])


def rolloff(freqs, psd, frac):
    c = np.cumsum(psd)
    if c[-1] <= 0:
        return 0.0
    return float(freqs[np.searchsorted(c, frac * c[-1])])


def bands(freqs, psd, spec):
    out = []
    for lo, hi in spec:
        m = (freqs >= lo) & (freqs < hi)
        out.append(float(np.mean(psd[m])) if np.any(m) else 0.0)
    out = np.array(out)
    ref = out.max()
    return np.full(len(spec), -999.0) if ref <= 0 else 10 * np.log10(np.maximum(out, 1e-30) / ref)


def describe(tag, x, fs, spec):
    x = x - x.mean()
    freqs, psd = welch(x, fs)
    if freqs is None:
        print('  %-22s 样点不足' % tag)
        return
    b = bands(freqs, psd, spec)
    print('  %-22s  n=%6d rms=%7.1f  ro85=%5.0f ro95=%5.0f  mirror=%.3f' %
          (tag, len(x), np.sqrt(np.mean(x ** 2)), rolloff(freqs, psd, .85),
           rolloff(freqs, psd, .95), mirror(freqs, psd, fs)))
    print('      ' + ' '.join('%d-%d:%.1f' % (s[0], s[1], v) for s, v in zip(spec, b)))


def main():
    for path in sys.argv[1:]:
        raw = np.fromfile(path, dtype='<i2')
        if len(raw) < 8192:
            print('%s 太短' % path)
            continue
        print()
        print('=' * 100)
        print(path, ' n=%d  bytes=%d' % (len(raw), len(raw) * 2))
        print('=' * 100)
        print('  前 40 个原始样点: ' + ' '.join(str(v) for v in raw[:40]))
        print('  第 8000 起的 40 个: ' + ' '.join(str(v) for v in raw[8000:8040]))

        even = raw[0::2].astype(np.float64)
        odd = raw[1::2].astype(np.float64)
        print()
        print('  --- 单看整条流（现状：按 16 kHz 单声道上传） ---')
        describe('whole @16000', raw.astype(np.float64), 16000, BANDS16)

        print()
        print('  --- 拆开偶/奇位 ---')
        describe('even @16000', even, 16000, BANDS16)
        describe('odd  @16000', odd, 16000, BANDS16)
        b8 = [(0, 250), (250, 500), (500, 1000), (1000, 2000), (2000, 3000),
              (3000, 3500), (3500, 4000)]
        describe('even @8000', even, 8000, b8)
        describe('odd  @8000', odd, 8000, b8)

        # 偶/奇两路的相似性
        e = even - even.mean()
        o = odd - odd.mean()
        n = min(len(e), len(o))
        e, o = e[:n], o[:n]
        def corr(a, b):
            sa, sb = a.std(), b.std()
            return 0.0 if sa < 1e-9 or sb < 1e-9 else float(np.corrcoef(a, b)[0, 1])
        print()
        print('  --- 两路关系 ---')
        print('    corr(even[k], odd[k])      = %+.4f' % corr(e, o))
        print('    corr(even[k], odd[k-1])    = %+.4f' % corr(e[1:], o[:-1]))
        print('    corr(even[k], odd[k+1])    = %+.4f' % corr(e[:-1], o[1:]))
        print('    corr(even, even shifted 1) = %+.4f' % corr(e[:-1], e[1:]))
        print('    rms even=%.1f  rms odd=%.1f  ratio=%.3f' %
              (e.std(), o.std(), (o.std() / e.std()) if e.std() > 0 else 0))
        print('    去交织后(偶路)的样点数=%d -> 若真速率 8000 Hz 则时长 %.2f s'
              % (len(even), len(even) / 8000.0))
        print('    去交织后(偶路)的样点数=%d -> 若真速率 16000 Hz 则时长 %.2f s'
              % (len(even), len(even) / 16000.0))


if __name__ == '__main__':
    main()
