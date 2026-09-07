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
 ****************************************************************************/

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <time.h>

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

static tflite::MicroInterpreter *g_interp = nullptr;
static uint8_t *g_arena = nullptr;
static int g_arena_size = 0;

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

extern "C" int hm_person_detect_init(int arena_size)
{
    if (g_interp)
        return 0;

    if (arena_size <= 0)
        arena_size = PD_ARENA_DEFAULT;

    const tflite::Model *model = tflite::GetModel(g_person_detect_model_data);
    if (model->version() != TFLITE_SCHEMA_VERSION) {
        MicroPrintf("[PD-ERR]: schema version %d != %d", model->version(),
                    TFLITE_SCHEMA_VERSION);
        return -1;
    }

    static tflite::MicroMutableOpResolver<8> resolver;
    resolver.AddAveragePool2D(tflite::Register_AVERAGE_POOL_2D_INT8());
    resolver.AddConv2D(tflite::Register_CONV_2D_INT8());
    resolver.AddDepthwiseConv2D(tflite::Register_DEPTHWISE_CONV_2D_INT8());
    resolver.AddReshape();
    resolver.AddSoftmax(tflite::Register_SOFTMAX_INT8());

    g_arena = (uint8_t *)malloc((size_t)arena_size);
    if (!g_arena) {
        MicroPrintf("[PD-ERR]: arena malloc %d failed", arena_size);
        return -1;
    }
    g_arena_size = arena_size;

    g_interp = new (std::nothrow) tflite::MicroInterpreter(
        model, resolver, g_arena, (size_t)arena_size);
    if (!g_interp) {
        MicroPrintf("[PD-ERR]: interpreter alloc failed");
        free(g_arena);
        g_arena = nullptr;
        return -1;
    }

    if (g_interp->AllocateTensors() != kTfLiteOk) {
        MicroPrintf("[PD-ERR]: AllocateTensors failed");
        delete g_interp;
        g_interp = nullptr;
        free(g_arena);
        g_arena = nullptr;
        return -1;
    }

    /* 校验输入输出张量 */
    TfLiteTensor *in = g_interp->input(0);
    TfLiteTensor *out = g_interp->output(0);
    if (!in || !out || in->type != kTfLiteInt8 || out->type != kTfLiteInt8) {
        MicroPrintf("[PD-ERR]: tensor type mismatch (in=%d out=%d)",
                    in ? (int)in->type : -1, out ? (int)out->type : -1);
        return -1;
    }
    MicroPrintf("[PD]: init ok arena=%d in_bytes=%d out=%d",
                arena_size, (int)in->bytes, (int)out->dims->data[1]);
    return 0;
}

/* 对一帧 RGB565（w*h*2 字节）做中心裁剪 + 灰度 + 量化，填入输入张量。
 * 输入 int8 = gray - 128（官方 person_detect 预处理约定）。 */
static int pd_fill_input(const unsigned char *rgb565, int w, int h)
{
    TfLiteTensor *in = g_interp->input(0);
    if (in->bytes != (size_t)(PD_INPUT_SIZE * PD_INPUT_SIZE)) {
        MicroPrintf("[PD-ERR]: input bytes %d != %d", (int)in->bytes,
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
    if (pd_fill_input(rgb565, w, h) != 0)
        return -1;

    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    TfLiteStatus rc = g_interp->Invoke();
    clock_gettime(CLOCK_MONOTONIC, &t1);

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
    if (g_arena) {
        free(g_arena);
        g_arena = nullptr;
    }
    g_arena_size = 0;
    return 0;
}
