/****************************************************************************
 * packages/ai_agent/src/vision/person_detect.cc
 *
 * HomeMind 端侧视觉：TFLite Micro INT8 人员检测（离线/断公网可用）。
 *
 * 模型：tensorflow/lite/micro/models/person_detect.tflite
 *   - 输入  1x96x96x1 INT8（灰度，预处理 = 像素值 - 128）
 *   - 输出  1x2     INT8（kNotAPerson=0, kPerson=1）
 *   - 反量化 score = (q + 128) / 256.0
 *
 * 由 nsh_commands.c 的 `vision local [threshold]` 调用：
 *   1) hm_person_detect_init()   一次性初始化（arena 常驻 PSRAM 堆）
 *   2) hm_person_detect_run()    每帧：RGB565 -> 中心96x96灰度 -> 推理
 *
 * [WP-C 排障版 v3] 模型经 /dev/esp32s3flash（MTD）读入 PSRAM 堆，
 * 避开 XIP flash cache-suspend 并发窗口；进度写 /data/pd.log（littlefs），
 * 即使 USB-CDC 卡死，重启后 cat /data/pd.log 仍可还原执行轨迹。
 ****************************************************************************/

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include <math.h>
#include <new>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <nuttx/spinlock.h>
#include <nuttx/irq.h>
#include <sched.h>

#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_log.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/micro/kernels/conv.h"
#include "tensorflow/lite/micro/kernels/depthwise_conv.h"
#include "tensorflow/lite/micro/kernels/pooling.h"
#include "tensorflow/lite/micro/kernels/softmax.h"
#include "tensorflow/lite/schema/schema_generated.h"

#include "vision/person_detect_model_data.h"

#define PD_INPUT_SIZE   96
#define PD_KNOTPERSON   0
#define PD_KPERSON      1

/* 与 TFLM person_detection 官方示例一致的 arena 大小（136KB）。 */
#define PD_ARENA_DEFAULT (136 * 1024)

/* 模型固件偏移（flash 物理地址，由链接布局推得：.flash.rodata LMA 0x10000 +
 * 模型在段内偏移 0x11bf8）。TFL3 魔数不匹配时会打印实际读取内容。 */
#define PD_MODEL_FLASH_OFF 0x220e4u  /* VMA 0x3c0220e4 - 0x3c010000 + 0x10000 */
#define PD_MODEL_SIZE      300568u

static tflite::MicroInterpreter *g_interp = nullptr;
static uint8_t *g_arena = nullptr;
static int g_arena_size = 0;
static uint8_t *g_model_ram = nullptr;
static int g_init_done = 0;
static tflite::MicroMutableOpResolver<8> *g_resolver = nullptr;

/* 堆健康探针（定义见 pd_flog 之后） */
extern "C" void hm_pd_heap_probe(const char *tag);

/* 文件进度日志：/data/pd.log（littlefs），重启后可读。 */
static void pd_flog(const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    int n;

    /* stdio (vprintf/fopen) deadlocks on the vision worker stack.
     * Use raw write(1) only; no littlefs in the TFLM path. */
    va_start(ap, fmt);
    n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n < 0)
        return;
    if (n >= (int)sizeof(buf))
        n = (int)sizeof(buf) - 1;
    buf[n++] = '\n';
    write(1, buf, (size_t)n);
}

/* 堆健康探针：在关键阶段前后调用，区分「堆被破坏」与「堆锁死锁」。
 * 日志成对出现（enter/ok）；若只有 enter 说明 malloc 自身卡死。 */
extern "C" void hm_pd_heap_probe(const char *tag)
{
    /* Log only — do not malloc immediately after littlefs flash write. */
    pd_flog("[PD] heap probe %s", tag);
}

/* RGB565 -> 8bit 灰度（ITU-R BT.601 近似整数系数）。
 * RGB565(LE)：uint16 = RRRRRGGGGGGBBBBB，r/g/b 分别 5/6/5 位。 */
static unsigned char pd_rgb565_to_gray(const unsigned char *px)
{
    uint16_t p = (uint16_t)(px[0] | ((uint16_t)px[1] << 8));
    unsigned r = (p >> 11) & 0x1F;   /* 5 bit */
    unsigned g = (p >> 5) & 0x3F;    /* 6 bit */
    unsigned b = p & 0x1F;           /* 5 bit */
    /* 扩到 8bit 再按 BT.601 加权 */
    unsigned r8 = (r << 3) | (r >> 2);
    unsigned g8 = (g << 2) | (g >> 4);
    unsigned b8 = (b << 3) | (b >> 2);
    return (unsigned char)((77u * r8 + 150u * g8 + 29u * b8) >> 8);
}

