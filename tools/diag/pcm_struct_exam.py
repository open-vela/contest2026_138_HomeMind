#!/usr/bin/env python3
"""HomeMind: 判定麦克风 PCM 的帧结构 / 有效采样率。

四项互相独立的判据，任一命中即可定案，不必依赖"听起来对不对"：

  A. 零插入      : 偶数位/奇数位的零占比是否显著不同（[L,0,L,0] 结构）
  B. 样本重复    : x[n]==x[n+1] / x[n]==x[n+2] 的占比（DMA 双写、slot 复制）
  C. 频谱镜像    : 频谱关于 Fs/4（=4 kHz）是否对称 —— 零插入必然产生镜像
  D. Nyquist 截止: 能量在 0-8 kHz 内的实际滚降点。真 16 kHz 采样，
                   有效带宽到 ~7.5 kHz；若在 ~3.7 kHz 就掉下去，
                   说明真实速率是 8 kHz（我们把样点数高估了一倍）。

用法: python3 pcm_struct_exam.py [目录或文件...]
"""
import glob
import os
import sys

import numpy as np

FS = 16000.0          # 目前上报给云端的速率
NF = 2048             # Welch 帧长
HOP = NF // 2

BANDS = [(0, 500), (500, 1000), (1000, 2000), (2000, 3000), (3000, 3500),
         (3500, 4000), (4000, 4500), (4500, 5000), (5000, 6000),
         (6000, 7000), (7000, 7500), (7500, 8000)]


