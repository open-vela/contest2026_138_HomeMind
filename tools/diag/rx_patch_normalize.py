# -*- coding: utf-8 -*-
"""给 overlay 的 nsh_commands.c 打上「RX 归一化」补丁。

现场 I2S RX 交给上层的不是 PCM：每个 32 位 FIFO 字只有一个 16 位样点
（另一半恒为 0），且同一个样点在连续两个字里原值重复。整块 memcpy 出去
= 把容器字当 16 位 PCM 用，真实样点率只剩 1/4，云端 ASR 因此糊。

本补丁把采集拷贝改成逐字流式归一化：取非零半字 -> 吃掉成对重复 ->
线性插值 x2 -> 输出 8000 Hz（落在云端可收的 8000..48000）。

幂等：带 g_hm_rx_normalize 标记，已打过就跳过。
"""
import io
import os
import shutil
import sys

F = ('/home/hfy/work/openvela-clean-20260830/contest2026_138_HomeMind'
     '/firmware/ai_agent_overlay/src/channels/nsh_commands.c')
BAK = '/home/hfy/_diag/nsh_commands.c.pre-rxnorm'

s = io.open(F, encoding='utf-8').read()

if 'g_hm_rx_normalize' in s:
    print('ALREADY PATCHED, nothing to do')
    sys.exit(0)

if not os.path.exists(BAK):
    shutil.copy2(F, BAK)
    print('backup -> %s' % BAK)


def sub1(old, new, tag):
    global s
    n = s.count(old)
    if n != 1:
        print('ABORT %s: count=%d' % (tag, n))
        sys.exit(1)
    s = s.replace(old, new)
    print('ok %s' % tag)


# --- 1) 跨块状态变量（放在函数声明区，每次会话重置） -----------------------
AOLD = "    static unsigned char prev_chunk[640];\n"
ANEW = (AOLD +
        "    /* g_hm_rx_normalize: RX 归一化状态，跨块保持 */\n"
        "    int conv_samp = 0;\n"
        "    int rx_have_last = 0;\n"
        "    int rx_last = 0;\n"
        "    int rx_skipped = 0;\n"
        "    int rx_pend_valid = 0;\n"
        "    int rx_pend = 0;\n")
sub1(AOLD, ANEW, 'state-vars')

# --- 2) 删掉不再使用的 int k; --------------------------------------------
sub1("            int nsamp = (int)apb->nbytes / 2;\n            int k;\n",
     "            int nsamp = (int)apb->nbytes / 2;\n", 'drop-k')

# --- 3) 把整块 memcpy 换成逐字归一化 --------------------------------------
BOLD = """            if (got + nsamp * 2 <= want) {
                memcpy(dst + got, apb->samp, nsamp * 2);
                for (k = 0; k < nsamp; k++) {
                    if (src[k] != 0) {
                        if (k & 1)
                            nz_odd++;
                        else
                            nz_even++;
                    }
                }
                got += nsamp * 2;
                out_samp += nsamp;
"""
BNEW = """            if (got + nsamp * 2 <= want) {
                /* g_hm_rx_normalize: 这里原来是一句整块 memcpy，把 I2S RX 的
                 * 32 位「容器字」直接当成 16 位 PCM 交给上层。实测每个容器字
                 * 里只有一个 16 位样点（另一半恒为 0），且同一个样点会在连续
                 * 两个字里原值重复（游程直方图 93% 为长度 2，自然语音绝无此
                 * 形态）=> 真实样点率只剩 1/4，还掺进一半静音。离线用服务端
                 * ASR 判决：原样送出字符级相似度仅 3.6%，归一化后 48%。
                 *
                 * 逐字流式归一化三步：
                 *   1) 每个 32 位字取非零半字 -> 一个样点（两半皆零给 0）
                 *   2) 吃掉与上一个保留样点原值相同的重复（成对，只吃一次）
                 *   3) 线性插值 x2：y[2n]=x[n]、y[2n+1]=(x[n]+x[n+1])/2，
                 *      需要 1 个样点前瞻，故上一个保留样点留到下一轮才输出。
                 * 输出 8000 Hz —— 云端 /v1/voice/utterance 只收 8000..48000。
                 */
                int nword = (int)apb->nbytes / 4;
                int w;

                for (w = 0; w < nword; w++) {
                    int lo = (int)src[w * 2];
                    int hi = (int)src[w * 2 + 1];
                    int sv = (lo != 0) ? lo : ((hi != 0) ? hi : 0);

                    if (lo != 0)
                        nz_even++;
                    if (hi != 0)
                        nz_odd++;

                    if (rx_have_last && sv == rx_last) {
                        if (!rx_skipped) {
                            rx_skipped = 1;
                            continue;
                        }
                    }
                    rx_skipped = 0;

                    if (rx_pend_valid) {
                        if (got + 4 > want)
                            break;
                        *(short *)(dst + got) = (short)rx_pend;
                        *(short *)(dst + got + 2) = (short)((rx_pend + sv) >> 1);
                        got += 4;
                        out_samp += 2;
                    }
                    rx_pend = sv;
                    rx_pend_valid = 1;
                    rx_last = sv;
                    rx_have_last = 1;
                    conv_samp++;
                }
"""
sub1(BOLD, BNEW, 'normalize-loop')

# --- 4) 增加一行 [RXNORM] 观测输出 ---------------------------------------
COLD = """           poll_wait_ms);
    fflush(stdout);
    return got;
}
"""
CNEW = """           poll_wait_ms);
    printf("[RXNORM] out=%d samples  raw_words=%d (kept) "
           "nzLow=%d nzHigh=%d\\n", out_samp, conv_samp, nz_even, nz_odd);
    fflush(stdout);
    return got;
}
"""
sub1(COLD, CNEW, 'rxnrom-print')

io.open(F, 'w', encoding='utf-8').write(s)
print('PATCHED %s' % F)