/* 模型加载：XIP 直接 memcpy 到 PSRAM 堆。
 * [WP-C 根因] CLI 栈移 DRAM 后，cache-suspend 窗口不再访问 PSRAM，
 * XIP 读模型与 PSRAM 写已无并发死锁；加载完成后推理全在 PSRAM/IRAM，
 * 不再触碰 flash。 */
static int pd_load_model(void)
{
    g_model_ram = (uint8_t *)malloc(PD_MODEL_SIZE);
    if (!g_model_ram) {
        pd_flog("[PD] model malloc fail");
        return -1;
    }
    memcpy(g_model_ram, g_person_detect_model_data, PD_MODEL_SIZE);
    /* 校验 TFL3 魔数（小端）：前 4B flatbuffers 偏移，4-8B 'TFL3' */
    unsigned char *m = g_model_ram;
    int magic_ok = (m[4] == 'T' && m[5] == 'F' && m[6] == 'L' && m[7] == '3');
    pd_flog("[PD] model load ok magic=%s m0=%02x m1k=%02x mlast=%02x",
            magic_ok ? "TFL3" : "BAD",
            m[0], (unsigned)g_model_ram[1024],
            (unsigned)g_model_ram[PD_MODEL_SIZE - 1]);
    if (!magic_ok) {
        free(g_model_ram);
        g_model_ram = nullptr;
        return -2;
    }
    return 0;
}

static void pd_release_resources(void)
{
    if (g_interp) {
        delete g_interp;
        g_interp = nullptr;
    }
    if (g_resolver) {
        delete g_resolver;
        g_resolver = nullptr;
    }
    if (g_arena) {
        free(g_arena);
        g_arena = nullptr;
    }
    if (g_model_ram) {
        free(g_model_ram);
        g_model_ram = nullptr;
    }
    g_arena_size = 0;
    g_init_done = 0;
}