def welch(x, fs):
    """平均周期图（Hann 窗，50% 重叠）。返回 (freqs, psd_linear)。"""
    win = np.hanning(NF)
    wnorm = np.sum(win ** 2)
    nfr = (len(x) - NF) // HOP + 1
    if nfr < 1:
        return None, None
    acc = np.zeros(NF // 2 + 1)
    for i in range(nfr):
        seg = x[i * HOP:i * HOP + NF] * win
        sp = np.fft.rfft(seg)
        acc += (sp.real ** 2 + sp.imag ** 2)
    acc /= (nfr * wnorm)
    return np.fft.rfftfreq(NF, 1.0 / fs), acc


def band_db(freqs, psd):
    out = []
    for lo, hi in BANDS:
        m = (freqs >= lo) & (freqs < hi)
        out.append(float(np.mean(psd[m])) if np.any(m) else 0.0)
    out = np.array(out)
    ref = np.max(out)
    if ref <= 0:
        return np.full(len(BANDS), -999.0)
    return 10.0 * np.log10(np.maximum(out, 1e-30) / ref)


def mirror_score(freqs, psd):
    """频谱关于 Fs/4 的对称性。零插入会把 0..Fs/4 的内容镜像到 Fs/4..Fs/2。
    返回相关系数：接近 1 = 存在镜像（说明有零插入 / 上采样）。"""
    lo = (freqs > 200) & (freqs < FS / 4 - 100)
    f_lo = freqs[lo]
    p_lo = np.log10(np.maximum(psd[lo], 1e-30))
    # 镜像位置 2*(FS/4) - f
    f_mi = FS / 2 - f_lo
    p_mi = np.interp(f_mi, freqs, np.log10(np.maximum(psd, 1e-30)))
    if p_lo.std() < 1e-9 or p_mi.std() < 1e-9:
        return 0.0
    return float(np.corrcoef(p_lo, p_mi)[0, 1])


def rolloff(freqs, psd, frac=0.85):
    """累计能量达到 frac 的频率。"""
    c = np.cumsum(psd)
    if c[-1] <= 0:
        return 0.0
    return float(freqs[np.searchsorted(c, frac * c[-1])])


def examine(path):
    raw = np.fromfile(path, dtype='<i2')
    n = len(raw)
    if n < 4096:
        print('%-46s  太短 (%d 样点)，跳过' % (os.path.basename(path), n))
        return None
    x = raw.astype(np.float64)
    x = x - x.mean()

    res = {}
    res['file'] = os.path.basename(path)
    res['n'] = n
    res['rms'] = float(np.sqrt(np.mean(x ** 2)))
    res['max'] = int(np.max(np.abs(raw)))
    res['zfrac'] = float(np.mean(raw == 0))

    # --- A/B: 位置结构 ---
    res['z_even'] = float(np.mean(raw[0::2] == 0))
    res['z_odd'] = float(np.mean(raw[1::2] == 0))
    res['e_even'] = float(np.mean(x[0::2] ** 2))
    res['e_odd'] = float(np.mean(x[1::2] ** 2))
    res['eq1'] = float(np.mean(raw[:-1] == raw[1:]))
    res['eq1_pair'] = float(np.mean(raw[0::2] == raw[1::2]))   # 相邻成对相同
    res['eq1_shift'] = float(np.mean(raw[1:-1:2] == raw[2::2]))  # 错位成对
    res['eq2'] = float(np.mean(raw[:-2] == raw[2:]))

    freqs, psd = welch(x, FS)
    if freqs is None:
        print('%-46s  帧数不足' % res['file'])
        return None
    res['bands'] = band_db(freqs, psd)
    res['mirror'] = mirror_score(freqs, psd)
    res['rolloff85'] = rolloff(freqs, psd)
    res['rolloff95'] = rolloff(freqs, psd, 0.95)
    return res


def main():
    args = sys.argv[1:]
    if not args:
        args = ['/tmp/voice_dump']
    files = []
    for a in args:
        if os.path.isdir(a):
            files += sorted(glob.glob(os.path.join(a, '*.pcm')))
        else:
            files.append(a)
    if not files:
        print('没有找到 PCM 文件')
        return

    results = []
    for f in files:
        r = examine(f)
        if r:
            results.append(r)

    print()
    print('=' * 108)
    print('A/B  位置结构   (零插入 / 样本重复)')
    print('=' * 108)
    print('%-46s %8s %7s %7s %7s %7s %7s' %
          ('file', 'rms', 'max', 'z_even', 'z_odd', 'eq1', 'eq2'))
    for r in results:
        print('%-46s %8.1f %7d %7.3f %7.3f %7.3f %7.3f' %
              (r['file'][:46], r['rms'], r['max'], r['z_even'], r['z_odd'],
               r['eq1'], r['eq2']))
    print()
    print('%-46s %9s %9s %9s' % ('file', 'eq1_pair', 'eq1_shift', 'e_odd/e_even'))
    for r in results:
        ratio = r['e_odd'] / r['e_even'] if r['e_even'] > 0 else 0.0
        print('%-46s %9.3f %9.3f %9.3f' %
              (r['file'][:46], r['eq1_pair'], r['eq1_shift'], ratio))

    print()
    print('=' * 108)
    print('C/D  频谱   (分带能量 dB，相对最强带；镜像相关；85%% 能量滚降点)')
    print('=' * 108)
    hdr = '%-30s' % 'file'
    for lo, hi in BANDS:
        hdr += '%7s' % ('%d-%d' % (lo, hi) if hi < 1000 else '%dk-%dk' % (lo / 1000, hi / 1000))
    hdr += '%8s %8s %8s' % ('mirror', 'ro85', 'ro95')
    print(hdr)
    for r in results:
        line = '%-30s' % r['file'][:30]
        for v in r['bands']:
            line += '%7.1f' % v
        line += '%8.2f %8.0f %8.0f' % (r['mirror'], r['rolloff85'], r['rolloff95'])
        print(line)

    print()
    print('=' * 108)
    print('汇总均值')
    print('=' * 108)
    if results:
        print('z_even=%.3f  z_odd=%.3f  |  eq1=%.3f  eq1_pair=%.3f  eq1_shift=%.3f  eq2=%.3f' % (
            np.mean([r['z_even'] for r in results]),
            np.mean([r['z_odd'] for r in results]),
            np.mean([r['eq1'] for r in results]),
            np.mean([r['eq1_pair'] for r in results]),
            np.mean([r['eq1_shift'] for r in results]),
            np.mean([r['eq2'] for r in results])))
        mb = np.mean([r['bands'] for r in results], axis=0)
        print('band dB: ' + '  '.join('%d-%d:%.1f' % (b[0], b[1], v)
                                      for b, v in zip(BANDS, mb)))
        print('mirror=%.3f  ro85=%.0f Hz  ro95=%.0f Hz' % (
            np.mean([r['mirror'] for r in results]),
            np.mean([r['rolloff85'] for r in results]),
            np.mean([r['rolloff95'] for r in results])))

    print()
    print('判读:')
    print('  z_odd >> z_even        -> 存在零插入 ([L,0,L,0])，真实速率是上报的一半')
    print('  eq1 或 eq1_pair 很高   -> 样本被重复投递 (每样点写两次)')
    print('  mirror > 0.6           -> 频谱在 4 kHz 处镜像 = 有零插入/上采样')
    print('  ro95 落在 3.5-4.2 kHz  -> 真实速率 ~8 kHz (我们多算了一倍样点)')
    print('  ro95 落在 6.5-7.8 kHz  -> 真实速率就是 16 kHz，问题在别处')


if __name__ == '__main__':
    main()
