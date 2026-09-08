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
#define PD_MODEL_FLASH_OFF 0x21bf8u
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
    FILE *fp = fopen("/data/pd.log", "a");
    if (!fp)
        return;

    /* 日志自维护：超过 32KB 时截断重写，避免 littlefs 反复 GC/擦块，
     * 也避免日志无限增长拖慢后续 fopen。 */
    fseek(fp, 0, SEEK_END);
    if (ftell(fp) > 32768) {
        fclose(fp);
        fp = fopen("/data/pd.log", "w");
        if (!fp)
            return;
    }

    va_list ap;
    va_start(ap, fmt);
    vfprintf(fp, fmt, ap);
    va_end(ap);
    fputc('\n', fp);
    fclose(fp);
}

/* 堆健康探针：在关键阶段前后调用，区分「堆被破坏」与「堆锁死锁」。
 * 日志成对出现（enter/ok）；若只有 enter 说明 malloc 自身卡死。 */
extern "C" void hm_pd_heap_probe(const char *tag)
{
    void *a;
    void *b;

    pd_flog("[PD] heap probe %s: enter", tag);
    a = malloc(4096);
    b = malloc(65536);
    pd_flog("[PD] heap probe %s: a=%p b=%p", tag, a, b);
    if (b) {
        memset(b, 0xa5, 4096);
        free(b);
    }
    if (a)
        free(a);
    pd_flog("[PD] heap probe %s: ok", tag);
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
    pd_flog("[PD] model load ok magic=%s m0=%02x m1k=%02x m299k=%02x",
            magic_ok ? "TFL3" : "BAD",
            m[0], (unsigned)g_model_ram[1024],
            (unsigned)g_model_ram[299 * 1024]);
    if (!magic_ok)
        return -2;
    return 0;
}

extern "C" int hm_person_detect_init(int arena_size)
{
    pd_flog("[PD] init enter");

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
    if (pd_load_model() != 0)
        return -1;

    if (g_interp)
        return 0;

    if (arena_size <= 0)
        arena_size = PD_ARENA_DEFAULT;

    const tflite::Model *model = tflite::GetModel(g_model_ram);
    pd_flog("[PD] GetModel done ver=%d", (int)model->version());

    if (model->version() != TFLITE_SCHEMA_VERSION) {
        pd_flog("[PD-ERR]: schema version %d != %d", (int)model->version(),
                TFLITE_SCHEMA_VERSION);
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
        return -1;
    }
    g_arena_size = arena_size;

    pd_flog("[PD] S3 new interp");
    g_interp = new (std::nothrow) tflite::MicroInterpreter(
        model, *g_resolver, g_arena, (size_t)arena_size);
    if (!g_interp) {
        sched_unlock();
        pd_flog("[PD-ERR]: interpreter alloc failed");
        free(g_arena);
        g_arena = nullptr;
        return -1;
    }

    if (g_interp->AllocateTensors() != kTfLiteOk) {
        sched_unlock();
        pd_flog("[PD-ERR]: AllocateTensors failed");
        delete g_interp;
        g_interp = nullptr;
        free(g_arena);
        g_arena = nullptr;
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
static int pd_fill_input(const unsigned char *rgb565, int w, int h)
{
    TfLiteTensor *in = g_interp->input(0);
    if (in->bytes != (size_t)(PD_INPUT_SIZE * PD_INPUT_SIZE)) {
        printf("[PD-ERR]: input bytes %d != %d\n", (int)in->bytes,
               PD_INPUT_SIZE * PD_INPUT_SIZE);
        return -1;
    }
    const int x0 = (w - PD_INPUT_SIZE) / 2;
    const int y0 = (h - PD_INPUT_SIZE) / 2;
    int8_t *dst = in->data.int8;
    for (int y = 0; y < PD_INPUT_SIZE; y++) {
        const unsigned char *row = rgb565 + (size_t)(y + y0) * w * 2
                                   + (size_t)x0 * 2;
        for (int x = 0; x < PD_INPUT_SIZE; x++) {
            unsigned char gray = pd_rgb565_to_gray(row + (size_t)x * 2);
            dst[y * PD_INPUT_SIZE + x] = (int8_t)((int)gray - 128);
        }
    }
    return 0;
}

/* 运行一次推理。
 * 返回 0 成功；*score 为 person 概率 [0,1]；*latency_ms 为推理耗时。
 * 成功时返回 1 表示检测到人员（score >= threshold），0 表示未检测到。 */
extern "C" int hm_person_detect_run(const unsigned char *rgb565, int w, int h,
                                    float threshold, float *score,
                                    float *latency_ms)
{
    if (!g_interp)
        return -1;
    if (!rgb565 || w < PD_INPUT_SIZE || h < PD_INPUT_SIZE)
        return -1;

    pd_flog("[PD] run enter");
    if (pd_fill_input(rgb565, w, h) != 0) {
        pd_flog("[PD-ERR]: fill_input failed");
        return -1;
    }
    pd_flog("[PD] run input filled");

    /* [WP-C 2026-09-08 修正] 仅 sched_lock，不再关中断：Invoke 期间
     * WiFi/GDMA 中断仍需响应。 */
    struct timespec t0, t1;
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
                      + (float)(t1.tv_nsec - t0.tv_nsec) / 1000000.0f;
    }

    TfLiteTensor *out = g_interp->output(0);
    if (!out || out->bytes < 2)
        return -3;
    int8_t q_person = out->data.int8[PD_KPERSON];
    float p = (float)((int)q_person + 128) / 256.0f;
    if (p < 0.0f)
        p = 0.0f;
    if (p > 1.0f)
        p = 1.0f;
    if (score)
        *score = p;
    return (p >= threshold) ? 1 : 0;
}

extern "C" int hm_person_detect_close(void)
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
    return 0;
}