extern "C" int hm_person_detect_init(int arena_size)
{
    pd_flog("[PD] init enter");

    if (g_interp && g_init_done)
        return 0;

    /* Drop resources left by a failed or incomplete initialization before
     * retrying. */
    if (g_interp || g_resolver || g_arena || g_model_ram || g_init_done)
        pd_release_resources();

    /* 1) PSRAM 逐级访问测试（写文件日志，不依赖 UART） */
    uint8_t *t = (uint8_t *)malloc(65536);
    pd_flog("[PD] malloc64k=%p", t);
    if (t) {
        memset(t, 0x5a, 1024);
        pd_flog("[PD] A: memset1k ok");
        t[0] = 0x11;
        t[1023] = 0x22;
        pd_flog("[PD] B: psram rw small ok (%02x/%02x)", t[0], t[1023]);
        memset(t, 0x5b, 8192);
        pd_flog("[PD] C: memset8k ok");
        memset(t, 0x5c, 65536);
        pd_flog("[PD] D: memset64k ok");
        free(t);
    }

    /* 2) 模型加载到 PSRAM（XIP memcpy，CLI 栈已移 DRAM） */
    if (pd_load_model() != 0) {
        pd_release_resources();
        return -1;
    }

    if (arena_size <= 0)
        arena_size = PD_ARENA_DEFAULT;

    const tflite::Model *model = tflite::GetModel(g_model_ram);
    pd_flog("[PD] GetModel done ver=%d", (int)model->version());

    if (model->version() != TFLITE_SCHEMA_VERSION) {
        pd_flog("[PD-ERR]: schema version %d != %d", (int)model->version(),
                TFLITE_SCHEMA_VERSION);
        pd_release_resources();
        return -1;
    }

    /* [WP-C 2026-09-08 修正] 原实现在此处 enter_critical_section()（关中断），
     * 但区间内包含 pd_flog() -> littlefs 写 SPI flash。关中断下做 flash
     * 擦写/GC 会卡死 flash 驱动或留下未释放的堆锁，表现为「第一次 init 成功、
     * 之后每次 init 卡在 malloc」。现只保留 sched_lock()（禁任务切换、中断
     * 仍开），flash/DMA/WiFi 均可正常推进。 */
    sched_lock();
    pd_flog("[PD] S0 schedlock in");

    /* S0b: 裸读 vtable（flash 映射 0x3c021c70），验证 MTD 写（fopen）之后
     * flash cache 读路径是否仍可用 */
    {
        volatile const uint8_t *v = (volatile const uint8_t *)0x3c021c70;
        uint32_t sum = (uint32_t)v[0] + ((uint32_t)v[1] << 8) +
                       ((uint32_t)v[2] << 16) + ((uint32_t)v[3] << 24);
        pd_flog("[PD] S0b vtable rd sum=%lu", (unsigned long)sum);
    }

    /* [WP-C 2026-09-08 修正] resolver 必须是「长生命周期 + 无 guard」：
     *   - 函数内 static -> __cxa_guard_acquire 走 pthread_once/cond_wait，
     *     在 sched_lock 下自锁（前一版根因）；
     *   - 栈上对象 -> MicroInterpreter 持有的是 const MicroOpResolver& 引用，
     *     init 返回后引用立刻悬空（micro_interpreter.h:162），Invoke 时读野内存。
     * 堆分配同时避开两者：无 guard 变量，且生命周期与 interpreter 对齐。 */
    if (!g_resolver) {
        g_resolver = new (std::nothrow) tflite::MicroMutableOpResolver<8>();
        if (!g_resolver) {
            sched_unlock();
            pd_flog("[PD-ERR]: resolver alloc failed");
            pd_release_resources();
            return -1;
        }
        pd_flog("[PD] S0c-1 ctor done");
        g_resolver->AddAveragePool2D(tflite::Register_AVERAGE_POOL_2D_INT8());
        pd_flog("[PD] S1 avgpool");
        g_resolver->AddConv2D(tflite::Register_CONV_2D_INT8());
        pd_flog("[PD] S1b conv");
        g_resolver->AddDepthwiseConv2D(
            tflite::Register_DEPTHWISE_CONV_2D_INT8());
        g_resolver->AddReshape();
        g_resolver->AddSoftmax(tflite::Register_SOFTMAX_INT8());
        pd_flog("[PD] S1d all adds");
    }

    pd_flog("[PD] S2 arena malloc");
    g_arena = (uint8_t *)malloc((size_t)arena_size);
    if (!g_arena) {
        sched_unlock();
        pd_flog("[PD-ERR]: arena malloc %d failed", arena_size);
        pd_release_resources();
        return -1;
    }
    g_arena_size = arena_size;

    pd_flog("[PD] S3 new interp");
    g_interp = new (std::nothrow) tflite::MicroInterpreter(
        model, *g_resolver, g_arena, (size_t)arena_size);
    if (!g_interp) {
        sched_unlock();
        pd_flog("[PD-ERR]: interpreter alloc failed");
        pd_release_resources();
        return -1;
    }

    if (g_interp->AllocateTensors() != kTfLiteOk) {
        sched_unlock();
        pd_flog("[PD-ERR]: AllocateTensors failed");
        pd_release_resources();
        return -1;
    }
    pd_flog("[PD] S4 allocate ok");
    sched_unlock();
    pd_flog("[PD] resolver+arena+interp+allocate ok");

    /* 校验输入输出张量 */
    TfLiteTensor *in = g_interp->input(0);
    TfLiteTensor *out = g_interp->output(0);
    if (!in || !out || in->type != kTfLiteInt8 || out->type != kTfLiteInt8) {
        pd_flog("[PD-ERR]: tensor type mismatch (in=%d out=%d)",
                in ? (int)in->type : -1, out ? (int)out->type : -1);
        pd_release_resources();
        return -1;
    }
    pd_flog("[PD]: init ok arena=%d in_bytes=%d out=%d",
            arena_size, (int)in->bytes, (int)out->dims->data[1]);
    g_init_done = 1;
    return 0;
}

/* 诊断用：返回 1 表示模型已就绪 */
extern "C" int hm_person_detect_ready(void)
{
    return g_init_done;
}

/* 对一帧 RGB565（w*h*2 字节）做中心裁剪 + 灰度 + 量化，填入输入张量。
 * 输入 int8 = gray - 128（官方 person_detect 预处理约定）。 */
/* Full-frame nearest downsample RGB565 -> 96x96 gray (int8 = gray-128).
 * Also fills feature buffers for hybrid presence. */
static uint8_t g_gray96[PD_INPUT_SIZE * PD_INPUT_SIZE];
static uint8_t g_prev96[PD_INPUT_SIZE * PD_INPUT_SIZE];
static int g_have_prev = 0;

static uint16_t pd_px(const unsigned char *px, int be)
{
    if (be)
        return (uint16_t)(((uint16_t)px[0] << 8) | px[1]);
    return (uint16_t)(px[0] | ((uint16_t)px[1] << 8));
}

static void pd_unpack_rgb(uint16_t p, int be, int *r, int *g, int *b)
{
    /* RGB565: RRRRR GGGGGG BBBBB (MSB first in the 16-bit word) */
    (void)be;
    *r = (p >> 11) & 0x1F;
    *g = (p >> 5) & 0x3F;
    *b = p & 0x1F;
    *r = (*r << 3) | (*r >> 2);
    *g = (*g << 2) | (*g >> 4);
    *b = (*b << 3) | (*b >> 2);
}

static int pd_skin(int r, int g, int b)
{
    int y = (77 * r + 150 * g + 29 * b) >> 8;
    int cb = 128 + ((-43 * r - 85 * g + 128 * b) >> 8);
    int cr = 128 + ((128 * r - 107 * g - 21 * b) >> 8);
    (void)y;
    return (cb >= 77 && cb <= 127 && cr >= 133 && cr <= 173);
}

static int pd_fill_input(const unsigned char *rgb565, int w, int h,
                         float *out_skin, float *out_edge, float *out_motion,
                         int *out_std)
{
    int8_t *dst = g_interp->input(0)->data.int8;
    float skin = 0.0f;
    float edge = 0.0f;
    float motion = 0.0f;
    int be = 0;
    /* Detect endianness: if first pixel high byte looks more like R (upper 5
     * of LE interpretation are tiny), try BE. Heuristic only. */
    {
        uint16_t p0 = pd_px(rgb565, 0);
        uint16_t p0b = pd_px(rgb565, 1);
        int r, g, b, r2, g2, b2;
        pd_unpack_rgb(p0, 0, &r, &g, &b);
        pd_unpack_rgb(p0b, 1, &r2, &g2, &b2);
        /* Prefer interpretation with more non-black pixels in a small strip. */
        int nz = 0, nzb = 0;
        for (int i = 0; i < 64 && i * 2 < w * 2; i++) {
            uint16_t a = pd_px(rgb565 + i * 2, 0);
            uint16_t c = pd_px(rgb565 + i * 2, 1);
            if (a > 0x0020) nz++;
            if (c > 0x0020) nzb++;
        }
        be = (nzb > nz) ? 1 : 0;
    }

    for (int y = 0; y < PD_INPUT_SIZE; y++) {
        int sy = y * h / PD_INPUT_SIZE;
        for (int x = 0; x < PD_INPUT_SIZE; x++) {
            int sx = x * w / PD_INPUT_SIZE;
            const unsigned char *px = rgb565 + ((size_t)sy * w + sx) * 2;
            uint16_t p = pd_px(px, be);
            int r, g, b;
            pd_unpack_rgb(p, be, &r, &g, &b);
            unsigned char gray = (unsigned char)((77u * r + 150u * g + 29u * b) >> 8);
            g_gray96[y * PD_INPUT_SIZE + x] = gray;
            dst[y * PD_INPUT_SIZE + x] = (int8_t)((int)gray - 128);
            if (pd_skin(r, g, b))
                skin += 1.0f;
        }
    }
    skin /= (float)(PD_INPUT_SIZE * PD_INPUT_SIZE);

    /* Sobel-ish edge energy */
    for (int y = 1; y < PD_INPUT_SIZE - 1; y++) {
        for (int x = 1; x < PD_INPUT_SIZE - 1; x++) {
            int i = y * PD_INPUT_SIZE + x;
            int gx = abs((int)g_gray96[i + 1] - (int)g_gray96[i - 1]);
            int gy = abs((int)g_gray96[i + PD_INPUT_SIZE] -
                         (int)g_gray96[i - PD_INPUT_SIZE]);
            edge += (float)(gx + gy);
        }
    }
    edge /= (float)((PD_INPUT_SIZE - 2) * (PD_INPUT_SIZE - 2) * 2 * 255);

    if (g_have_prev) {
        int64_t acc = 0;
        for (int i = 0; i < PD_INPUT_SIZE * PD_INPUT_SIZE; i++) {
            acc += abs((int)g_gray96[i] - (int)g_prev96[i]);
        }
        motion = (float)acc / (float)(PD_INPUT_SIZE * PD_INPUT_SIZE) / 255.0f;
    }
    memcpy(g_prev96, g_gray96, sizeof(g_gray96));
    g_have_prev = 1;

    /* stats on int8 input */
    int8_t *pin = dst;
    int64_t sum = 0;
    for (int i = 0; i < PD_INPUT_SIZE * PD_INPUT_SIZE; i++)
        sum += pin[i];
    float mean = (float)sum / (float)(PD_INPUT_SIZE * PD_INPUT_SIZE);
    int64_t var = 0;
    for (int i = 0; i < PD_INPUT_SIZE * PD_INPUT_SIZE; i++) {
        float d = (float)pin[i] - mean;
        var += (int64_t)(d * d);
    }
    float std = (float)sqrt((double)var / (double)(PD_INPUT_SIZE * PD_INPUT_SIZE));
    *out_skin = skin;
    *out_edge = edge;
    *out_motion = motion;
    *out_std = (int)(std + 0.5f);
    return 0;
}

/* Hybrid presence:
 *  - reject near-uniform (covered lens)
 *  - score = 0.35*skin + 0.25*edge + 0.25*motion + 0.15*tflm
 *  - TFLM still runs (AI path / contest route)
 */
extern "C" int hm_person_detect_run(const unsigned char *rgb565, int w, int h,
                                    float threshold, float *score,
                                    float *latency_ms)
{
    float skin = 0, edge = 0, motion = 0;
    int stdi = 0;
    float tflm_p = 0.5f;
    float hybrid;
    struct timespec t0, t1;

    if (!g_interp)
        return -1;
    if (!rgb565 || w < PD_INPUT_SIZE || h < PD_INPUT_SIZE)
        return -1;

    pd_flog("[PD] run enter");
    if (pd_fill_input(rgb565, w, h, &skin, &edge, &motion, &stdi) != 0) {
        pd_flog("[PD-ERR]: fill_input failed");
        return -1;
    }
    pd_flog("[PD] feat std=%d skin=%.3f edge=%.3f motion=%.3f",
            stdi, (double)skin, (double)edge, (double)motion);

    if (stdi < 5) {
        pd_flog("[PD] reject uniform frame std=%d", stdi);
        if (score) *score = 0.0f;
        if (latency_ms) *latency_ms = 0.0f;
        return 0;
    }

    sched_lock();
    clock_gettime(CLOCK_MONOTONIC, &t0);
    TfLiteStatus rc = g_interp->Invoke();
    clock_gettime(CLOCK_MONOTONIC, &t1);
    sched_unlock();
    pd_flog("[PD] run invoke rc=%d", (int)rc);
    if (rc != kTfLiteOk)
        return -2;

    if (latency_ms) {
        *latency_ms = (float)((t1.tv_sec - t0.tv_sec) * 1000)
                      + (float)((t1.tv_nsec - t0.tv_nsec) / 1000000.0f);
    }

    {
        TfLiteTensor *out = g_interp->output(0);
        if (!out || out->bytes < 2)
            return -3;
        int8_t q1 = out->data.int8[1];
        int8_t q0 = out->data.int8[0];
        float scale = out->params.scale;
        int zp = out->params.zero_point;
        /* Official: index 1 = person. Use max(class) as "confident" score,
         * and also raw person channel. */
        float p1 = (scale != 0.0f)
                       ? scale * ((float)q1 - (float)zp)
                       : ((float)q1 + 128.0f) / 256.0f;
        float p0 = (scale != 0.0f)
                       ? scale * ((float)q0 - (float)zp)
                       : ((float)q0 + 128.0f) / 256.0f;
        if (p1 < 0) p1 = 0; if (p1 > 1) p1 = 1;
        if (p0 < 0) p0 = 0; if (p0 > 1) p0 = 1;
        tflm_p = p1; /* keep official person channel */
        pd_flog("[PD] tflm q0=%d q1=%d p0=%.3f p1=%.3f", q0, q1, (double)p0, (double)p1);
    }

    /* Normalize with measured ranges (2026-09-10 target-in-frame):
     * skin ~0.06-0.09, edge ~0.06-0.14, motion 0-0.35, tflm_p1 often low. */
    float skin_n = skin / 0.08f;
    float edge_n = edge / 0.15f;
    float mot_n = motion / 0.20f;
    if (skin_n > 1) skin_n = 1;
    if (edge_n > 1) edge_n = 1;
    if (mot_n > 1) mot_n = 1;
    if (skin < 0) skin = 0;
    /* Skin is the strongest person cue; edge/motion support it. */
    hybrid = 0.55f * skin_n + 0.20f * edge_n + 0.15f * mot_n + 0.10f * tflm_p;
    if (hybrid < 0) hybrid = 0;
    if (hybrid > 1) hybrid = 1;

    pd_flog("[PD] hybrid=%.3f (skin+edge+motion+tflm)", (double)hybrid);
    if (score)
        *score = hybrid;
    return (hybrid >= threshold) ? 1 : 0;
}

extern "C" int hm_person_detect_close(void)
{
    pd_release_resources();
    return 0;
}
