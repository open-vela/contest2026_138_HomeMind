/*
 * Copyright (C) 2026 Xiaomi Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * This file contains code derived from MimiClaw (https://github.com/memovai/mimiclaw)
 * Copyright (c) 2026 Ziboyan Wang, licensed under the MIT License.
 * See NOTICE file for the original MIT License terms.
 */

#include "channels/nsh_commands.h"
#include "channels/cmd_channel.h"
#include "channels/cmd_llm.h"
#include "channels/cmd_voice.h"
#include "core/message_bus.h"
#include "infra/config_store.h"
#include "infra/cron_service.h"
#include "infra/heartbeat.h"
#include "llm/llm_proxy.h"
#ifdef CONFIG_AI_AGENT_MCP
#include "tools/mcp_client.h"
#endif
#include "core/memory_store.h"
#include "core/session_mgr.h"
#include "infra/network_manager.h"
#include "infra/http_proxy.h"
#include "infra/vela_tls.h"
#include "tools/tool_get_time.h"
#include "tools/tool_proxyquickapp.h"
#include "channels/mqtt_channel.h"
#include "tools/tool_registry.h"
#include "tools/tool_web_search.h"
#include "agent_compat.h"
#include "ui/hm_lcd_display.h"
#include "agent_config.h"

#if AGENT_SKILL_SYNC_ENABLED
#include "tools/skill_sync.h"
#endif

#ifdef CONFIG_AI_AGENT_LVGL_UI
#include "ui/lvgl_ui_channel.h"
#endif

#ifdef CONFIG_AI_AGENT_TEST
#include "integs/test_vision_integ.h"
#include "core/arena_alloc.h"
#endif

#include <malloc.h>
#include <math.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <nuttx/ioexpander/gpio.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include <nuttx/audio/audio.h>
#include <nuttx/video/video.h>

#ifdef CONFIG_BOARDCTL_RESET
#include <sys/boardctl.h>
#endif

static const char* TAG = "cli";

#define MAX_ARGS 8
#define LINE_LEN 256

/* Temporary TLS transport diagnostic implemented in vela_tls.c. */
int vela_tls_diag_replay(const char* host, const char* port);

/* ── Helpers ──────────────────────────────────────────────────── */

static int tokenise(char* line, char** argv, int max_argc)
{
    int argc = 0;
    char* tok = strtok(line, " \t\r\n");
    while (tok && argc < max_argc) {
        argv[argc++] = tok;
        tok = strtok(NULL, " \t\r\n");
    }
    return argc;
}

static void cmd_help(void)
{
    printf(
        "Available commands:\n"
        "  net_status           - Show network connection status\n"
        "  net_test [host] [port] - Test HTTPS (default: www.baidu.com:443)\n"
        "  tcp_probe <host> <port> <bytes> - Raw TCP echo length probe (1..1400)\n"
        "  tls_replay <host> <port> - Replay last ClientHello after TLS cleanup\n"
        "  set_feishu_app <app_id> <app_secret>  - Set Feishu app credentials\n"
        "  set_feishu_user_token <token>  - Set Feishu user_access_token for doc APIs\n"
        "  set_llm <preset|host> [model] [key] - Switch LLM backend (kimi/qwen/deepseek/glm/openai)\n"
        "  set_vision_llm <preset|host> [model] [key] - Set independent vision model\n"
        "  list_models [--free] [keyword] - List available models (openrouter)\n"
        "  memory_read          - Read MEMORY.md\n"
        "  memory_write <text>  - Write MEMORY.md (quote text)\n"
        "  session_list         - List sessions\n"
        "  session_clear <id>   - Clear a session\n"
        "  session_clear_all    - Clear all sessions\n"
        "  heap_info            - Show memory usage\n"
        "  set_proxy <host> <port> - Set HTTP proxy\n"
        "  clear_proxy          - Remove proxy config\n"
        "  set_wifi <ssid> <pw> - Connect to WiFi (real hardware; saved for reboot)\n"
        "  wifi_reconnect       - Re-join WiFi using saved credentials\n"
        "  set_search_key <key> - Set SerpAPI (Google) key\n"
        "  set_exa_key <key>    - Set Exa AI search key\n"
        "  set_tavily_key <key> - Set Tavily search key\n"
        "  set_news_key <key>   - Set NewsAPI key\n"
        "  set_tavily_key <key> - Set Tavily AI search key\n"
        "  config_show          - Show current configuration\n"
        "  config_reset         - Clear all runtime config\n"
        "  ask <text>           - Chat with AI Agent\n"
        "  heartbeat_trigger    - Manually trigger heartbeat check\n"
        "  cron_start           - Start cron scheduler\n"
#ifdef CONFIG_AI_AGENT_NODE
        "  node_list            - List connected remote Nodes\n"
        "  set_gateway <host> [port] [token] - Set OpenClaw Gateway\n"
        "  node_start          - Connect to OpenClaw Gateway as Node\n"
        "  node_stop           - Disconnect from OpenClaw Gateway\n"
#endif
        "  restart              - Restart the device\n"
        "  quit                 - Exit agent\n"
        "  set_mqtt <broker> [client_id] - Set MQTT broker (host:port)\n"
        "  set_volc_key <api_key>       - Set Doubao voice API key\n"
        "  set_volc_asr <id> <tok> <cluster> - Set ASR credentials\n"
        "  set_volc_speaker <id>  - Set TTS voice (e.g. zh_female_cancan)\n"
        "  voice_start            - Start voice channel\n"
        "  voice_stop             - Stop voice channel\n"
        "  voice_test_tts <text> [out.pcm] - Test TTS synthesis\n"
        "  voice_test_asr <file>  - Test ASR recognition\n"
        "  media_probe [jpeg]   - Probe OV2640 RGB565 frame + I2S mic (jpeg: only if sensor supports it)\n"
        "  audio_stream [sec]     - Continuous PCM capture via audio_capture API\n"
        "  i2sknob [half bitsmod chanbits ws totchan dbg] - I2S RX 寄存器旋钮(-1=原行为)\n"
        "  wake_loop [sec] [thr] [hold] - Energy VAD wake + LED blink\n"
        "  wake_kws [thr] - Hello openvela KWS score + LED\n"
        "  kws_listen start|stop|status [thr] [vad_thr] - Continuous offline KWS\n"
        "  kws_listen voice on|off - 唤醒后是否自动进入语音闭环（默认 on）\n"
        "  intent_send <text...> - MQTT intent to home agent\n"
        "  vision_loop start|stop [sec] - Periodic vision tick\n"
        "  wake_rec pos|neg <i> [sec] - Record KWS sample to /data\n"
        "  kws_dump pos|neg <i> - Dump sample as base64\n"
        "  set_voice_tts <name>   - Switch TTS backend\n"
        "  set_voice_asr <name>   - Switch ASR backend\n"
        "  set_weixin_token <tok> - Set WeChat bot token\n"
        "  weixin_login           - QR code login to WeChat\n"
        "  router_status          - Show LLM router status\n"
        "  router_set <preset> <key> - Add LLM backend (deepseek/kimi/qwen/openai...)\n"
        "  router_model <idx> <model> - Change model for a backend\n"
        "  router_profile <p>     - Set routing profile (eco/auto/premium)\n"
        "  router_clear [idx]     - Clear router backends\n"
        "  launch_app <pkg>     - Test launch a QuickApp by package name\n"
        "  exit_app             - Exit current QuickApp and go home\n"
        "  install_skill <name> <url|-> - Install skill from URL or stdin\n"
        "  skill_write_begin <name> - Begin bounded serial Skill import\n"
        "  skill_write_hex <name> <hex> - Append one Skill data chunk\n"
        "  skill_write_commit <name> - Atomically activate imported Skill\n"
#if AGENT_SKILL_SYNC_ENABLED
        "  skill_sync            - Sync skills from Feishu Bitable\n"
#endif
#ifdef CONFIG_AI_AGENT_MCP
        "  mcp_add <name> <url> [token] - Add remote MCP server\n"
        "  mcp_remove <name>     - Remove remote MCP server\n"
        "  mcp_discover          - Discover tools from remote servers\n"
        "  mcp_status            - Show MCP client status\n"
        "  mcp_tools             - List remote MCP tools\n"
#endif
#ifdef CONFIG_AI_AGENT_LVGL_UI
        "  show_chat            - Show chat UI on screen\n"
#endif
#ifdef CONFIG_AI_AGENT_NET_RPMSG
        "  net_diag [ping|http] <target>  Network diagnostics\n"
        "  net_reconnect                  Reconnect network\n"
        "  set_netproxy <mode> [cpu]      Set network proxy mode\n"
        "  set_dns <primary> [secondary]  Set DNS servers\n"
#endif
#ifdef CONFIG_AI_AGENT_TEST
        "  claw_test [V-XX]    - Run vision integration tests\n"
#endif
#ifdef CONFIG_AI_AGENT_BLE_GATT
        "  ble_gatt_test [init|status|send|deinit] - BLE GATT test\n"
#endif
        "  help                 - This message\n");
}

/* ── Command implementations ──────────────────────────────────── */

static unsigned long media_checksum(const unsigned char* data, size_t len)
{
    unsigned long checksum = 2166136261UL;
    size_t i;

    for (i = 0; i < len; i++) {
        checksum ^= data[i];
        checksum *= 16777619UL;
    }

    return checksum;
}


/* ── HomeMind media upload（云端转码视觉/语音）────────────────── */

#define HM_MEDIA_FRAME_BYTES (320 * 240 * 2)

static int hm_media_capture_rgb565(unsigned char *out, int out_len)
{
    const char *video_path = "/dev/video0";
    struct v4l2_format fmt;
    struct v4l2_requestbuffers req;
    struct v4l2_buffer buf;
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    struct pollfd pfd;
    int video_fd;
    int streaming = 0;
    int copied = -1;
    int i;
    size_t buf_len = (size_t)out_len + 2048;  /* DMA 越界防护 padding */

    /* [WP-C 2026-09-08] 摄像头缓冲改为「一次分配、永不释放」。
     * 原实现每次 capture 都 memalign/free 三块 ~152KB：若 close() 后 GDMA
     * 仍在写（或写越界），free 之后落到堆上的数据会破坏 kmm 元数据，
     * 表现为下一次 malloc 直接卡死。常驻缓冲彻底消除该类破坏。 */
    static unsigned char *frames[3] = { NULL, NULL, NULL };

    video_fd = open(video_path, O_RDWR | O_NONBLOCK);
    if (video_fd < 0)
        return -1;

    for (i = 0; i < 3; i++)
        if (!frames[i])
            frames[i] = (unsigned char *)memalign(32, buf_len);
    if (!frames[0] || !frames[1] || !frames[2])
        goto out;

    memset(&fmt, 0, sizeof(fmt));
    fmt.type = type;
    fmt.fmt.pix.width = 320;
    fmt.fmt.pix.height = 240;
    fmt.fmt.pix.field = V4L2_FIELD_ANY;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_RGB565;
    if (ioctl(video_fd, VIDIOC_S_FMT, (uintptr_t)&fmt) < 0)
        goto out;

    memset(&req, 0, sizeof(req));
    req.type = type;
    req.memory = V4L2_MEMORY_USERPTR;
    req.count = 3;
    req.mode = V4L2_BUF_MODE_RING;
    if (ioctl(video_fd, VIDIOC_REQBUFS, (uintptr_t)&req) < 0)
        goto out;

    memset(&buf, 0, sizeof(buf));
    for (buf.index = 0; buf.index < 3; buf.index++) {
        buf.type = type;
        buf.memory = V4L2_MEMORY_USERPTR;
        buf.m.userptr = (uintptr_t)frames[buf.index];
        buf.length = buf_len;
        if (ioctl(video_fd, VIDIOC_QBUF, (uintptr_t)&buf) < 0)
            goto out;
    }

    if (ioctl(video_fd, VIDIOC_STREAMON, (uintptr_t)&type) < 0)
        goto out;
    streaming = 1;

    memset(&pfd, 0, sizeof(pfd));
    pfd.fd = video_fd;
    pfd.events = POLLIN;
    if (poll(&pfd, 1, 5000) <= 0)
        goto out;

    memset(&buf, 0, sizeof(buf));
    buf.type = type;
    buf.memory = V4L2_MEMORY_USERPTR;
    if (ioctl(video_fd, VIDIOC_DQBUF, (uintptr_t)&buf) < 0)
        goto out;

    if (buf.bytesused < (unsigned int)out_len) {
        copied = -2;  /* short frame: refuse to hand off partial data */
    } else {
        memcpy(out, (const void *)(uintptr_t)buf.m.userptr, out_len);
        copied = out_len;
    }

out:
    if (streaming)
        ioctl(video_fd, VIDIOC_STREAMOFF, (uintptr_t)&type);
    /* 缓冲常驻，此处不再 free（见上方说明） */
    close(video_fd);
    return copied;
}

static void hm_url_encode(const char *in, char *out, int out_cap)
{
    static const char hex[] = "0123456789ABCDEF";
    int n = 0;

    while (*in && n + 4 <= out_cap - 1) {
        unsigned char c = (unsigned char)*in++;
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' ||
            c == '.' || c == '~') {
            out[n++] = (char)c;
        } else {
            out[n++] = '%';
            out[n++] = hex[c >> 4];
            out[n++] = hex[c & 0x0F];
        }
    }
    out[n] = '\0';
}

static void hm_media_json_str(const char *resp, const char *anchor,
                              char *out, int out_cap)
{
    const char *p;
    int n = 0;

    out[0] = '\0';
    p = strstr(resp, anchor);
    if (!p)
        return;
    p += strlen(anchor);
    while (*p && *p != '"' && n < out_cap - 1) {
        if (*p == '\\' && p[1])
            p++;
        out[n++] = *p++;
    }
    out[n] = '\0';
}

static void cmd_set_media(int argc, char **argv)
{
    if (argc < 4) {
        printf("usage: set_media <host> <port> <token>\n");
        return;
    }
    if (claw_config_set("media_host", argv[1]) != OK ||
        claw_config_set("media_port", argv[2]) != OK ||
        claw_config_set("media_token", argv[3]) != OK) {
        printf("set_media: save failed (kept in RAM)\n");
        return;
    }
    printf("media endpoint saved: %s:%s (token saved)\n", argv[1], argv[2]);
}

#ifdef CONFIG_TFLITEMICRO
/* 端侧 TFLM 人员检测（断公网可用），实现见 src/vision/person_detect.cc */
int hm_person_detect_init(int arena_size);
int hm_person_detect_run(const unsigned char *rgb565, int w, int h,
                         float threshold, float *score, float *latency_ms);
void hm_pd_heap_probe(const char *tag);
int hm_person_detect_ready(void);
#endif

#ifdef CONFIG_TFLITEMICRO
/* [2026-09-10] TFLM hangs on PSRAM CLI stack (pd.log stops at heap-probe
 * malloc). Run init/capture/run on a dedicated 32KB DRAM stack worker.
 * Do NOT move the whole CLI stack into DRAM BSS — that froze media_probe. */
struct pd_job_s {
    float threshold;
    int init_rc;
    int cap_n;
    int run_rc;
    float score;
    float ms;
};

static uint8_t g_pd_stack[32 * 1024] __attribute__((aligned(16)));

static int pd_job_body(struct pd_job_s *job)
{
    static unsigned char *frame = NULL;

    write(1, "[Vision-local]: body A\n", 23);
    hm_pd_heap_probe("pre-init");
    write(1, "[Vision-local]: body B after probe\n", 35);
    job->init_rc = hm_person_detect_init(0);
    hm_pd_heap_probe("post-init");
    if (job->init_rc != 0)
        return -1;

    printf("[Vision-local]: capturing frame...\n");
    fflush(stdout);
    hm_pd_heap_probe("pre-capture");
    if (!frame)
        frame = (unsigned char *)malloc(HM_MEDIA_FRAME_BYTES);
    if (!frame) {
        job->cap_n = -1;
        return -1;
    }
    job->cap_n = hm_media_capture_rgb565(frame, HM_MEDIA_FRAME_BYTES);
    hm_pd_heap_probe("post-capture");
    if (job->cap_n != HM_MEDIA_FRAME_BYTES)
        return -1;

    printf("[Vision-local]: capture ok, running inference...\n");
    fflush(stdout);
    job->run_rc = hm_person_detect_run(frame, 320, 240, job->threshold,
                                       &job->score, &job->ms);
    if (job->run_rc >= 0)
      {
        char ev[160];
        snprintf(ev, sizeof(ev),
                 "{\"type\":\"person_detected\",\"score\":%.3f,"
                 "\"detected\":%d,\"threshold\":%.2f,\"ms\":%.0f}",
                 job->score, job->run_rc ? 1 : 0, job->threshold, job->ms);
        if (mqtt_channel_send("homemind", ev) == 0)
          printf("[Vision-local]: mqtt event published\n");
        else
          printf("[Vision-local]: mqtt event not published (offline)\n");
        fflush(stdout);
      }
    return job->run_rc < 0 ? -1 : 0;
}

static void *pd_worker(void *arg)
{
    write(1, "[Vision-local]: worker enter\n", 29);
    pd_job_body((struct pd_job_s *)arg);
    printf("[Vision-local]: worker leave init_rc=%d cap_n=%d run_rc=%d\n",
           ((struct pd_job_s *)arg)->init_rc,
           ((struct pd_job_s *)arg)->cap_n,
           ((struct pd_job_s *)arg)->run_rc);
    fflush(stdout);
    return NULL;
}
#endif /* CONFIG_TFLITEMICRO */

static void cmd_vision(int argc, char **argv)
{
#ifdef CONFIG_TFLITEMICRO
    /* 端侧离线推理：vision local [threshold] */
    if (argc > 1 && strcmp(argv[1], "local") == 0) {
        struct pd_job_s job;
        pthread_attr_t attr;
        pthread_t tid;
        void *ret = NULL;
        int rc;
        int i;

        memset(&job, 0, sizeof(job));
        job.threshold = 0.35f;
        for (i = 2; i < argc; i++) {
            char *end = NULL;
            float v = strtof(argv[i], &end);
            if (end && *end == '\0' && v > 0.0f && v <= 1.0f)
                job.threshold = v;
        }

        printf("[Vision-local]: worker stack=%p\n", (void *)g_pd_stack);
        fflush(stdout);

        pthread_attr_init(&attr);
        pthread_attr_setstack(&attr, g_pd_stack, sizeof(g_pd_stack));
        rc = pthread_create(&tid, &attr, pd_worker, &job);
        pthread_attr_destroy(&attr);
        printf("[Vision-local]: create rc=%d tid=%d\n", rc, (int)tid);
        fflush(stdout);
        if (rc != 0) {
            printf("[Vision-ERR]: pd worker create rc=%d\n", rc);
            return;
        }
        rc = pthread_join(tid, &ret);
        printf("[Vision-local]: join rc=%d ret=%p\n", rc, ret);
        fflush(stdout);

        if (job.init_rc != 0) {
            printf("[Vision-ERR]: person detect init failed rc=%d\n",
                   job.init_rc);
            return;
        }
        if (job.cap_n != HM_MEDIA_FRAME_BYTES) {
            printf("[Vision-ERR]: capture failed n=%d\n", job.cap_n);
            return;
        }
        if (job.run_rc < 0) {
            printf("[Vision-ERR]: inference failed rc=%d\n", job.run_rc);
            return;
        }
        printf("[Vision-local]: person=%.3f threshold=%.2f %s (latency=%.1fms)\n",
               job.score, job.threshold,
               job.run_rc ? "DETECTED" : "none", job.ms);
        return;
    }
#endif /* CONFIG_TFLITEMICRO */

    char host[64] = "api.hfy-ai.cloud";
    char port[8] = "443";
    char token[128] = "";
    char qraw[160] = "";
    char qenc[480];
    char auth[176];
    char path[768];
    char text[512];
    static unsigned char *frame;
    static char resp[4096];
    const vela_header_t extra[] = {
        { "Content-Type", "application/octet-stream" },
        { "Authorization", auth },
        { NULL, NULL }
    };
    size_t rlen = 0;
    int status;
    int n;
    int i;

    claw_config_get("media_host", host, sizeof(host));
    claw_config_get("media_port", port, sizeof(port));
    claw_config_get("media_token", token, sizeof(token));
    if (!token[0]) {
        printf("[Vision-ERR]: media_token not set (set_media first)\n");
        return;
    }

    for (i = 1; i < argc && strlen(qraw) < sizeof(qraw) - 2; i++) {
        strncat(qraw, argv[i], sizeof(qraw) - strlen(qraw) - 2);
        strncat(qraw, " ", sizeof(qraw) - strlen(qraw) - 2);
    }
    if (!qraw[0])
        strncpy(qraw, "简要描述画面里的内容", sizeof(qraw) - 1);
    hm_url_encode(qraw, qenc, sizeof(qenc));

    frame = malloc(HM_MEDIA_FRAME_BYTES);
    if (!frame) {
        printf("[Vision-ERR]: alloc failed\n");
        return;
    }
    printf("[Vision]: capturing frame...\n");
    n = hm_media_capture_rgb565(frame, HM_MEDIA_FRAME_BYTES);
    if (n != HM_MEDIA_FRAME_BYTES) {
        printf("[Vision-ERR]: capture failed n=%d\n", n);
        free(frame);
        frame = NULL;
        return;
    }

    snprintf(path, sizeof(path),
             "/v1/media/frame?w=320&h=240&swap=1&q=%s", qenc);
    snprintf(auth, sizeof(auth), "Bearer %s", token);
    printf("[Vision]: uploading %d bytes to %s:%s\n", n, host, port);
    status = vela_https_request(host, port, "POST", path, extra,
                                (const char *)frame, n,
                                resp, sizeof(resp), &rlen);
    free(frame);
    frame = NULL;
    if (rlen < sizeof(resp))
        resp[rlen] = '\0';
    else
        resp[sizeof(resp) - 1] = '\0';
    if (status == 200) {
        hm_media_json_str(resp, "\"text\":\"", text, sizeof(text));
        printf("[Vision]: %s\n", text);
    } else {
        printf("[Vision-ERR]: http=%d body=%.200s\n", status, resp);
    }
}


/* ── HomeMind voice：录音 → 云端 ASR → MiMo 问答 → 小爱音箱播报 ── */

/* 5 秒：用户习惯把唤醒词和指令连着说，窗口留宽一点更稳。
 * 2026-09-15 实机验证 5s(160000B) 采集 PASS continuous。 */
#define HM_VOICE_SECONDS 5
/* 听到提示音后再开麦的等待；太短会把提示音本身录进去。 */
#define HM_WAKE_PROMPT_WAIT_MS 2200

/* 实测麦克风采样率（Hz）。采集会话结束时更新；语音上报用它声明 rate
 * 参数 —— 云端 local_asr.transcribe() 会把 PCM 按 rate 包成 WAV 交给
 * faster-whisper 重采样，声明错速率 = 时间轴错 = 转写崩。 */
static int g_hm_mic_rate_hz;

/* 音频连续采集：2026-09-10 验证过的配方（单次 open + 640B 入队循环）。
 *
 * 关键教训：**不要每块重新 open**。nuttx/audio/audio.c 的 head/tail 记账在重新
 * open 后会错位，poll 立刻返回 POLLIN|POLLERR 而 apb->nbytes 仍为 0，表现就是
 * 只拿到第 1 块 640 字节、之后再也拿不到数据。本函数曾在 2026-09-03 采用
 * "每块 open->config->alloc->enqueue->start->排空->stop->free->close" 的保底
 * 形态，2026-09-10 查明该形态已被上述 bug 破坏（实机复现 "captured 640 bytes /
 * captured too little"），因此改为直接复用 hm_audio_stream_session。 */
static int hm_audio_stream_session(unsigned char *dst, int want, int rate);

static int hm_voice_record(unsigned char *buf, int want_bytes, int rate)
{
    int got = hm_audio_stream_session(buf, want_bytes, rate);

    printf("[Voice-DBG] captured %d bytes\n", got);
    return got;
}

static void hm_json_escape(const char *in, char *out, int cap)
{
    int n = 0;

    while (*in && n < cap - 7) {
        unsigned char c = (unsigned char)*in++;
        if (c == '"' || c == '\\') {
            out[n++] = '\\';
            out[n++] = (char)c;
        } else if (c == '\n') {
            out[n++] = '\\';
            out[n++] = 'n';
        } else if (c == '\r' || c == '\t') {
            out[n++] = ' ';
        } else if (c >= 0x20) {
            out[n++] = (char)c;
        }
    }
    out[n] = '\0';
}

/* 语音闭环的一次完整往返：录音 -> 家庭私有云本机 ASR + 语义规划 + 执行 ->
 * 小爱播报回复。命令行 `voice` 与 KWS 唤醒后的自动触发共用这一份实现。
 * 返回 0 表示拿到了有效转写（或识别到人说话），-1 表示没听到人话/链路失败。 */
/* 唤醒提示音：请家庭音箱回一句"我在，请说"。
 *
 * 用户习惯把唤醒词和指令连成一句说完，而唤醒判定需要人声之后的一段静音；
 * 等判定成功，指令已经说完了，端侧再开录音只会录到沉默。先给一个听得见的
 * 应答，用户才知道该开口——这是让语音闭环稳定的关键一环。
 * 尽力而为：失败不抛错，后面的录音照常进行。 */
static void hm_voice_wake_prompt(void)
{
    char host[64] = "192.168.31.251";
    char port[8] = "443";
    char token[128] = "";
    char auth[224];
    static char resp[512];
    const vela_header_t hdr[] = {
        { "Content-Type", "application/json" },
        { "Authorization", auth },
        { NULL, NULL }
    };
    size_t rlen = 0;
    int status;

    claw_config_get("media_host", host, sizeof(host));
    claw_config_get("media_port", port, sizeof(port));
    claw_config_get("media_token", token, sizeof(token));
    if (!token[0])
        return;

    snprintf(auth, sizeof(auth), "Bearer %s", token);
    status = vela_https_request(host, port, "POST",
                                "/v1/voice/wake?device_id=esp32s3-eye",
                                hdr, "{}", 2, resp, sizeof(resp), &rlen);
    printf("[Voice] wake prompt -> %d\n", status);
    fflush(stdout);
}

static int hm_voice_utterance_roundtrip(void)
{
    char host[64] = "192.168.31.251";
    char port[8] = "443";
    char token[128] = "";
    char auth[224];
    char path[128];
    static unsigned char *pcm;
    static char resp[4096];
    char *body = NULL;
    char trans[512];
    char esc_t[1024];
    char esc_a[1024];
    const vela_header_t bin_hdr[] = {
        { "Content-Type", "application/octet-stream" },
        { "Authorization", auth },
        { NULL, NULL }
    };
    const vela_header_t json_hdr[] = {
        { "Content-Type", "application/json" },
        { "Authorization", auth },
        { NULL, NULL }
    };
    size_t rlen = 0;
    int status;
    int pcm_len;
    int mic_rate;
    int i;

    claw_config_get("media_host", host, sizeof(host));
    claw_config_get("media_port", port, sizeof(port));
    claw_config_get("media_token", token, sizeof(token));
    if (!token[0]) {
        printf("[Voice-ERR]: media_token not set (set_media first)\n");
        return -1;
    }
    /* 不再需要 llm_host/api_key：语义规划与执行都在家庭私有云本机完成，
     * 端侧只负责采集音频并上报（对应方案"端侧采集、云侧理解"的分工）。 */

    pcm = malloc(HM_VOICE_SECONDS * 16000 * 2);
    if (!pcm) {
        printf("[Voice-ERR]: alloc failed\n");
        return -1;
    }
    printf("[Voice]: recording ~%ds, speak now...\n", HM_VOICE_SECONDS);
    hm_lcd_show_text("SPEAK NOW");
    pcm_len = hm_voice_record(pcm, HM_VOICE_SECONDS * 16000 * 2, 16000);
    /* 用实测速率声明上传参数：云端按声明速率把 PCM 包成 WAV 再交给
     * faster-whisper 重采样，声明错速率会让时间轴整体错掉。
     * 实测值带抖动（usleep 按 tick 取整、块边界不对齐），吸附到最近的
     * 标准采样率；云端 /v1/voice/utterance 只接受 8000..48000。 */
    mic_rate = g_hm_mic_rate_hz > 0 ? g_hm_mic_rate_hz : 16000;
    {
        static const int k_std[] = { 8000, 11025, 12000, 16000, 22050,
                                     24000, 32000, 44100, 48000 };
        int best = k_std[0];
        int bi;

        for (bi = 0; bi < (int)(sizeof(k_std) / sizeof(k_std[0])); bi++) {
            if (abs(mic_rate - k_std[bi]) < abs(mic_rate - best))
                best = k_std[bi];
        }
        if (mic_rate >= 7000 && mic_rate <= 52000)
            mic_rate = best;
        if (mic_rate < 8000)
            mic_rate = 8000;
        if (mic_rate > 48000)
            mic_rate = 48000;
    }
    printf("[Voice]: measured mic rate = %d Hz (raw %d)\n",
           mic_rate, g_hm_mic_rate_hz);
    if (pcm_len < mic_rate * HM_VOICE_SECONDS) {
        printf("[Voice-ERR]: captured too little (%d bytes)\n", pcm_len);
        hm_lcd_show_text("MIC ERR");
        hm_lcd_release();
        free(pcm);
        pcm = NULL;
        return -1;
    }
    printf("[Voice]: captured %d bytes, transcribing...\n", pcm_len);
    hm_lcd_show_text("THINKING");

    /* 一步到位：家庭私有云本机 ASR + 语义规划 + 执行。
     * announce=0 —— 播报由端侧自己发起，避免同一条回复被云端与端侧播两遍。 */
    snprintf(auth, sizeof(auth), "Bearer %s", token);
    /* 采样率二次尝试。吸附档位本身是带抖动的估计，赌错一次整轮语音就
     * 白做；而复核只需把同一段 PCM 重发一次，不必重新录音。
     * 实测档是 16000 就用 8000 复核，否则用 16000 复核。 */
    {
        int rate_try[2];
        int attempt;

        rate_try[0] = mic_rate;
        rate_try[1] = (mic_rate == 16000) ? 8000 : 16000;
        for (attempt = 0; attempt < 2; attempt++) {
            trans[0] = '\0';
            snprintf(path, sizeof(path),
                     "/v1/voice/utterance?rate=%d&device_id=esp32s3-eye"
                     "&announce=0", rate_try[attempt]);
            status = vela_https_request(host, port, "POST", path, bin_hdr,
                                        (const char *)pcm, pcm_len,
                                        resp, sizeof(resp), &rlen);
            if (rlen < sizeof(resp))
                resp[rlen] = '\0';
            else
                resp[sizeof(resp) - 1] = '\0';
            if (status != 200) {
                printf("[Voice-ERR]: utterance rate=%d http=%d "
                       "body=%.200s\n", rate_try[attempt], status, resp);
                continue;
            }
            hm_media_json_str(resp, "\"text\":\"", trans, sizeof(trans));
            if (trans[0]) {
                printf("[Voice]: recognised at %d Hz\n",
                       rate_try[attempt]);
                break;
            }
            printf("[Voice]: rate=%d -> asr_empty, retry\n",
                   rate_try[attempt]);
        }
    }
    /* 两次请求都已同步发送完毕，释放录音缓冲：
     * pcm 是 static 指针，成功路径若不释放会每轮语音泄漏约 96 KB。 */
    free(pcm);
    pcm = NULL;
    if (!trans[0]) {
        /* 云侧回 asr_empty：确实没听到人话，不算链路故障，不触发任何控制 */
        printf("[Voice]: no speech recognised (asr_empty)\n");
        hm_lcd_release();
        hm_lcd_show_text("NO SPEECH");
        return -1;
    }
    printf("[Voice]: %s\n", trans);

    /* 回答直接取私有云返回的 plan.speak，不再调用公网大模型 */
    esc_a[0] = '\0';
    hm_media_json_str(resp, "\"speak\":\"", esc_a, sizeof(esc_a));
    if (!esc_a[0])
        snprintf(esc_a, sizeof(esc_a), "好的");
    printf("[Voice]: resp=%.400s\n", resp);   /* 含 plan/executed，便于验收 */
    printf("[Agent]: %s\n", esc_a);
    hm_lcd_release();
    hm_lcd_show_text("DONE");

    /* 让小爱音箱播报回答（announce 端点 -> MQTT speak -> 网关 -> HA） */
    snprintf(auth, sizeof(auth), "Bearer %s", token);
    hm_json_escape(esc_a, esc_t, sizeof(esc_t));
    body = malloc(1024);
    if (!body) {
        printf("[Voice-ERR]: announce alloc failed\n");
        return 0;   /* 转写与执行已完成，只是播报没发出去 */
    }
    snprintf(body, 1024,
             "{\"device_id\":\"esp32s3-eye\",\"text\":\"%s\"}", esc_t);
    status = vela_https_request(host, port, "POST", "/v1/media/announce",
                                json_hdr, body, strlen(body),
                                resp, sizeof(resp), &rlen);
    free(body);
    body = NULL;
    printf("[Voice]: announce http=%d（音箱应已播报）\n", status);

    /* 注意：语义规划与执行已在家庭私有云完成（云端会下发 led/mihome 指令），
     * 端侧不再把同一句话二次送入本地 agent 管线，避免"开灯"被执行两次。 */
    (void)i;
    return 0;
}

/* 命令行入口：voice */
static void cmd_voice(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    hm_voice_utterance_roundtrip();
}








/* Single-session streaming: enqueue, wait, copy, free, repeat.
 * audio_poll only waits correctly for the first buffer; later waits use
 * usleep (640B@16kHz ≈ 20ms). Recycle APBs to stay within MAXINFLIGHT=4. */
/* ============ HM_AUDIO_RATEPROBE ============
 * 驱动侧（补丁 0006）导出的运行时旋钮。默认全 -1 = 保持原行为。
 * 目的：把"RX 每帧几个 slot / 采样字宽 / WS 宽度"这类纸面推不出来的
 * 问题，变成一组可实测的配置扫描，避免反复刷写。 */
extern int g_hm_i2s_rx_dbg;
extern int g_hm_i2s_rx_half_bits;
extern int g_hm_i2s_rx_bits_mod;
extern int g_hm_i2s_rx_chan_bits;
extern int g_hm_i2s_rx_ws_width;
extern int g_hm_i2s_rx_tot_chan;

static void cmd_i2sknob(int argc, char **argv)
{
    if (argc > 1)
        g_hm_i2s_rx_half_bits = atoi(argv[1]);
    if (argc > 2)
        g_hm_i2s_rx_bits_mod = atoi(argv[2]);
    if (argc > 3)
        g_hm_i2s_rx_chan_bits = atoi(argv[3]);
    if (argc > 4)
        g_hm_i2s_rx_ws_width = atoi(argv[4]);
    if (argc > 5)
        g_hm_i2s_rx_tot_chan = atoi(argv[5]);
    if (argc > 6)
        g_hm_i2s_rx_dbg = atoi(argv[6]);
    printf("[I2SKNOB] half=%d bitsmod=%d chanbits=%d ws=%d totchan=%d dbg=%d "
           "(-1=保持原行为)\n",
           g_hm_i2s_rx_half_bits, g_hm_i2s_rx_bits_mod,
           g_hm_i2s_rx_chan_bits, g_hm_i2s_rx_ws_width,
           g_hm_i2s_rx_tot_chan, g_hm_i2s_rx_dbg);
    fflush(stdout);
}

static int hm_audio_stream_session(unsigned char *dst, int want, int rate)
{
    const char *path = "/dev/audio/pcm_in0";
    struct audio_caps_desc_s caps;
    struct audio_buf_desc_s desc;
    struct ap_buffer_info_s info;
    struct pollfd pfd;
    struct ap_buffer_s *apb = NULL;
    struct timespec t_start;
    struct timespec t_now;
    int bsize = 640;
    int got = 0;
    int fd;
    int started = 0;
    int chunk = 0;
    int used = 0;
    int out_samp = 0;
    int nz_even = 0;
    int nz_odd = 0;
    int zero_chunks = 0;
    int dup_chunks = 0;
    int budget_ms;
    long elapsed_ms;
    long total_ms;
    long steady_ms;
    int have_first = 0;
    struct timespec t_first;
    int poll_to = 0;
    long gap_ms = 0;
    long gap_max = 0;
    long poll_wait_ms = 0;
    static unsigned char prev_chunk[640];

    fd = open(path, O_RDWR | O_NONBLOCK);
    if (fd < 0) {
        printf("[Audio] open errno=%d\n", errno);
        return -1;
    }
    memset(&caps, 0, sizeof(caps));
    caps.caps.ac_len = sizeof(struct audio_caps_s);
    caps.caps.ac_type = AUDIO_TYPE_INPUT;
    caps.caps.ac_controls.w = rate;
    caps.caps.ac_controls.b[2] = 16;
    caps.caps.ac_channels = 1;
    caps.caps.ac_format.hw = AUDIO_FMT_PCM;
    if (ioctl(fd, AUDIOIOC_CONFIGURE, (uintptr_t)&caps) < 0) {
        close(fd);
        return -1;
    }
    (void)ioctl(fd, AUDIOIOC_GETBUFFERINFO, (uintptr_t)&info);

    /* 采集时长按"想要多少字节 / 每秒多少字节"反推，与驱动实际速率无关：
     * want 字节 = rate*2 字节/秒 x 秒数。历史写法（写死块数上限）会让
     * "改录音时长"变成静默失败。 */
    budget_ms = (want * 1000) / (rate * 2);
    if (budget_ms < 200)
        budget_ms = 200;
    memset(prev_chunk, 0, sizeof(prev_chunk));
    clock_gettime(CLOCK_MONOTONIC, &t_start);

    memset(&pfd, 0, sizeof(pfd));
    pfd.fd = fd;
    pfd.events = POLLIN;

    while (got < want) {
        memset(&desc, 0, sizeof(desc));
        desc.numbytes = bsize;
        desc.u.pbuffer = &apb;
        if (ioctl(fd, AUDIOIOC_ALLOCBUFFER, (uintptr_t)&desc) !=
                (int)sizeof(desc) || !apb)
            break;
        memset(&desc, 0, sizeof(desc));
        desc.u.buffer = apb;
        desc.numbytes = bsize;
        if (ioctl(fd, AUDIOIOC_ENQUEUEBUFFER, (uintptr_t)&desc) < 0) {
            memset(&desc, 0, sizeof(desc));
            desc.u.buffer = apb;
            ioctl(fd, AUDIOIOC_FREEBUFFER, (uintptr_t)&desc);
            printf("[Audio] enq fail chunk=%d\n", chunk);
            break;
        }
        if (!started) {
            if (ioctl(fd, AUDIOIOC_START, 0) < 0) {
                printf("[Audio] start fail\n");
                break;
            }
            started = 1;
        }
        if (chunk == 0)
            poll(&pfd, 1, 800);
        else
            {
                /* 2026-09-16 定论：这里原本是固定 `usleep(10000)`（=1 个
                 * tick）。CONFIG_USEC_PER_TICK=10000 使 usleep 按 tick 向上
                 * 取整，于是消费速率被死锁在 640B/20ms = 32kB/s。
                 * 实测 5 秒只拿到 226 块（22.0ms/块）、eff_rate=14457Hz ——
                 * 历史上一度把这个数字当成"硬件采样率实测值"，其实是
                 * **消费循环自己的上限**。硬件产得更快时，APB 池（MAXINFLIGHT）
                 * 填满，整条链路被反压，中间必然丢样点。
                 * 改成按数据到达驱动：等 POLLIN（驱动填满一块才置位），
                 * 消费端就永远不会反过来给生产者限速。 */
                struct timespec ta;
                struct timespec tb;

                clock_gettime(CLOCK_MONOTONIC, &ta);
                /* 2026-09-16 实测：把这里换成 `poll(&pfd,1,200)` 会让本驱动
                 * 立刻回"POLLIN 但 apb->nbytes=0"（因为每轮都新分配 APB，
                 * 完成队列头部的缓冲并不是本轮这个），随后缓冲池耗尽、
                 * AUDIOIOC_ALLOCBUFFER 永久阻塞 —— 实测卡在第 5 块。
                 * 而 [STRM] 又证明消费端不是瓶颈（生产 640B/22ms 慢于
                 * 消费 640B/10ms），所以这里维持已知可用的 usleep(1 tick)，
                 * 只加计时把"到底谁慢"量化出来。 */
                usleep(10000);
                clock_gettime(CLOCK_MONOTONIC, &tb);
                gap_ms = (long)(tb.tv_sec - ta.tv_sec) * 1000L +
                         (long)(tb.tv_nsec - ta.tv_nsec) / 1000000L;
                poll_wait_ms += gap_ms;
                if (gap_ms > gap_max)
                    gap_max = gap_ms;
            }
        /* 整块拷走。2026-09-16 实测（[STRM] nzL/nzR 相当）证明 RX 不是
         * "右声道恒零"的交错流，去交错会丢掉一半真实样点，故不做。 */
        if (apb->nbytes >= 2) {
            const short *src = (const short *)apb->samp;
            int nsamp = (int)apb->nbytes / 2;
            int k;

            /* dup：驱动是否在重复投递同一块（过载征兆）；
             * zero：驱动是否欠载（生产慢于消费）。 */
            if (apb->nbytes == (unsigned)bsize) {
                if (chunk > 0 && memcmp(prev_chunk, apb->samp, bsize) == 0)
                    dup_chunks++;
                memcpy(prev_chunk, apb->samp, bsize);
            }
            if (got + nsamp * 2 <= want) {
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
                used++;
                if (used == 1)
                    {
                        int q;
                        printf("[STRM-RAW] nbytes=%u first24:",
                               (unsigned)apb->nbytes);
                        for (q = 0; q < 24 && q < nsamp; q++)
                            printf(" %d", (int)src[q]);
                        printf("\n");
                        fflush(stdout);
                    }
                if (!have_first) {
                    /* 稳态计时的基准：从第一块真正拿到数据之后起算，
                     * 剔掉首次 poll 与串口打印的固定开销。 */
                    clock_gettime(CLOCK_MONOTONIC, &t_first);
                    have_first = 1;
                }
            }
        }
        else {
            zero_chunks++;
        }
        chunk++;
        if (chunk <= 4 || chunk % 10 == 0)
            printf("[Audio] chunk=%d nbytes=%u total=%d\n",
                   chunk, apb->nbytes, got);
        memset(&desc, 0, sizeof(desc));
        desc.u.buffer = apb;
        ioctl(fd, AUDIOIOC_FREEBUFFER, (uintptr_t)&desc);
        apb = NULL;
        /* 退出条件按"实际拿到多少数据"判断，而不是总块数：空块（生产者
         * 还没填好）不应该消耗采集预算。历史上写死 `chunk > 200`
         * （=128000B=4 秒）会让"改录音时长"变成静默失败；改按目标字节数
         * 推导后又发现空块会提前吃掉预算，所以这里统一用 used。
         * 绝对上限给 3 倍块数兜底，避免驱动异常时死等。 */
        /* 退出：墙钟预算用尽或输出已填满。历史教训——写死块数上限会让
         * "改录音时长"变成静默失败（见 2026-09-15 取证）。 */
        clock_gettime(CLOCK_MONOTONIC, &t_now);
        elapsed_ms = (long)(t_now.tv_sec - t_start.tv_sec) * 1000L +
                     (long)(t_now.tv_nsec - t_start.tv_nsec) / 1000000L;
        if (elapsed_ms >= (long)budget_ms || got >= want)
            break;
        if (chunk > 4000)
            break;
    }
    if (started)
        ioctl(fd, AUDIOIOC_STOP, 0);
    close(fd);
    clock_gettime(CLOCK_MONOTONIC, &t_now);
    total_ms = (long)(t_now.tv_sec - t_start.tv_sec) * 1000L +
               (long)(t_now.tv_nsec - t_start.tv_nsec) / 1000000L;
    /* 采样率只按**稳态**估：整段墙钟当分母会含首次 poll 与串口打印的
     * 固定开销，2 秒采集实测 13280 Hz、3 秒实测 15573 Hz —— 同样硬件
     * 只因为开销占比不同。被压低后会吸附到 12000 而不是 16000。 */
    {
        struct timespec t_base = have_first ? t_first : t_start;
        long n_samp = (long)out_samp - (have_first ? (bsize / 2) : 0);

        steady_ms = (long)(t_now.tv_sec - t_base.tv_sec) * 1000L +
                    (long)(t_now.tv_nsec - t_base.tv_nsec) / 1000000L;
        if (n_samp < 0)
            n_samp = 0;
        if (steady_ms > 0)
            g_hm_mic_rate_hz = (int)(n_samp * 1000L / steady_ms);
    }
    printf("[STRM] out=%d bytes (%d samples) chunks=%d zero=%d dup=%d "
           "ms=%ld steady_ms=%ld eff_rate=%d Hz nzE=%d nzO=%d "
           "budget=%d ms tmo=%d gap_max=%ld poll_wait=%ld ms\n",
           got, out_samp, chunk, zero_chunks, dup_chunks,
           total_ms, steady_ms, g_hm_mic_rate_hz,
           nz_even, nz_odd, budget_ms, poll_to, gap_max,
           poll_wait_ms);
    fflush(stdout);
    return got;
}

static void cmd_audio_stream(int argc, char **argv)
{
    int seconds = 2;
    int rate = 16000;
    int want;
    int got;
    unsigned char *buf;
    long long sum_sq = 0;
    int peak = 0;
    int i;
    double rms;

    if (argc > 1) {
        int v = atoi(argv[1]);
        if (v > 0 && v <= 10)
            seconds = v;
    }
    want = seconds * rate * 2;
    buf = (unsigned char *)malloc((size_t)want);
    if (!buf) {
        printf("[Audio-ERR] alloc\n");
        return;
    }
    printf("[Audio] stream-session %ds @%dHz\n", seconds, rate);
    got = hm_audio_stream_session(buf, want, rate);
    for (i = 0; i + 1 < got; i += 2) {
        int s = (int)(int16_t)(buf[i] | (buf[i + 1] << 8));
        sum_sq += (long long)s * (long long)s;
        if (s < 0)
            s = -s;
        if (s > peak)
            peak = s;
    }
    rms = (got >= 2) ? sqrt((double)sum_sq / (double)(got / 2)) : 0.0;
    printf("[Audio] DONE bytes=%d/%d peak=%d rms=%.1f\n",
           got, want, peak, rms);
    if (got >= want * 8 / 10)
        printf("[Audio] PASS continuous\n");
    else if (got >= 640 * 4)
        printf("[Audio] PARTIAL\n");
    else
        printf("[Audio] FAIL\n");
    free(buf);
}



/* ── Offline wake loop (energy VAD + local LED) ──────────────────
 * Uses the working single-session I2S recipe (poll first chunk, then
 * usleep). This is an energy / voice-activity wake, NOT a trained
 * keyword model for "你好，openvela". Structured so a KWS model can
 * replace hm_wake_score() later. */
#define HM_WAKE_CHUNK     640
#define HM_WAKE_RATE      16000
#define HM_WAKE_RUN_MAX   600   /* ~12s of 20ms chunks; override via argv */

static int hm_led_blink(int times, int on_ms, int off_ms)
{
    int fd = open("/dev/gpio0", O_RDWR);
    int i;
    if (fd < 0)
        return -1;
    for (i = 0; i < times; i++) {
        ioctl(fd, GPIOC_WRITE, 1UL);
        usleep(on_ms * 1000);
        ioctl(fd, GPIOC_WRITE, 0UL);
        usleep(off_ms * 1000);
    }
    close(fd);
    return 0;
}

static float hm_chunk_rms(const unsigned char *buf, int nbytes)
{
    /* 必须 64 位累加：一个 chunk 320 个样本，单样本平方可达 1.07e9，
     * 合计约 3.4e11，32 位 long 必然回绕（回绕值还可能为负 → sqrt 得 nan），
     * 会让 VAD 门限彻底失效：静音也误开、调高则永不开。 */
    long long sum = 0;
    int n = nbytes / 2;
    int i;
    if (n <= 0)
        return 0.0f;
    for (i = 0; i + 1 < nbytes; i += 2) {
        int s = (int)(int16_t)(buf[i] | (buf[i + 1] << 8));
        sum += (long long)s * (long long)s;
    }
    return (float)sqrt((double)sum / (double)n);
}

/* wake if rms >= thr for hold consecutive chunks */
static int hm_wake_score(float rms, float thr, int *hold_need, int *hold_now)
{
    if (rms >= thr) {
        (*hold_now)++;
        if (*hold_now >= *hold_need) {
            *hold_now = 0;
            return 1;
        }
    } else {
        *hold_now = 0;
    }
    return 0;
}


/* Record labeled PCM for KWS training: wake_rec pos|neg <index> [sec]
 * Writes /data/kws_<label>_<idx>.pcm  (s16le 16k mono). */

static void cmd_kws_dump(int argc, char **argv)
{
    char path[64];
    const char *label = "pos";
    int idx = 1;
    FILE *fp;
    unsigned char buf[57];
    char b64[80];
    static const char enc[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    int n;
    int i;

    if (argc >= 3) {
        label = argv[1];
        idx = atoi(argv[2]);
    } else if (argc == 2) {
        /* full path */
        snprintf(path, sizeof(path), "%s", argv[1]);
        goto dump;
    }
    snprintf(path, sizeof(path), "/data/kws_%s_%02d.pcm", label, idx);
dump:
    fp = fopen(path, "rb");
    if (!fp) {
        printf("KWS_ERR open %s errno=%d\\n", path, errno);
        return;
    }
    printf("KWS_B64_BEGIN %s\\n", path);
    while ((n = fread(buf, 1, 57, fp)) > 0) {
        int o = 0;
        for (i = 0; i < n; i += 3) {
            unsigned v = buf[i] << 16;
            if (i + 1 < n) v |= buf[i + 1] << 8;
            if (i + 2 < n) v |= buf[i + 2];
            b64[o++] = enc[(v >> 18) & 63];
            b64[o++] = enc[(v >> 12) & 63];
            b64[o++] = (i + 1 < n) ? enc[(v >> 6) & 63] : '=';
            b64[o++] = (i + 2 < n) ? enc[v & 63] : '=';
        }
        b64[o] = 0;
        printf("%s\\n", b64);
    }
    fclose(fp);
    printf("KWS_B64_END\\n");
}


static void cmd_wake_rec(int argc, char **argv)
{
    const char *label = "pos";
    int idx = 0;
    int seconds = 3;
    char path[64];
    int rate = 16000;
    int want;
    int got = 0;
    unsigned char *buf;
    FILE *fp;
    int fd;
    int started = 0;
    struct audio_caps_desc_s caps;
    struct audio_buf_desc_s desc;
    struct ap_buffer_info_s info;
    struct ap_buffer_s *apb = NULL;
    struct pollfd pfd;
    int bsize = 640;
    int chunk = 0;

    if (argc < 2) {
        printf("Usage: wake_rec pos|neg <index> [sec=3]\\n");
        return;
    }
    label = argv[1];
    if (strcmp(label, "pos") != 0 && strcmp(label, "neg") != 0) {
        printf("[Wake-ERR] label must be pos|neg\\n");
        return;
    }
    if (argc >= 3)
        idx = atoi(argv[2]);
    if (argc >= 4) {
        int v = atoi(argv[3]);
        if (v > 0 && v <= 10)
            seconds = v;
    }
    want = seconds * rate * 2;
    snprintf(path, sizeof(path), "/data/kws_%s_%02d.pcm", label, idx);
    buf = (unsigned char *)malloc((size_t)want);
    if (!buf) {
        printf("[Wake-ERR] alloc\\n");
        return;
    }
    printf("[WakeRec] %s idx=%d sec=%d -> %s\\n", label, idx, seconds, path);
    printf("[WakeRec] START speaking/noise now...\\n");
    fflush(stdout);
    if (strcmp(label, "pos") == 0)
        hm_lcd_show_text("SAY: NI HAO OPENVELA");
    else
        hm_lcd_show_text("KEEP QUIET");
    {
        int lfd = open("/dev/gpio0", O_RDWR);
        if (lfd >= 0) {
            ioctl(lfd, GPIOC_WRITE, 1UL);
            close(lfd);
        }
    }
    usleep(600000);

    fd = open("/dev/audio/pcm_in0", O_RDWR | O_NONBLOCK);
    if (fd < 0) {
        printf("[Wake-ERR] open\\n");
        free(buf);
        return;
    }
    memset(&caps, 0, sizeof(caps));
    caps.caps.ac_len = sizeof(struct audio_caps_s);
    caps.caps.ac_type = AUDIO_TYPE_INPUT;
    caps.caps.ac_controls.w = rate;
    caps.caps.ac_controls.b[2] = 16;
    caps.caps.ac_channels = 1;
    caps.caps.ac_format.hw = AUDIO_FMT_PCM;
    if (ioctl(fd, AUDIOIOC_CONFIGURE, (uintptr_t)&caps) < 0) {
        close(fd);
        free(buf);
        return;
    }
    memset(&info, 0, sizeof(info));
    (void)ioctl(fd, AUDIOIOC_GETBUFFERINFO, (uintptr_t)&info);
    memset(&pfd, 0, sizeof(pfd));
    pfd.fd = fd;
    pfd.events = POLLIN;

    while (got < want) {
        apb = NULL;
        memset(&desc, 0, sizeof(desc));
        desc.numbytes = bsize;
        desc.u.pbuffer = &apb;
        if (ioctl(fd, AUDIOIOC_ALLOCBUFFER, (uintptr_t)&desc) !=
                (int)sizeof(desc) || !apb)
            break;
        memset(&desc, 0, sizeof(desc));
        desc.u.buffer = apb;
        desc.numbytes = bsize;
        if (ioctl(fd, AUDIOIOC_ENQUEUEBUFFER, (uintptr_t)&desc) < 0) {
            memset(&desc, 0, sizeof(desc));
            desc.u.buffer = apb;
            ioctl(fd, AUDIOIOC_FREEBUFFER, (uintptr_t)&desc);
            break;
        }
        if (!started) {
            if (ioctl(fd, AUDIOIOC_START, 0) < 0)
                break;
            started = 1;
            poll(&pfd, 1, 800);
        } else {
            usleep(30000);
        }
        if (apb->nbytes > 0 && got + apb->nbytes <= want) {
            memcpy(buf + got, apb->samp, apb->nbytes);
            got += apb->nbytes;
        }
        memset(&desc, 0, sizeof(desc));
        desc.u.buffer = apb;
        ioctl(fd, AUDIOIOC_FREEBUFFER, (uintptr_t)&desc);
        chunk++;
    }
    if (started)
        ioctl(fd, AUDIOIOC_STOP, 0);
    close(fd);

    printf("[WakeRec] captured %d bytes chunks=%d\\n", got, chunk);
    if (got < want / 2) {
        printf("[WakeRec] FAIL short\\n");
        hm_lcd_show_text("FAIL");
        free(buf);
        return;
    }
    fp = fopen(path, "wb");
    if (!fp) {
        printf("[WakeRec] open file errno=%d\\n", errno);
        free(buf);
        return;
    }
    {
        size_t nw = fwrite(buf, 1, (size_t)got, fp);
        int ferr = ferror(fp);
        fflush(fp);
        fsync(fileno(fp));
        fclose(fp);
        printf("[WakeRec] wrote %u / %d ferr=%d\\n", (unsigned)nw, got, ferr);
        if (nw != (size_t)got) {
            printf("[WakeRec] FAIL fwrite\\n");
            hm_lcd_show_text("WR FAIL");
            free(buf);
            return;
        }
    }
    printf("[WakeRec] SAVED %s (%d bytes)\\n", path, got);
    hm_lcd_show_text("SAVED OK");
    usleep(500000);
    free(buf);
}



/* ── Trained KWS (你好 openvela) linear model ───────────────────
 * Features: log-mel 16 bands, 40 frames, stats mean/std/max/dmean = 64
 * Score: z = ((f-mu)*sd_inv)·w_q*scale + b; sigmoid >= 0.5 */
#include "vision/kws_model_data.h"

static float kws_score_pcm(const int16_t *pcm, int nsamp)
{
    float rms[KWS_N_FRAMES], zcr[KWS_N_FRAMES];
    float feat[KWS_FEATURE_DIM];
    int fi, i;
    int need = (KWS_N_FRAMES-1)*KWS_HOP + KWS_WIN;
    int16_t peak = 1;
    if (nsamp < KWS_WIN * 4)
        return -1.0f;
    if (nsamp < need)
        need = nsamp;
    /* normalize to unit peak */
    for (i = 0; i < nsamp; i++) {
        int16_t v = pcm[i];
        if (v < 0) v = -v;
        if (v > peak) peak = v;
    }
    memset(rms, 0, sizeof(rms));
    memset(zcr, 0, sizeof(zcr));
    for (fi = 0; fi < KWS_N_FRAMES; fi++) {
        const int16_t *s = pcm + fi * KWS_HOP;
        float e = 0;
        int zc = 0;
        if (fi * KWS_HOP + KWS_WIN > nsamp)
            break;
        for (i = 0; i < KWS_WIN; i++) {
            float v = (float)s[i] / (float)peak;
            e += v * v;
            if (i > 0 && ((s[i] >= 0) != (s[i - 1] >= 0)))
                zc++;
        }
        rms[fi] = (float)sqrt(e / KWS_WIN);
        zcr[fi] = (float)zc / (float)(KWS_WIN - 1);
    }
    {
        float sr=0, sz=0, mxr=0, zvar=0, var=0;
        int n = KWS_N_FRAMES;
        for (fi = 0; fi < n; fi++) {
            sr += rms[fi]; sz += zcr[fi];
            if (rms[fi] > mxr) mxr = rms[fi];
        }
        sr /= n; sz /= n;
        for (fi = 0; fi < n; fi++) {
            float d = rms[fi] - sr; var += d*d;
            float dz = zcr[fi] - sz; zvar += dz*dz;
        }
        feat[0] = sr;
        feat[1] = (float)sqrt(var / n);
        feat[2] = feat[1] / (sr + 1e-9f);
        feat[3] = sz;
        feat[4] = (float)sqrt(zvar / n);
        feat[5] = (rms[0]+rms[1]+rms[2]+rms[3]+rms[4]+rms[5]+rms[6]+rms[7]+rms[8]+rms[9]) / 10.0f / (sr + 1e-9f);
        feat[6] = (mxr - sr) / (sr + 1e-9f);
        feat[7] = (rms[0]+rms[1]+rms[2]+rms[3]+rms[4] - (rms[n-5]+rms[n-4]+rms[n-3]+rms[n-2]+rms[n-1])) / 5.0f / (sr + 1e-9f);
    }
    {
        float z = kws_b;
        for (i = 0; i < KWS_FEATURE_DIM; i++) {
            float fx = (feat[i] - kws_mu[i]) * kws_sd_inv[i];
            z += fx * (float)kws_w_q[i] * kws_w_scale;
        }
        printf("[KWS] pk=%d f2=%.3f f3=%.3f f4=%.3f z=%.3f\n",
               (int)peak, feat[2], feat[3], feat[4], z);
        return 1.0f / (1.0f + expf(-z));
    }
}

/* Capture ~3s and score. Used by wake_kws. */
static int kws_capture_score(float *score)
{
    const int rate = 16000;
    const int want = 3 * rate * 2;
    int16_t *pcm;
    int got = 0;
    int fd, started = 0, bsize = 640;
    struct audio_caps_desc_s caps;
    struct audio_buf_desc_s desc;
    struct ap_buffer_info_s info;
    struct ap_buffer_s *apb = NULL;
    struct pollfd pfd;

    pcm = (int16_t *)malloc((size_t)want);
    if (!pcm)
        return -1;
    fd = open("/dev/audio/pcm_in0", O_RDWR | O_NONBLOCK);
    if (fd < 0) {
        free(pcm);
        return -1;
    }
    memset(&caps, 0, sizeof(caps));
    caps.caps.ac_len = sizeof(struct audio_caps_s);
    caps.caps.ac_type = AUDIO_TYPE_INPUT;
    caps.caps.ac_controls.w = rate;
    caps.caps.ac_controls.b[2] = 16;
    caps.caps.ac_channels = 1;
    caps.caps.ac_format.hw = AUDIO_FMT_PCM;
    if (ioctl(fd, AUDIOIOC_CONFIGURE, (uintptr_t)&caps) < 0) {
        close(fd);
        free(pcm);
        return -1;
    }
    memset(&info, 0, sizeof(info));
    (void)ioctl(fd, AUDIOIOC_GETBUFFERINFO, (uintptr_t)&info);
    memset(&pfd, 0, sizeof(pfd));
    pfd.fd = fd;
    pfd.events = POLLIN;
    while (got + bsize <= want) {
        struct ap_buffer_s *ap = NULL;
        memset(&desc, 0, sizeof(desc));
        desc.numbytes = bsize;
        desc.u.pbuffer = &ap;
        if (ioctl(fd, AUDIOIOC_ALLOCBUFFER, (uintptr_t)&desc) != (int)sizeof(desc) || !ap)
            break;
        memset(&desc, 0, sizeof(desc));
        desc.u.buffer = ap;
        desc.numbytes = bsize;
        if (ioctl(fd, AUDIOIOC_ENQUEUEBUFFER, (uintptr_t)&desc) < 0) {
            memset(&desc, 0, sizeof(desc));
            desc.u.buffer = ap;
            ioctl(fd, AUDIOIOC_FREEBUFFER, (uintptr_t)&desc);
            break;
        }
        if (!started) {
            if (ioctl(fd, AUDIOIOC_START, 0) < 0)
                break;
            started = 1;
            poll(&pfd, 1, 800);
        } else {
            usleep(30000);
        }
        if (ap->nbytes > 0) {
            memcpy((char *)pcm + got, ap->samp, ap->nbytes);
            got += ap->nbytes;
        }
        memset(&desc, 0, sizeof(desc));
        desc.u.buffer = ap;
        ioctl(fd, AUDIOIOC_FREEBUFFER, (uintptr_t)&desc);
    }
    if (started)
        ioctl(fd, AUDIOIOC_STOP, 0);
    close(fd);
    printf("[KWS] got=%d nsamp=%d\n", got, got/2);
    *score = kws_score_pcm(pcm, got / 2);
    printf("[KWS] after score=%.4f\n", *score);
    free(pcm);
    return (got >= want / 2) ? 0 : -1;
}

/* ── Continuous offline KWS listen (background thread) ──────────
 * kws_listen start [thr] [vad_thr]
 * kws_listen stop
 * kws_listen status
 *
 * Pipeline: energy VAD gate -> 1.2s speech window -> kws_score_pcm
 * On HIT: LED + LCD + mqtt event {"type":"kws_wake",...}
 * Offline: no network required; MQTT publish is best-effort.
 */
static volatile int g_kws_listen_run = 0;
static volatile int g_kws_listen_hits = 0;
static volatile int g_kws_listen_frames = 0;

/* read_chunk 诊断：四种失败以前全静默，frames=0 时无从定位 */
static volatile int g_kws_dbg_calls = 0;
static volatile int g_kws_dbg_alloc = 0;
static volatile int g_kws_dbg_enq = 0;
static volatile int g_kws_dbg_start = 0;
static volatile int g_kws_dbg_empty = 0;
/* 唤醒后是否自动进入语音闭环（录音 -> 私有云本机 ASR -> 规划执行）。
 * 默认开启：这是"主动式无感交互"的默认形态；验收纯 KWS 时可 kws_listen voice off。 */
static volatile int g_kws_auto_voice = 1;
static volatile float g_kws_listen_last = 0.0f;
static pthread_t g_kws_listen_tid;
static int g_kws_listen_thr_x100 = 55;   /* score thr * 100 */
static int g_kws_listen_vad = 900;       /* energy RMS gate */

#define KWS_LISTEN_CHUNK   640
#define KWS_LISTEN_RATE    16000
#define KWS_LISTEN_RING_N  96            /* ~1.92s @ 20ms */
#define KWS_LISTEN_SCORE_N 60            /* 1.2s window */

typedef struct {
    int16_t samp[KWS_LISTEN_CHUNK / 2];
    int used;
} kws_chunk_s;

static int kws_listen_open_mic(int *out_fd)
{
    struct audio_caps_desc_s caps;
    struct ap_buffer_info_s info;
    struct pollfd pfd;
    int fd;

    fd = open("/dev/audio/pcm_in0", O_RDWR | O_NONBLOCK);
    if (fd < 0)
        return -1;
    memset(&caps, 0, sizeof(caps));
    caps.caps.ac_len = sizeof(struct audio_caps_s);
    caps.caps.ac_type = AUDIO_TYPE_INPUT;
    caps.caps.ac_controls.w = KWS_LISTEN_RATE;
    caps.caps.ac_controls.b[2] = 16;
    caps.caps.ac_channels = 1;
    caps.caps.ac_format.hw = AUDIO_FMT_PCM;
    if (ioctl(fd, AUDIOIOC_CONFIGURE, (uintptr_t)&caps) < 0) {
        close(fd);
        return -1;
    }
    /* 必须调！audio.c 的 audio_allocbuffer() 会先判 `periods >= nbuffers`
     * 而直接 return 0，而 nbuffers 只有这个 ioctl 会赋值。少这一句，
     * 后续所有 ALLOCBUFFER 都拿不到 buffer（且 errno 保持 0，极难发现）。 */
    memset(&info, 0, sizeof(info));
    if (ioctl(fd, AUDIOIOC_GETBUFFERINFO, (uintptr_t)&info) >= 0) {
        printf("[KWS-L] bufinfo nbuffers=%u size=%u\n",
               (unsigned)info.nbuffers, (unsigned)info.buffer_size);
        fflush(stdout);
    } else {
        printf("[KWS-L] GETBUFFERINFO fail errno=%d\n", errno);
        fflush(stdout);
    }
    memset(&pfd, 0, sizeof(pfd));
    pfd.fd = fd;
    pfd.events = POLLIN;
    *out_fd = fd;
    return 0;
}

static int kws_listen_read_chunk(int fd, int16_t *dst, int *started,
                                 struct pollfd *pfd)
{
    struct audio_buf_desc_s desc;
    struct ap_buffer_s *apb = NULL;
    int n = 0;

    g_kws_dbg_calls++;
    memset(&desc, 0, sizeof(desc));
    desc.numbytes = KWS_LISTEN_CHUNK;
    desc.u.pbuffer = &apb;
    if (ioctl(fd, AUDIOIOC_ALLOCBUFFER, (uintptr_t)&desc) !=
            (int)sizeof(desc) || !apb) {
        g_kws_dbg_alloc++;
        if (g_kws_dbg_alloc <= 3) {
            printf("[KWS-DBG] allocbuf fail errno=%d\n", errno);
            fflush(stdout);
        }
        usleep(10000);              /* 别让失败路径变成热转圈 */
        return -1;
    }
    memset(&desc, 0, sizeof(desc));
    desc.u.buffer = apb;
    desc.numbytes = KWS_LISTEN_CHUNK;
    if (ioctl(fd, AUDIOIOC_ENQUEUEBUFFER, (uintptr_t)&desc) < 0) {
        g_kws_dbg_enq++;
        if (g_kws_dbg_enq <= 3) {
            printf("[KWS-DBG] enqueue fail errno=%d\n", errno);
            fflush(stdout);
        }
        memset(&desc, 0, sizeof(desc));
        desc.u.buffer = apb;
        ioctl(fd, AUDIOIOC_FREEBUFFER, (uintptr_t)&desc);
        usleep(10000);
        return -1;
    }
    if (!*started) {
        if (ioctl(fd, AUDIOIOC_START, 0) < 0) {
            g_kws_dbg_start++;
            if (g_kws_dbg_start <= 3) {
                printf("[KWS-DBG] start fail errno=%d\n", errno);
                fflush(stdout);
            }
            memset(&desc, 0, sizeof(desc));
            desc.u.buffer = apb;
            ioctl(fd, AUDIOIOC_FREEBUFFER, (uintptr_t)&desc);
            usleep(10000);
            return -1;
        }
        *started = 1;
        poll(pfd, 1, 800);
    } else {
        usleep(10000);
    }
    if (apb->nbytes > 0) {
        n = apb->nbytes;
        if (n > KWS_LISTEN_CHUNK)
            n = KWS_LISTEN_CHUNK;
        memcpy(dst, apb->samp, n);
    } else {
        g_kws_dbg_empty++;
        if (g_kws_dbg_empty <= 3) {
            printf("[KWS-DBG] empty nmax=%u flags=0x%04x\n",
                   (unsigned)apb->nmaxbytes, (unsigned)apb->flags);
            fflush(stdout);
        }
    }
    memset(&desc, 0, sizeof(desc));
    desc.u.buffer = apb;
    ioctl(fd, AUDIOIOC_FREEBUFFER, (uintptr_t)&desc);
    return n;
}

static void *kws_listen_worker(void *arg)
{
    float thr = (float)g_kws_listen_thr_x100 / 100.0f;
    float vad_thr = (float)g_kws_listen_vad;
    int16_t *ring = NULL;
    int16_t chunk[KWS_LISTEN_CHUNK / 2];
    int fd = -1, started = 0;
    struct pollfd pfd;
    int hold = 0, speech = 0, silence = 0;
    int frames = 0;
    (void)arg;

    ring = (int16_t *)malloc(sizeof(int16_t) *
                             (KWS_LISTEN_RING_N * (KWS_LISTEN_CHUNK / 2)));
    if (!ring) {
        printf("[KWS-L] OOM ring\n");
        g_kws_listen_run = 0;
        return NULL;
    }
    memset(ring, 0, sizeof(int16_t) *
           (KWS_LISTEN_RING_N * (KWS_LISTEN_CHUNK / 2)));
    if (kws_listen_open_mic(&fd) != 0) {
        printf("[KWS-L] mic open fail errno=%d\n", errno);
        free(ring);
        g_kws_listen_run = 0;
        return NULL;
    }
    memset(&pfd, 0, sizeof(pfd));
    pfd.fd = fd;
    pfd.events = POLLIN;
    printf("[KWS-L] listening thr=%.2f vad=%.0f\n", thr, vad_thr);
    fflush(stdout);
    hm_lcd_show_text("KWS LISTEN");

    while (g_kws_listen_run) {
        int n = kws_listen_read_chunk(fd, chunk, &started, &pfd);
        float rms;
        if (n <= 0)
            continue;
        if (n < KWS_LISTEN_CHUNK) {
            memset((char *)chunk + n, 0, KWS_LISTEN_CHUNK - n);
            n = KWS_LISTEN_CHUNK;
        }
        /* ring write */
        {
            int idx = frames % KWS_LISTEN_RING_N;
            memcpy(ring + idx * (KWS_LISTEN_CHUNK / 2), chunk,
                   sizeof(int16_t) * (KWS_LISTEN_CHUNK / 2));
        }
        frames++;
        g_kws_listen_frames = frames;
        rms = hm_chunk_rms((unsigned char *)chunk, n);

        if (rms >= vad_thr) {
            hold++;
            silence = 0;
            if (!speech && hold >= 3) {
                speech = 1;
                hold = 0;
            }
        } else {
            hold = 0;
            if (speech) {
                silence++;
                if (silence >= 8) { /* ~160ms quiet -> score window */
                    float score = -1.0f;
                    int16_t *win = (int16_t *)malloc(
                        sizeof(int16_t) * KWS_LISTEN_SCORE_N *
                        (KWS_LISTEN_CHUNK / 2));
                    if (win) {
                        int i, got = 0;
                        int startf = frames - KWS_LISTEN_SCORE_N;
                        if (startf < 0)
                            startf = 0;
                        for (i = startf; i < frames && got < KWS_LISTEN_SCORE_N; i++) {
                            int idx = i % KWS_LISTEN_RING_N;
                            memcpy(win + got * (KWS_LISTEN_CHUNK / 2),
                                   ring + idx * (KWS_LISTEN_CHUNK / 2),
                                   sizeof(int16_t) * (KWS_LISTEN_CHUNK / 2));
                            got++;
                        }
                        if (got >= 20) {
                            score = kws_score_pcm(win, got * (KWS_LISTEN_CHUNK / 2));
                            g_kws_listen_last = score;
                            printf("[KWS-L] score=%.3f thr=%.2f frames=%d\n",
                                   score, thr, got);
                            fflush(stdout);
                            if (score >= thr) {
                                g_kws_listen_hits++;
                                printf("[KWS-L] HIT #%d\n", g_kws_listen_hits);
                                fflush(stdout);
                                /* 唤醒后尽快把麦克风交给录音：原来的
                                 * hm_led_blink(3,80,80) 会阻塞约 480ms，
                                 * 再加上 MQTT 上报，用户刚说出口的指令
                                 * 很容易被挤掉。这里只做一次极短光脉冲，
                                 * MQTT 事件挪到录音结束之后再发。 */
                                hm_lcd_show_text("SPEAK NOW");
                                hm_led_blink(1, 50, 0);
                                /* 唤醒即进入语音闭环：让出麦克风 -> 录音 ->
                                 * 家庭私有云本机 ASR + 语义规划 + 执行 ->
                                 * 重新打开麦克风继续监听。
                                 * 断网时这一步会在首个 HTTP 请求快速失败，
                                 * 不影响 KWS 继续监听与本地工具。 */
                                if (g_kws_auto_voice) {
                                    if (started) {
                                        ioctl(fd, AUDIOIOC_STOP, 0);
                                        started = 0;
                                    }
                                    if (fd >= 0) {
                                        close(fd);
                                        fd = -1;
                                    }
                                    /* 让 audio 驱动把会话真正摘干净再重新
                                     * open：close 后立刻 open 有踩到驱动
                                     * head/tail 记账错位的风险（见
                                     * hm_voice_record 注释）。 */
                                    usleep(150000);
                                    /* 先给一句听得见的应答，等它播完再开麦，
                                     * 用户才知道该开口说指令。 */
                                    hm_voice_wake_prompt();
                                    usleep(HM_WAKE_PROMPT_WAIT_MS * 1000);
                                    hm_voice_utterance_roundtrip();
                                    if (kws_listen_open_mic(&fd) != 0) {
                                        printf("[KWS-L] mic reopen failed; "
                                               "stop listening\n");
                                        fflush(stdout);
                                        /* 置 0 让下一轮循环自然退出，
                                         * 保留 free(win)/free(ring) 的清理路径 */
                                        g_kws_listen_run = 0;
                                    } else {
                                        pfd.fd = fd;
                                        pfd.events = POLLIN;
                                        speech = 0;
                                        silence = 0;
                                        hold = 0;
                                    }
                                }
                                /* 唤醒事件在录音之后上报：MQTT 断线时
                                 * mqtt_channel_send 会走连接/超时路径，
                                 * 放在录音前会吞掉用户的指令。 */
                                {
                                    char ev[160];
                                    snprintf(ev, sizeof(ev),
                                             "{\"type\":\"kws_wake\","
                                             "\"keyword\":\"hello_openvela\","
                                             "\"score\":%.3f,\"hits\":%d}",
                                             score, g_kws_listen_hits);
                                    if (mqtt_channel_send("homemind", ev) == 0)
                                        printf("[KWS-L] mqtt published\n");
                                    else
                                        printf("[KWS-L] mqtt offline\n");
                                    fflush(stdout);
                                }
                                hm_lcd_show_text("KWS LISTEN");
                            }
                        }
                        free(win);
                    }
                    speech = 0;
                    silence = 0;
                }
            }
        }
    }
    if (started)
        ioctl(fd, AUDIOIOC_STOP, 0);
    if (fd >= 0)
        close(fd);
    free(ring);
    printf("[KWS-L] stopped frames=%d hits=%d last=%.3f\n",
           frames, g_kws_listen_hits, g_kws_listen_last);
    printf("[KWS-DBG] calls=%d alloc=%d enq=%d start=%d empty=%d\n",
           g_kws_dbg_calls, g_kws_dbg_alloc, g_kws_dbg_enq,
           g_kws_dbg_start, g_kws_dbg_empty);
    fflush(stdout);
    return NULL;
}

static void cmd_kws_listen(int argc, char **argv)
{
    const char *sub = (argc >= 2) ? argv[1] : "status";
    if (strcmp(sub, "voice") == 0) {
        int on = !(argc >= 3 && strcmp(argv[2], "off") == 0);
        g_kws_auto_voice = on;
        printf("[KWS-L] auto voice = %s\n", on ? "on" : "off");
        return;
    }
    if (strcmp(sub, "start") == 0) {
        if (g_kws_listen_run) {
            printf("[KWS-L] already running\n");
            return;
        }
        if (argc >= 3) {
            float t = (float)atof(argv[2]);
            if (t > 0.01f && t < 0.99f)
                g_kws_listen_thr_x100 = (int)(t * 100.0f + 0.5f);
        }
        if (argc >= 4) {
            int v = atoi(argv[3]);
            if (v > 50 && v < 20000)
                g_kws_listen_vad = v;
        }
        g_kws_listen_run = 1;
        g_kws_listen_hits = 0;
        g_kws_listen_frames = 0;
        g_kws_dbg_calls = g_kws_dbg_alloc = g_kws_dbg_enq = 0;
        g_kws_dbg_start = g_kws_dbg_empty = 0;
        /* 唤醒后要在本线程里跑一次完整的 HTTPS 往返（mbedTLS 握手 + 4KB 响应
         * 缓冲 + JSON 解析）。默认 pthread 栈放不下，实测表现是"唤醒一次后
         * 整机静默、云端收不到任何请求"。这里显式给足 32KB。 */
        {
            pthread_attr_t kattr;
            int rc;

            pthread_attr_init(&kattr);
            pthread_attr_setstacksize(&kattr, 32 * 1024);
            rc = pthread_create(&g_kws_listen_tid, &kattr,
                                kws_listen_worker, NULL);
            pthread_attr_destroy(&kattr);
            if (rc != 0) {
                printf("[KWS-L] thread create fail\n");
                g_kws_listen_run = 0;
                return;
            }
        }
        pthread_detach(g_kws_listen_tid);
        printf("[KWS-L] started thr=%.2f vad=%d\n",
               (float)g_kws_listen_thr_x100 / 100.0f, g_kws_listen_vad);
    } else if (strcmp(sub, "stop") == 0) {
        g_kws_listen_run = 0;
        printf("[KWS-L] stop requested\n");
    } else {
        printf("[KWS-L] run=%d hits=%d frames=%d last=%.3f thr=%.2f vad=%d\n",
               g_kws_listen_run, g_kws_listen_hits, g_kws_listen_frames,
               g_kws_listen_last,
               (float)g_kws_listen_thr_x100 / 100.0f, g_kws_listen_vad);
        printf("[KWS-DBG] calls=%d alloc=%d enq=%d start=%d empty=%d\n",
               g_kws_dbg_calls, g_kws_dbg_alloc, g_kws_dbg_enq,
               g_kws_dbg_start, g_kws_dbg_empty);
    }
}


/* ── Intent send via MQTT (home private cloud) ─────────────────
 * intent_send 我准备睡觉了
 * Publishes {"type":"intent","text":"..."} on homemind topic.
 * Offline: mqtt_channel_send fails, local tools still work.
 */
static void cmd_intent_send(int argc, char **argv)
{
    char text[200];
    char json[280];
    int i, n = 0;
    if (argc < 2) {
        printf("Usage: intent_send <text...>\n");
        return;
    }
    text[0] = 0;
    for (i = 1; i < argc && n < (int)sizeof(text) - 4; i++) {
        int l = snprintf(text + n, sizeof(text) - n, "%s%s",
                         n ? " " : "", argv[i]);
        if (l < 0)
            break;
        n += l;
    }
    for (i = 0; text[i]; i++) {
        if (text[i] == '"' || text[i] == '\\')
            text[i] = '\'';
    }
    snprintf(json, sizeof(json),
             "{\"type\":\"intent\",\"text\":\"%s\",\"device_id\":\"esp32s3-eye\"}",
             text);
    printf("[Intent] publish %s\n", json);
    fflush(stdout);
    if (mqtt_channel_send("homemind", json) == 0)
        printf("[Intent] MQTT ok\n");
    else
        printf("[Intent] MQTT offline — local tools only\n");
    fflush(stdout);
}

/* ── Periodic vision tick ───────────────────────────────────
 * Camera+TFLM must not overlap kws_listen. This loop only
 * heartbeats; run `vision local` when audio is idle.
 */
static volatile int g_vision_loop_run = 0;
static pthread_t g_vision_loop_tid;
static int g_vision_loop_sec = 10;

static void *vision_loop_worker(void *arg)
{
    (void)arg;
    printf("[VisLoop] start interval=%ds (run vision local when audio idle)\n",
           g_vision_loop_sec);
    fflush(stdout);
    while (g_vision_loop_run) {
        printf("[VisLoop] tick\n");
        fflush(stdout);
        sleep(g_vision_loop_sec > 0 ? g_vision_loop_sec : 10);
    }
    printf("[VisLoop] stopped\n");
    return NULL;
}

static void cmd_vision_loop(int argc, char **argv)
{
    const char *sub = (argc >= 2) ? argv[1] : "status";
    if (strcmp(sub, "start") == 0) {
        if (g_vision_loop_run) {
            printf("[VisLoop] already running\n");
            return;
        }
        if (argc >= 3) {
            int v = atoi(argv[2]);
            if (v >= 3 && v <= 300)
                g_vision_loop_sec = v;
        }
        g_vision_loop_run = 1;
        if (pthread_create(&g_vision_loop_tid, NULL, vision_loop_worker, NULL)) {
            printf("[VisLoop] thread fail\n");
            g_vision_loop_run = 0;
            return;
        }
        pthread_detach(g_vision_loop_tid);
        printf("[VisLoop] started sec=%d\n", g_vision_loop_sec);
    } else if (strcmp(sub, "stop") == 0) {
        g_vision_loop_run = 0;
        printf("[VisLoop] stop requested\n");
    } else {
        printf("[VisLoop] run=%d sec=%d\n", g_vision_loop_run, g_vision_loop_sec);
    }
}

static void cmd_wake_kws(int argc, char **argv)
{
    float thr = 0.55f;
    float score = 0;
    int rc;
    if (argc > 1) {
        float t = (float)atof(argv[1]);
        if (t > 0.01f && t < 0.99f)
            thr = t;
    }
    hm_lcd_show_text("LISTEN 3S...");
    printf("[KWS] capturing 3s...\n");
    fflush(stdout);
    rc = kws_capture_score(&score);
    if (rc != 0) {
        printf("[KWS] FAIL capture\n");
        hm_lcd_show_text("MIC FAIL");
        return;
    }
    printf("[KWS] score=%.3f thr=%.2f %s\n", score, thr,
           score >= thr ? "HIT" : "miss");
    if (score >= thr) {
        hm_lcd_show_text("WAKE! LED");
        hm_led_blink(3, 80, 80);
        printf("[KWS] LED blinked (local cmd)\n");
    } else {
        hm_lcd_show_text("NO WAKE");
    }
}


static void cmd_wake_loop(int argc, char **argv)
{
    int seconds = 8;
    float thr = 400.0f;
    int hold_need = 4;      /* ~80ms above thr */
    int hold_now = 0;
    int max_chunks;
    int chunk = 0;
    int wakes = 0;
    int cooldown = 0;
    int fd;
    int started = 0;
    struct audio_caps_desc_s caps;
    struct audio_buf_desc_s desc;
    struct ap_buffer_s *apb = NULL;
    struct pollfd pfd;
    int bsize = HM_WAKE_CHUNK;
    float rms_log[8];
    int rms_i = 0;

    if (argc > 1) {
        int v = atoi(argv[1]);
        if (v > 0 && v <= 60)
            seconds = v;
    }
    if (argc > 2) {
        float t = (float)atof(argv[2]);
        if (t > 0.0f && t < 30000.0f)
            thr = t;
    }
    if (argc > 3) {
        int h = atoi(argv[3]);
        if (h > 0 && h < 50)
            hold_need = h;
    }
    max_chunks = (seconds * HM_WAKE_RATE * 2) / bsize;
    if (max_chunks < 10)
        max_chunks = 10;
    if (max_chunks > HM_WAKE_RUN_MAX)
        max_chunks = HM_WAKE_RUN_MAX;

    printf("[Wake] listen %ds thr=%.0f hold=%d (~energy VAD, not KWS)\n",
           seconds, thr, hold_need);
    fflush(stdout);

    fd = open("/dev/audio/pcm_in0", O_RDWR | O_NONBLOCK);
    if (fd < 0) {
        printf("[Wake-ERR] open mic errno=%d\n", errno);
        return;
    }
    memset(&caps, 0, sizeof(caps));
    caps.caps.ac_len = sizeof(struct audio_caps_s);
    caps.caps.ac_type = AUDIO_TYPE_INPUT;
    caps.caps.ac_controls.w = HM_WAKE_RATE;
    caps.caps.ac_controls.b[2] = 16;
    caps.caps.ac_channels = 1;
    caps.caps.ac_format.hw = AUDIO_FMT_PCM;
    if (ioctl(fd, AUDIOIOC_CONFIGURE, (uintptr_t)&caps) < 0) {
        printf("[Wake-ERR] config\n");
        close(fd);
        return;
    }
    {
        struct ap_buffer_info_s info;
        memset(&info, 0, sizeof(info));
        if (ioctl(fd, AUDIOIOC_GETBUFFERINFO, (uintptr_t)&info) == 0)
            printf("[Wake] info nb=%u sz=%u\n", info.nbuffers, info.buffer_size);
    }
    memset(&pfd, 0, sizeof(pfd));
    pfd.fd = fd;
    pfd.events = POLLIN;

    while (chunk < max_chunks) {
        float rms;
        int w;

        apb = NULL;
        memset(&desc, 0, sizeof(desc));
        desc.numbytes = bsize;
        desc.u.pbuffer = &apb;
        if (ioctl(fd, AUDIOIOC_ALLOCBUFFER, (uintptr_t)&desc) !=
                (int)sizeof(desc) || !apb) {
            printf("[Wake-ERR] alloc errno=%d apb=%p\n", errno, apb);
            break;
        }
        memset(&desc, 0, sizeof(desc));
        desc.u.buffer = apb;
        desc.numbytes = bsize;
        if (ioctl(fd, AUDIOIOC_ENQUEUEBUFFER, (uintptr_t)&desc) < 0) {
            printf("[Wake-ERR] enq errno=%d\n", errno);
            memset(&desc, 0, sizeof(desc));
            desc.u.buffer = apb;
            ioctl(fd, AUDIOIOC_FREEBUFFER, (uintptr_t)&desc);
            break;
        }
        if (!started) {
            if (ioctl(fd, AUDIOIOC_START, 0) < 0) {
                printf("[Wake-ERR] start\n");
                break;
            }
            started = 1;
            poll(&pfd, 1, 800);
        } else {
            usleep(30000);
        }

        if (apb->nbytes > 0) {
            rms = hm_chunk_rms(apb->samp, apb->nbytes);
            rms_log[rms_i++ & 7] = rms;
            if (chunk % 25 == 0) {
                printf("[Wake] chunk=%d rms=%.1f thr=%.0f wakes=%d\n",
                       chunk, rms, thr, wakes);
                fflush(stdout);
            }
            if (cooldown > 0)
                cooldown--;
            else {
                w = hm_wake_score(rms, thr, &hold_need, &hold_now);
                if (w) {
                    wakes++;
                    printf("[Wake] WAKE #%d rms=%.1f -> LED blink\n",
                           wakes, rms);
                    fflush(stdout);
                    hm_led_blink(3, 80, 80);
                    cooldown = 25; /* ~0.5s refractory */
                }
            }
        }
        memset(&desc, 0, sizeof(desc));
        desc.u.buffer = apb;
        ioctl(fd, AUDIOIOC_FREEBUFFER, (uintptr_t)&desc);
        chunk++;
    }
    if (started)
        ioctl(fd, AUDIOIOC_STOP, 0);
    close(fd);
    printf("[Wake] DONE chunks=%d wakes=%d thr=%.0f\n", chunk, wakes, thr);
}


static void cmd_media_probe(int argc, char** argv)
{
    const char* video_path = "/dev/video0";
    const char* audio_path = "/dev/audio/pcm_in0";
    const size_t video_bytes = 320 * 240 * 2;
    unsigned char* frames[3] = { NULL, NULL, NULL };
    int video_fd = -1;
    int audio_fd = -1;
    int audio_ret;
    int audio_started = 0;
    int video_ret;
    int streaming = 0;
    struct v4l2_format fmt;
    struct v4l2_requestbuffers req;
    struct v4l2_buffer buf;
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    struct pollfd pfd;
    struct audio_caps_desc_s audio_caps;
    struct audio_buf_desc_s audio_desc;
    struct ap_buffer_info_s audio_info;
    struct ap_buffer_s* audio_buffers[4] = { NULL, NULL, NULL, NULL };
    unsigned int audio_count = 0;
    unsigned int audio_index;
    unsigned int audio_completed;
    unsigned int audio_polls;
    int audio_safe_to_free = 0;
    unsigned char audio[640];
    int force_jpeg;
    int use_jpeg = 0;

    force_jpeg = (argc >= 2 && strcmp(argv[1], "jpeg") == 0);

    printf("MEDIA_PROBE_BEGIN video=%s audio=%s\n", video_path,
           audio_path);
    fflush(stdout);

    printf("MEDIA_VIDEO_STAGE open_enter\n");
    fflush(stdout);
    video_fd = open(video_path, O_RDWR | O_NONBLOCK);
    printf("MEDIA_VIDEO_STAGE open_exit fd=%d errno=%d\n", video_fd, errno);
    fflush(stdout);
    if (video_fd < 0) {
        printf("MEDIA_VIDEO_FRAME_FAIL stage=open errno=%d\n", errno);
    } else {
        printf("MEDIA_VIDEO_STAGE alloc_enter\n");
        fflush(stdout);
        frames[0] = memalign(32, video_bytes);
        frames[1] = memalign(32, video_bytes);
        frames[2] = memalign(32, video_bytes);
        printf("MEDIA_VIDEO_STAGE alloc_exit f0=%p f1=%p f2=%p\n",
               frames[0], frames[1], frames[2]);
        fflush(stdout);
        if (!frames[0] || !frames[1] || !frames[2]) {
            printf("MEDIA_VIDEO_FRAME_FAIL stage=alloc errno=%d\n", ENOMEM);
        } else {
            if (force_jpeg) {
                /* Only request JPEG when the sensor enumerates it; the
                 * generic V4L2 layer would otherwise accept S_FMT(JPEG)
                 * even for an RGB-only sensor. */

                struct v4l2_fmtdesc fd;
                int has_jpeg = 0;
                int fidx;

                printf("MEDIA_VIDEO_STAGE enum_fmt_enter\n");
                fflush(stdout);
                for (fidx = 0; fidx < 8 && !has_jpeg; fidx++) {
                    memset(&fd, 0, sizeof(fd));
                    fd.index = fidx;
                    fd.type = type;
                    if (ioctl(video_fd, VIDIOC_ENUM_FMT,
                              (uintptr_t)&fd) < 0) {
                        break;
                    }
                    if (fd.pixelformat == V4L2_PIX_FMT_JPEG) {
                        has_jpeg = 1;
                    }
                }
                printf("MEDIA_VIDEO_STAGE enum_fmt_exit has_jpeg=%d index=%d\n",
                       has_jpeg, fidx);
                fflush(stdout);
                if (has_jpeg) {
                    memset(&fmt, 0, sizeof(fmt));
                    fmt.type = type;
                    fmt.fmt.pix.width = 320;
                    fmt.fmt.pix.height = 240;
                    fmt.fmt.pix.field = V4L2_FIELD_ANY;
                    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_JPEG;
                    fmt.fmt.pix.sizeimage = video_bytes;
                    video_ret = ioctl(video_fd, VIDIOC_S_FMT,
                                      (uintptr_t)&fmt);
                    if (video_ret >= 0) {
                        use_jpeg = 1;
                    }
                }
            }
            if (!use_jpeg) {
                /* JPEG unsupported by this driver: fall back to RGB565 */

                memset(&fmt, 0, sizeof(fmt));
                fmt.type = type;
                fmt.fmt.pix.width = 320;
                fmt.fmt.pix.height = 240;
                fmt.fmt.pix.field = V4L2_FIELD_ANY;
                fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_RGB565;
                video_ret = ioctl(video_fd, VIDIOC_S_FMT, (uintptr_t)&fmt);
            }
            printf("MEDIA_VIDEO_STAGE s_fmt_exit ret=%d jpeg=%d errno=%d\n",
                   video_ret, use_jpeg, errno);
            fflush(stdout);
            if (video_ret < 0) {
                printf("MEDIA_VIDEO_FRAME_FAIL stage=s_fmt errno=%d\n",
                       errno);
            } else {
                memset(&req, 0, sizeof(req));
                req.type = type;
                req.memory = V4L2_MEMORY_USERPTR;
                req.count = 3;
                req.mode = V4L2_BUF_MODE_RING;
                printf("MEDIA_VIDEO_STAGE reqbufs_enter\n");
                fflush(stdout);
                video_ret = ioctl(video_fd, VIDIOC_REQBUFS,
                                  (uintptr_t)&req);
                printf("MEDIA_VIDEO_STAGE reqbufs_exit ret=%d count=%u errno=%d\n",
                       video_ret, req.count, errno);
                fflush(stdout);
                if (video_ret < 0) {
                    printf("MEDIA_VIDEO_FRAME_FAIL stage=reqbufs errno=%d\n",
                           errno);
                } else {
                    video_ret = 0;
                    memset(&buf, 0, sizeof(buf));
                    for (buf.index = 0; buf.index < 3; buf.index++) {
                        buf.type = type;
                        buf.memory = V4L2_MEMORY_USERPTR;
                        buf.m.userptr = (uintptr_t)frames[buf.index];
                        buf.length = video_bytes;
                        video_ret = ioctl(video_fd, VIDIOC_QBUF,
                                          (uintptr_t)&buf);
                        if (video_ret < 0) {
                            break;
                        }
                    }
                    printf("MEDIA_VIDEO_STAGE qbuf_exit ret=%d index=%u errno=%d\n",
                           video_ret, (unsigned int)buf.index, errno);
                    fflush(stdout);
                    if (video_ret < 0) {
                        printf("MEDIA_VIDEO_FRAME_FAIL stage=qbuf index=%u errno=%d\n",
                               (unsigned int)buf.index, errno);
                    } else {
                        printf("MEDIA_VIDEO_STAGE streamon_enter\n");
                        fflush(stdout);
                        video_ret = ioctl(video_fd, VIDIOC_STREAMON,
                                          (uintptr_t)&type);
                        printf("MEDIA_VIDEO_STAGE streamon_exit ret=%d errno=%d\n",
                               video_ret, errno);
                        fflush(stdout);
                        if (video_ret < 0) {
                            printf("MEDIA_VIDEO_FRAME_FAIL stage=streamon errno=%d\n",
                                   errno);
                        } else {
                            streaming = 1;
                            memset(&pfd, 0, sizeof(pfd));
                            pfd.fd = video_fd;
                            pfd.events = POLLIN;
                            printf("MEDIA_VIDEO_STAGE poll_enter\n");
                            fflush(stdout);
                            video_ret = poll(&pfd, 1, 5000);
                            printf("MEDIA_VIDEO_STAGE poll_exit ret=%d revents=0x%x errno=%d\n",
                                   video_ret, pfd.revents, errno);
                            fflush(stdout);
                            if (video_ret <= 0) {
                                printf("MEDIA_VIDEO_FRAME_FAIL stage=poll ret=%d revents=0x%x errno=%d\n",
                                       video_ret, pfd.revents, errno);
                            } else {
                                memset(&buf, 0, sizeof(buf));
                                buf.type = type;
                                buf.memory = V4L2_MEMORY_USERPTR;
                                printf("MEDIA_VIDEO_STAGE dqbuf_enter\n");
                                fflush(stdout);
                                video_ret = ioctl(video_fd, VIDIOC_DQBUF,
                                                  (uintptr_t)&buf);
                                printf("MEDIA_VIDEO_STAGE dqbuf_exit ret=%d bytes=%u errno=%d\n",
                                       video_ret, (unsigned int)buf.bytesused, errno);
                                fflush(stdout);
                                if (video_ret < 0) {
                                    printf("MEDIA_VIDEO_FRAME_FAIL stage=dqbuf errno=%d\n",
                                           errno);
                                } else {
                                    FAR const unsigned char* frame =
                                        (FAR const unsigned char*)
                                        (uintptr_t)buf.m.userptr;
                                    if (use_jpeg) {
                                        unsigned int head_i;
                                        unsigned int scan_i;
                                        int soi_off = -1;

                                        /* JPEG is only claimed when the
                                         * frame really starts with the
                                         * SOI marker ff d8. */

                                        for (scan_i = 0;
                                             frame != NULL &&
                                             scan_i + 1 < buf.bytesused;
                                             scan_i++) {
                                            if (frame[scan_i] == 0xff &&
                                                frame[scan_i + 1] == 0xd8) {
                                                soi_off = (int)scan_i;
                                                break;
                                            }
                                        }
                                        if (soi_off == 0) {
                                            printf("MEDIA_VIDEO_FRAME_JPEG bytes=%u marker=ffd8\n",
                                                   (unsigned int)buf.bytesused);
                                        } else {
                                            printf("MEDIA_VIDEO_FRAME_RAW bytes=%u marker=%02x%02x\n",
                                                   (unsigned int)buf.bytesused,
                                                   buf.bytesused >= 1 ?
                                                   frame[0] : 0,
                                                   buf.bytesused >= 2 ?
                                                   frame[1] : 0);
                                        }
                                        printf("MEDIA_VIDEO_HEAD8 hex=");
                                        for (head_i = 0;
                                             head_i < 8 &&
                                             head_i < buf.bytesused;
                                             head_i++) {
                                            printf("%s%02x",
                                                   head_i == 0 ? "" : " ",
                                                   frame[head_i]);
                                        }
                                        printf("\n");
                                        printf("MEDIA_VIDEO_SOI off=%d\n",
                                               soi_off);
                                    } else {
                                        printf("MEDIA_VIDEO_FRAME bytes=%u checksum=%lu sequence=%u\n",
                                               (unsigned int)buf.bytesused,
                                               media_checksum(frame,
                                                              buf.bytesused < 256 ?
                                                              buf.bytesused : 256),
                                               (unsigned int)buf.sequence);
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    if (streaming) {
        ioctl(video_fd, VIDIOC_STREAMOFF, (uintptr_t)&type);
    }
    if (frames[0]) {
        free(frames[0]);
    }
    if (frames[1]) {
        free(frames[1]);
    }
    if (frames[2]) {
        free(frames[2]);
    }
    if (video_fd >= 0) {
        close(video_fd);
    }

    audio_fd = open(audio_path, O_RDWR | O_NONBLOCK);
    if (audio_fd < 0) {
        printf("MEDIA_AUDIO_PCM_FAIL stage=open errno=%d\n", errno);
    } else {
        memset(&audio_caps, 0, sizeof(audio_caps));
        audio_caps.caps.ac_len = sizeof(struct audio_caps_s);
        audio_caps.caps.ac_type = AUDIO_TYPE_INPUT;
        audio_caps.caps.ac_controls.w = 16000;
        audio_caps.caps.ac_controls.b[2] = 16;
        audio_caps.caps.ac_channels = 1;
        audio_caps.caps.ac_format.hw = AUDIO_FMT_PCM;
        printf("MEDIA_AUDIO_STAGE config\n");
        fflush(stdout);
        audio_ret = ioctl(audio_fd, AUDIOIOC_CONFIGURE,
                          (uintptr_t)&audio_caps);
        if (audio_ret < 0) {
            printf("MEDIA_AUDIO_PCM_FAIL stage=config ret=%d errno=%d\n",
                   audio_ret, errno);
        } else {
            memset(&audio_info, 0, sizeof(audio_info));
            audio_ret = ioctl(audio_fd, AUDIOIOC_GETBUFFERINFO,
                              (uintptr_t)&audio_info);
            printf("MEDIA_AUDIO_STAGE info nbuffers=%u buffer=%u ret=%d\n",
                   (unsigned int)audio_info.nbuffers,
                   (unsigned int)audio_info.buffer_size, audio_ret);
            fflush(stdout);
            if (audio_ret < 0) {
                printf("MEDIA_AUDIO_PCM_FAIL stage=info ret=%d errno=%d\n",
                       audio_ret, errno);
            } else {
                audio_count = audio_info.nbuffers;
                /* This command is a finite capture probe, not a streaming
                 * consumer.  Keep one short, 4-byte-aligned DMA transfer so
                 * the stock I2S lower half can drain and release it cleanly
                 * before the next probe. */
                if (audio_count > 1) {
                    audio_count = 1;
                }
                printf("MEDIA_AUDIO_STAGE alloc count=%u\n", audio_count);
                fflush(stdout);
                audio_ret = 0;
                for (audio_index = 0; audio_index < audio_count;
                     audio_index++) {
                    memset(&audio_desc, 0, sizeof(audio_desc));
                    audio_desc.numbytes = sizeof(audio);
                    audio_desc.u.pbuffer = &audio_buffers[audio_index];
                    audio_ret = ioctl(audio_fd, AUDIOIOC_ALLOCBUFFER,
                                      (uintptr_t)&audio_desc);
                    if (audio_ret != sizeof(audio_desc) ||
                        !audio_buffers[audio_index]) {
                        printf("MEDIA_AUDIO_PCM_FAIL stage=alloc index=%u ret=%d errno=%d\n",
                               audio_index, audio_ret, errno);
                        break;
                    }
                }
                if (audio_index == audio_count) {
                    printf("MEDIA_AUDIO_STAGE enqueue count=%u\n",
                           audio_count);
                    fflush(stdout);
                    for (audio_index = 0; audio_index < audio_count;
                         audio_index++) {
                        memset(&audio_desc, 0, sizeof(audio_desc));
                        audio_desc.u.buffer = audio_buffers[audio_index];
                        audio_desc.numbytes = sizeof(audio);
                        audio_ret = ioctl(audio_fd, AUDIOIOC_ENQUEUEBUFFER,
                                          (uintptr_t)&audio_desc);
                        if (audio_ret < 0) {
                            printf("MEDIA_AUDIO_PCM_FAIL stage=enqueue index=%u ret=%d errno=%d\n",
                                   audio_index, audio_ret, errno);
                            break;
                        }
                    }
                }
                if (audio_index == audio_count) {
                    printf("MEDIA_AUDIO_STAGE start\n");
                    fflush(stdout);
                    audio_ret = ioctl(audio_fd, AUDIOIOC_START, 0);
                    if (audio_ret < 0) {
                        printf("MEDIA_AUDIO_PCM_FAIL stage=start ret=%d errno=%d\n",
                               audio_ret, errno);
                    } else {
                        audio_started = 1;
                        printf("MEDIA_AUDIO_STAGE poll\n");
                        fflush(stdout);
                        audio_completed = 0;
                        audio_polls = 0;
                        memset(&pfd, 0, sizeof(pfd));
                        pfd.fd = audio_fd;
                        pfd.events = POLLIN;
                        while (audio_completed < audio_count &&
                               audio_polls++ < 8) {
                            audio_completed = 0;
                            for (audio_index = 0; audio_index < audio_count;
                                 audio_index++) {
                                if (audio_buffers[audio_index] &&
                                    audio_buffers[audio_index]->nbytes > 0) {
                                    audio_completed++;
                                }
                            }
                            if (audio_completed == audio_count) {
                                break;
                            }
                            video_ret = poll(&pfd, 1, 1000);
                            if (video_ret <= 0) {
                                break;
                            }
                        }
                        if (audio_completed < audio_count) {
                            printf("MEDIA_AUDIO_PCM_FAIL stage=drain completed=%u/%u ret=%d revents=0x%x errno=%d\n",
                                   audio_completed, audio_count, video_ret,
                                   pfd.revents, errno);
                        } else {
                            audio_safe_to_free = 1;
                            audio_ret = 0;
                            for (audio_index = 0; audio_index < audio_count;
                                 audio_index++) {
                                if (audio_buffers[audio_index] &&
                                    audio_buffers[audio_index]->nbytes > 0) {
                                    audio_ret = audio_buffers[audio_index]->nbytes;
                                    memcpy(audio,
                                           audio_buffers[audio_index]->samp,
                                           audio_ret < (int)sizeof(audio) ?
                                           (size_t)audio_ret : sizeof(audio));
                                    break;
                                }
                            }
                            if (audio_ret == 0) {
                                printf("MEDIA_AUDIO_PCM_FAIL stage=buffer_empty revents=0x%x errno=%d\n",
                                       pfd.revents, errno);
                            } else {
                                printf("MEDIA_AUDIO_PCM bytes=%d buffer=%u checksum=%lu\n",
                                       audio_ret, audio_index,
                                       media_checksum(audio, audio_ret < 256 ?
                                                      (size_t)audio_ret : 256));
                            }
                        }
                    }
                }
            }
        }
        if (audio_started) {
            ioctl(audio_fd, AUDIOIOC_STOP, 0);
        }
        if (audio_safe_to_free) {
            for (audio_index = 0; audio_index < audio_count; audio_index++) {
                if (!audio_buffers[audio_index]) {
                    continue;
                }
                memset(&audio_desc, 0, sizeof(audio_desc));
                audio_desc.u.buffer = audio_buffers[audio_index];
                ioctl(audio_fd, AUDIOIOC_FREEBUFFER,
                      (uintptr_t)&audio_desc);
            }
        }
        close(audio_fd);
    }

    printf("MEDIA_PROBE_DONE\n");
    fflush(stdout);
}

static void cmd_net_test(int argc, char** argv)
{
    char resp[1024];
    const char* host = argc >= 2 ? argv[1] : "www.baidu.com";
    const char* port = argc >= 3 ? argv[2] : "443";
    printf("Testing HTTPS to %s:%s...\n", host, port);
    int status = vela_https_get(host, port, "/", resp, sizeof(resp));
    if (status > 0) {
        printf("SUCCESS! HTTP Status: %d\n", status);
    } else {
        printf("FAILED! TLS Error: 0x%x\n", -status);
    }
}

/* Temporary hardware diagnostic: isolate payload-size handling below TLS.
 * The paired Ubuntu echo server returns exactly what this command sends. */
static void cmd_tcp_probe(int argc, char** argv)
{
    if (argc < 4) {
        printf("Usage: tcp_probe <host> <port> <bytes>\n");
        return;
    }

    long requested = strtol(argv[3], NULL, 10);
    if (requested < 1 || requested > 1400) {
        printf("tcp_probe bytes must be in 1..1400\n");
        return;
    }

    struct addrinfo hints;
    struct addrinfo* result = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    int gai = getaddrinfo(argv[1], argv[2], &hints, &result);
    if (gai != 0 || result == NULL) {
        printf("[TCP-PROBE] resolve failed gai=%d errno=%d\n", gai, errno);
        return;
    }

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        printf("[TCP-PROBE] socket failed errno=%d\n", errno);
        freeaddrinfo(result);
        return;
    }

    struct timeval timeout = { .tv_sec = 5, .tv_usec = 0 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    errno = 0;
    if (connect(fd, result->ai_addr, result->ai_addrlen) != 0) {
        printf("[TCP-PROBE] connect failed errno=%d\n", errno);
        freeaddrinfo(result);
        close(fd);
        return;
    }
    freeaddrinfo(result);

    size_t length = (size_t)requested;
    unsigned char* tx = malloc(length);
    unsigned char* rx = malloc(length);
    if (tx == NULL || rx == NULL) {
        printf("[TCP-PROBE] allocation failed\n");
        free(tx);
        free(rx);
        close(fd);
        return;
    }

    for (size_t i = 0; i < length; i++) {
        tx[i] = (unsigned char)('A' + (i % 26));
    }

    /* Port 80 mode emits a syntactically valid request with an exact total
     * length.  A public HTTP response proves that the requested payload size
     * left the Wi-Fi device and was ACKed, even when same-subnet ARP is broken
     * on this board. */
    if (strcmp(argv[2], "80") == 0) {
        char prefix[192];
        static const char suffix[] = "\r\nConnection: close\r\n\r\n";
        int prefix_len = snprintf(prefix, sizeof(prefix),
            "GET / HTTP/1.0\r\nHost: %s\r\nX-Pad: ", argv[1]);
        size_t suffix_len = sizeof(suffix) - 1;
        if (prefix_len < 0 || (size_t)prefix_len + suffix_len > length) {
            printf("[TCP-PROBE] bytes=%zu too small for HTTP probe\n", length);
            free(tx);
            free(rx);
            close(fd);
            return;
        }
        memcpy(tx, prefix, (size_t)prefix_len);
        memset(tx + prefix_len, 'P', length - (size_t)prefix_len - suffix_len);
        memcpy(tx + length - suffix_len, suffix, suffix_len);
    }

    errno = 0;
    ssize_t sent = send(fd, tx, length, 0);
    int send_errno = errno;
    errno = 0;
    ssize_t received = recv(fd, rx, length, 0);
    int recv_errno = errno;
    int match = received == sent && received > 0 &&
        memcmp(tx, rx, (size_t)received) == 0;

    printf("[TCP-PROBE] bytes=%zu sent=%zd send_errno=%d recv=%zd recv_errno=%d match=%d\n",
        length, sent, send_errno, received, recv_errno, match);

    free(tx);
    free(rx);
    close(fd);
}

static void cmd_tls_replay(int argc, char** argv)
{
    if (argc < 3) {
        printf("Usage: tls_replay <host> <port>\n");
        return;
    }
    vela_tls_diag_replay(argv[1], argv[2]);
}

static void cmd_net_status(void)
{
    printf("Network connected: %s\n", network_is_connected() ? "yes" : "no");
    printf("IP: %s\n", network_get_ip());
#ifdef CONFIG_AI_AGENT_NET_RPMSG
    printf("State: %s\n",
        network_get_state() == NET_STATE_CONNECTED ? "CONNECTED" : "DISCONNECTED");
    printf("Active TCP conns: %d/%d\n",
        network_get_active_conns(), NET_MAX_TCP_CONNS);
    printf("IOB usage: %d%%\n", network_get_iob_usage());
    printf("Proxy mode: %s\n", network_get_proxy_mode());
    printf("RPMSG CPU: %s\n", network_get_rpmsg_cpu());
    /* Show DNS from resolv.conf */
    {
        FILE* fp = fopen("/tmp/resolv.conf", "r");
        if (fp) {
            char line[128];
            while (fgets(line, sizeof(line), fp)) {
                line[strcspn(line, "\n")] = '\0';
                printf("DNS: %s\n", line);
            }
            fclose(fp);
        }
    }
#endif
}

#ifdef CONFIG_AI_AGENT_NET_RPMSG
static void cmd_net_diag(int argc, char** argv)
{
    if (argc < 2) {
        /* Basic diagnostics */
        printf("=== Network Diagnostics ===\n");
        printf("Interface: %s\n",
            network_is_connected() ? "UP" : "DOWN");
        printf("IP: %s\n", network_get_ip());
        printf("State: %s\n",
            network_get_state() == NET_STATE_CONNECTED
                ? "CONNECTED"
                : "DISCONNECTED");
        printf("IOB: %d%%\n", network_get_iob_usage());
        printf("===========================\n");
        return;
    }

    if (strcmp(argv[1], "ping") == 0 && argc >= 3) {
        char cmd[128];
        snprintf(cmd, sizeof(cmd), "ping -c 3 %s", argv[2]);
        system(cmd);
    } else if (strcmp(argv[1], "http") == 0 && argc >= 3) {
        printf("HTTP HEAD %s ...\n", argv[2]);
        /* Simple connectivity test using existing TLS layer */
        char resp[256] = { 0 };
        struct timespec t0, t1;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        int status = vela_https_get(argv[2], "443", "/",
            resp, sizeof(resp));
        clock_gettime(CLOCK_MONOTONIC, &t1);
        long ms = (t1.tv_sec - t0.tv_sec) * 1000L
            + (t1.tv_nsec - t0.tv_nsec) / 1000000L;
        printf("Status: %d, Latency: %ldms\n", status, ms);
    } else {
        printf("Usage: net_diag [ping <host> | http <host>]\n");
    }
}
#endif

static void cmd_memory_read(void)
{
    char* buf = malloc(4096);
    if (!buf) {
        printf("Out of memory.\n");
        return;
    }
    if (memory_read_long_term(buf, 4096) == OK && buf[0]) {
        printf("=== MEMORY.md ===\n%s\n=================\n", buf);
    } else {
        printf("MEMORY.md is empty or not found.\n");
    }
    free(buf);
}

static void cmd_memory_write(int argc, char** argv)
{
    if (argc < 2) {
        printf("Usage: memory_write <content>\n");
        return;
    }
    memory_write_long_term(argv[1]);
    printf("MEMORY.md updated.\n");
}

static void cmd_session_list(void)
{
    printf("Sessions:\n");
    session_list();
}

static void cmd_session_clear(int argc, char** argv)
{
    if (argc < 2) {
        printf("Usage: session_clear <chat_id> | session_clear_all\n");
        return;
    }
    if (session_clear(argv[1]) == OK) {
        printf("Session cleared.\n");
    } else {
        printf("Session not found.\n");
    }
}

static void cmd_session_clear_all(void)
{
    session_clear_all();
    printf("All sessions cleared.\n");
}

static void cmd_heap_info(void)
{
    struct mallinfo mi = mallinfo();
    printf("Heap: arena=%d fordblks(free)=%d uordblks(used)=%d\n",
        mi.arena, mi.fordblks, mi.uordblks);
}

static void cmd_set_proxy(int argc, char** argv)
{
    if (argc < 3) {
        printf("Usage: set_proxy <host> <port>\n");
        return;
    }
    uint16_t port = (uint16_t)atoi(argv[2]);
    http_proxy_set(argv[1], port);
    printf("Proxy set to %s:%d.\n", argv[1], port);
}

static void cmd_clear_proxy(void)
{
    http_proxy_clear();
    printf("Proxy cleared. Restart to apply.\n");
}

static void cmd_set_wifi(int argc, char** argv)
{
    if (argc < 3) {
        printf("Usage: set_wifi <ssid> <password>\n");
        return;
    }
    printf("Connecting to '%s' ...\n", argv[1]);
    int err = network_wifi_connect(NULL, argv[1], argv[2]);
    if (err == OK)
        printf("WiFi connected: %s\n", network_get_ip());
    else
        printf("WiFi failed. Check SSID/password or run net_status.\n");
}

static void cmd_wifi_reconnect(void)
{
    printf("Reconnecting WiFi ...\n");
    int err = network_wifi_reconnect();
    if (err == OK)
        printf("Reconnected: %s\n", network_get_ip());
    else if (err == ERROR)
        printf("No saved credentials. Use: set_wifi <ssid> <pass>\n");
    else
        printf("WiFi reconnect failed.\n");
}

#ifdef CONFIG_AI_AGENT_NET_RPMSG
static void cmd_net_reconnect(void)
{
    printf("Reconnecting...\n");
    int ret = network_reconnect();
    printf("Reconnect %s\n", ret == OK ? "triggered" : "failed");
}
#endif

#ifdef CONFIG_AI_AGENT_NET_RPMSG
static void cmd_set_netproxy(int argc, char** argv)
{
    if (argc < 2) {
        printf("Usage: set_netproxy <usrsock|tun> [cpu_name]\n");
        return;
    }
    const char* cpu = (argc >= 3) ? argv[2] : NULL;
    int ret = network_save_proxy_config(argv[1], cpu);
    if (ret == OK) {
        printf("Proxy config saved: mode=%s cpu=%s\n",
            argv[1], cpu ? cpu : "(default)");
    } else {
        printf("Failed to save proxy config: %d\n", ret);
    }
}
#endif

#ifdef CONFIG_AI_AGENT_NET_RPMSG
static void cmd_set_dns(int argc, char** argv)
{
    if (argc < 2) {
        printf("Usage: set_dns <primary> [secondary]\n");
        return;
    }
    const char* secondary = (argc >= 3) ? argv[2] : NULL;
    int ret = network_set_dns(argv[1], secondary);
    if (ret == OK) {
        printf("DNS configured: %s %s\n",
            argv[1], secondary ? secondary : "");
    } else {
        printf("Failed to set DNS: %d\n", ret);
    }
}
#endif

static void cmd_set_search_key(int argc, char** argv)
{
    if (argc < 2) {
        printf("Usage: set_search_key <key>\n");
        return;
    }
    tool_web_search_set_serp_key(argv[1]);
    printf("SerpAPI (Google) key saved.\n");
}

static void cmd_set_exa_key(int argc, char** argv)
{
    if (argc < 2) {
        printf("Usage: set_exa_key <key>\n");
        return;
    }
    tool_web_search_set_exa_key(argv[1]);
    printf("Exa AI key saved.\n");
}

static void cmd_set_tavily_key(int argc, char** argv)
{
    if (argc < 2) {
        printf("Usage: set_tavily_key <key>\n");
        return;
    }
    tool_web_search_set_tavily_key(argv[1]);
    printf("Tavily key saved.\n");
}

static void cmd_set_news_key(int argc, char** argv)
{
    if (argc < 2) {
        printf("Usage: set_news_key <key>\n");
        return;
    }
    tool_web_search_set_news_key(argv[1]);
    printf("NewsAPI key saved.\n");
}

static void cmd_config_show(void)
{
    char val[128] = { 0 };

    printf("=== Current Configuration ===\n");

#define SHOW_CFG(label, key, mask)                                        \
    do {                                                                  \
        memset(val, 0, sizeof(val));                                      \
        if (claw_config_get((key), val, sizeof(val)) != OK || !val[0])    \
            strcpy(val, "(not set)");                                     \
        if ((mask) && strlen(val) > 6 && strcmp(val, "(not set)") != 0) { \
            char masked[128];                                             \
            snprintf(masked, sizeof(masked), "%.4s****", val);            \
            printf("  %-14s: %s\n", (label), masked);                     \
        } else {                                                          \
            printf("  %-14s: %s\n", (label), val);                        \
        }                                                                 \
    } while (0)

    SHOW_CFG("Feishu AppID", AGENT_CFG_KEY_FEISHU_APP_ID, false);
    SHOW_CFG("Feishu Secret", AGENT_CFG_KEY_FEISHU_APP_SECRET, true);
    SHOW_CFG("API Key", AGENT_CFG_KEY_API_KEY, true);
    SHOW_CFG("Model", AGENT_CFG_KEY_MODEL, false);
    SHOW_CFG("LLM Host", AGENT_CFG_KEY_LLM_HOST, false);
    SHOW_CFG("LLM Path", AGENT_CFG_KEY_LLM_PATH, false);
    SHOW_CFG("Vision Model", AGENT_CFG_KEY_VISION_MODEL, false);
    SHOW_CFG("Vision Host", AGENT_CFG_KEY_VISION_HOST, false);
    SHOW_CFG("Vision Key", AGENT_CFG_KEY_VISION_API_KEY, true);
    SHOW_CFG("Proxy Host", AGENT_CFG_KEY_PROXY_HOST, false);
    SHOW_CFG("Proxy Port", AGENT_CFG_KEY_PROXY_PORT, false);
    SHOW_CFG("SerpAPI Key", AGENT_CFG_KEY_SERP_KEY, true);
    SHOW_CFG("Exa Key", AGENT_CFG_KEY_EXA_KEY, true);
    SHOW_CFG("Tavily Key", AGENT_CFG_KEY_TAVILY_KEY, true);
    SHOW_CFG("News Key", AGENT_CFG_KEY_NEWS_KEY, true);
    SHOW_CFG("Tavily Key", AGENT_CFG_KEY_TAVILY_KEY, true);
    SHOW_CFG("Gateway", AGENT_CFG_KEY_GATEWAY_HOST, false);
    SHOW_CFG("GW Port", AGENT_CFG_KEY_GATEWAY_PORT, false);
    SHOW_CFG("GW Token", AGENT_CFG_KEY_GATEWAY_TOKEN, true);
    SHOW_CFG("MQTT Broker", AGENT_CFG_KEY_MQTT_BROKER, false);
    SHOW_CFG("Volc AppKey", AGENT_CFG_KEY_VOLC_APPKEY, true);
    SHOW_CFG("Volc Token", AGENT_CFG_KEY_VOLC_TOKEN, true);
    SHOW_CFG("Volc API Key", AGENT_CFG_KEY_VOLC_API_KEY, true);
    SHOW_CFG("Volc Speaker", AGENT_CFG_KEY_VOLC_SPEAKER, false);

#undef SHOW_CFG

    printf("Network: %s / %s\n",
        network_is_connected() ? "connected" : "disconnected",
        network_get_ip());
#ifdef CONFIG_AI_AGENT_NET_RPMSG
    printf("Net Proxy: %s (CPU: %s)\n",
        network_get_proxy_mode(), network_get_rpmsg_cpu());
    printf("Net Timeout: connect=%ds read=%ds\n",
        network_get_connect_timeout(), network_get_read_timeout());
    printf("Net Retry: max=%d base=%ds\n",
        network_get_retry_max(), network_get_retry_base_sec());
#endif
    printf("=============================\n");
}

static void cmd_config_reset(void)
{
    config_erase_all();
    printf("All runtime config cleared. Build-time defaults will be used on restart.\n");
}

#ifdef CONFIG_AI_AGENT_BLE_GATT
#include "infra/ble_cmd_handler.h"
#include "infra/ble_gatt.h"

static void ble_gatt_test_conn_cb(bool connected, void* user_data)
{
    (void)user_data;
    printf("[ble_gatt_test] Connection %s\n",
        connected ? "established" : "lost");
}

static void cmd_ble_gatt_test(int argc, char** argv)
{
    const char* sub = (argc > 1) ? argv[1] : "init";

    if (strcmp(sub, "init") == 0) {
        ble_gatt_config_t cfg = {
            .recv_cb = ble_cmd_handler_recv,
            .conn_cb = ble_gatt_test_conn_cb,
            .user_data = NULL,
        };
        int ret = ble_gatt_init(&cfg);
        printf("[ble_gatt_test] init: %s (%d)\n",
            ret == 0 ? "OK" : "FAILED", ret);
    } else if (strcmp(sub, "status") == 0) {
        printf("[ble_gatt_test] connected=%d mtu=%u\n",
            ble_gatt_is_connected(), ble_gatt_get_mtu());
    } else if (strcmp(sub, "send") == 0) {
        const char* msg = (argc > 2) ? argv[2] : "hello from AI Agent";
        int ret = ble_gatt_send((const uint8_t*)msg, strlen(msg));
        printf("[ble_gatt_test] send: %s (%d)\n",
            ret > 0 ? "OK" : "FAILED", ret);
    } else if (strcmp(sub, "deinit") == 0) {
        int ret = ble_gatt_deinit();
        printf("[ble_gatt_test] deinit: %s (%d)\n",
            ret == 0 ? "OK" : "FAILED", ret);
    } else {
        printf("Usage: ble_gatt_test [init|status|send <msg>|deinit]\n");
    }
}
#endif /* CONFIG_AI_AGENT_BLE_GATT */

static void cmd_restart(void)
{
    printf("Restarting...\n");
    fflush(stdout);
#ifdef CONFIG_BOARDCTL_RESET
    boardctl(BOARDIOC_RESET, 0);
#else
    /* Last resort: just loop forever (caller should call reboot()) */
    while (1)
        sleep(1);
#endif
}

static void cmd_heartbeat_trigger(void)
{
    if (heartbeat_trigger()) {
        printf("Heartbeat: triggered agent check.\n");
    } else {
        printf("Heartbeat: no actionable tasks found.\n");
    }
}

static void cmd_ask(int argc, char** argv)
{
    if (argc < 2) {
        printf("Usage: ask <message>\n");
        return;
    }

    /* Concatenate all arguments as the message */
    char content[256] = { 0 };
    for (int i = 1; i < argc; i++) {
        strncat(content, argv[i], sizeof(content) - strlen(content) - 1);
        if (i < argc - 1)
            strncat(content, " ", sizeof(content) - strlen(content) - 1);
    }

    agent_msg_t msg = { 0 };
    strncpy(msg.channel, "cli", sizeof(msg.channel) - 1);
    strncpy(msg.chat_id, "console", sizeof(msg.chat_id) - 1);
    msg.content = strdup(content);
    if (msg.content)
        message_bus_push_inbound(&msg);
    printf("Sent to agent: %s\n", content);
    syslog(LOG_INFO, "[agent] ask: %s\n", content);
}

static void cmd_cron_start(void)
{
    if (cron_service_start() == OK) {
        printf("Cron scheduler started.\n");
    } else {
        printf("Cron scheduler already running or failed to start.\n");
    }
}



/* ── list_models helpers ──────────────────────────────────────── */


#ifdef CONFIG_AI_AGENT_TEST
static void cmd_claw_test(int argc, char** argv)
{
    const char* filter = argc >= 2 ? argv[1] : NULL;

    /* Arena allocator test */
    if (!filter || strcmp(filter, "arena") == 0) {
        printf("=== Arena Allocator Test ===\n");
        arena_t a;
        int rc = arena_init(&a, 4096);
        printf("  init(4096): %s\n", rc == 0 ? "OK" : "FAIL");
        if (rc == 0) {
            void* p1 = arena_alloc(&a, 128);
            void* p2 = arena_alloc(&a, 256);
            void* p3 = arena_alloc(&a, 512);
            printf("  alloc 128+256+512: p1=%p p2=%p p3=%p\n",
                p1, p2, p3);
            printf("  used=%zu remaining=%zu\n",
                arena_used(&a), arena_remaining(&a));

            /* Verify no overlap */
            bool ok = p1 && p2 && p3
                && (char*)p2 >= (char*)p1 + 128
                && (char*)p3 >= (char*)p2 + 256;
            printf("  no-overlap: %s\n", ok ? "OK" : "FAIL");

            /* Test reset */
            arena_reset(&a);
            printf("  reset: used=%zu (expect 0)\n", arena_used(&a));

            /* Test overflow */
            void* big = arena_alloc(&a, 8192);
            printf("  overflow(8192): %s (expect NULL)\n",
                big == NULL ? "OK" : "FAIL");

            arena_destroy(&a);
            printf("  destroy: OK\n");
            printf("Arena test: PASSED\n\n");
        }
        if (filter) return;
    }

    int rc = test_vision_integ_run(filter);
    printf("Test suite %s\n", rc == 0 ? "PASSED" : "FAILED");
}
#endif

static void cmd_quit(void)
{
    printf("Exiting agent...\n");
    fflush(stdout);
    agent_request_shutdown();
}

static void cmd_launch_app(int argc, char** argv)
{
    if (argc < 2) {
        printf("Usage: launch_app <package_name>\n");
        return;
    }

    char input[256];
    snprintf(input, sizeof(input), "{\"package_name\":\"%s\"}", argv[1]);

    char output[512];
    int ret = tool_launch_quickapp_execute(input, output, sizeof(output));
    printf("Result(%d): %s\n", ret, output);
}

static void cmd_exit_app(void)
{
    char output[512];
    int ret = tool_exit_quickapp_execute("{}", output, sizeof(output));
    printf("Result(%d): %s\n", ret, output);
}

/* ── Router commands ──────────────────────────────────────────── */




/* ── mcp_add: add a remote MCP server ─────────────────────── */

#ifdef CONFIG_AI_AGENT_MCP
static void cmd_mcp_add(int argc, char** argv)
{
    if (argc < 3) {
        printf("Usage: mcp_add <name> <url> [token]\n"
               "  name:  server name (e.g. amap)\n"
               "  url:   MCP endpoint (e.g. http://host:port/mcp)\n"
               "  token: optional bearer token\n");
        return;
    }

    const char* token = (argc >= 4) ? argv[3] : NULL;
    int ret = mcp_client_add_server(argv[1], argv[2], token);
    if (ret == OK) {
        printf("Added MCP server: %s → %s\n", argv[1], argv[2]);
    } else {
        printf("Failed to add MCP server\n");
    }
}

/* ── mcp_remove: remove a remote MCP server ───────────────── */

static void cmd_mcp_remove(int argc, char** argv)
{
    if (argc < 2) {
        printf("Usage: mcp_remove <name>\n");
        return;
    }

    int ret = mcp_client_remove_server(argv[1]);
    if (ret == OK) {
        printf("Removed MCP server: %s\n", argv[1]);
    } else {
        printf("Server not found: %s\n", argv[1]);
    }
}

/* ── mcp_discover: discover tools from all servers ────────── */

static void cmd_mcp_discover(void)
{
    printf("Discovering remote MCP tools...\n");
    int n = mcp_client_discover();
    if (n >= 0) {
        printf("Discovered %d remote tools\n", n);
        /* Rebuild tools JSON so agent sees new tools */
        tool_registry_invalidate();
    } else {
        printf("Discovery failed\n");
    }
}

/* ── mcp_status: show MCP client status ───────────────────── */

static void cmd_mcp_status(void)
{
    char* json = mcp_client_status_json();
    if (json) {
        printf("%s\n", json);
        free(json);
    } else {
        printf("No MCP client data\n");
    }
}

/* ── mcp_tools: list remote tools ─────────────────────────── */

static void cmd_mcp_tools(void)
{
    char* json = mcp_client_get_tools_json();
    if (json) {
        printf("%s\n", json);
        free(json);
    } else {
        printf("No remote tools (run mcp_discover first)\n");
    }
}
#endif /* CONFIG_AI_AGENT_MCP */

/* ── Skill file installation helpers ───────────────────────── */

#define SKILL_NAME_MAX 48
#define SKILL_IMPORT_MAX_BYTES 8191
#define SKILL_IMPORT_HEX_MAX 192

static char g_skill_import_name[SKILL_NAME_MAX];
static size_t g_skill_import_size;
static int g_skill_import_active;

static int skill_name_valid(const char* name)
{
    size_t len;

    if (!name || name[0] == '\0') {
        return ERROR;
    }

    len = strlen(name);
    if (len >= SKILL_NAME_MAX) {
        return ERROR;
    }

    for (const char* p = name; *p; p++) {
        if (!(*p >= 'a' && *p <= 'z') && !(*p >= '0' && *p <= '9')
            && *p != '-' && *p != '_') {
            return ERROR;
        }
    }

    return OK;
}

static int skill_path(const char* name, const char* suffix,
                      char* path, size_t path_size)
{
    int n;

    if (skill_name_valid(name) != OK || !suffix || !path) {
        return ERROR;
    }

    n = snprintf(path, path_size, "%s%s%s", AGENT_SKILLS_DIR, name,
                 suffix);
    if (n < 0 || (size_t)n >= path_size) {
        return ERROR;
    }
    return OK;
}

static int skill_hex_value(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

/* ── install_skill: install a skill from URL ──────────────── */

/* vela_https_get() takes host, port and path separately.  Keep URL parsing
 * here so the public install_skill command cannot accidentally pass the
 * literal "https://..." prefix to getaddrinfo().  Only https URLs are
 * accepted; credentials and non-numeric ports are rejected. */
static int parse_https_url(const char* url,
                           char* host, size_t host_size,
                           char* port, size_t port_size,
                           char* path, size_t path_size)
{
    const char* prefix = "https://";
    const size_t prefix_len = 8;
    const char* authority;
    const char* end;
    const char* colon = NULL;
    size_t host_len;
    size_t port_len;
    size_t path_len;

    if (!url || strncmp(url, prefix, prefix_len) != 0) {
        return ERROR;
    }

    authority = url + prefix_len;
    end = strchr(authority, '/');
    if (!end) {
        end = authority + strlen(authority);
    }
    if (end == authority) {
        return ERROR;
    }

    for (const char* p = authority; p < end; p++) {
        if (*p == ':') {
            if (colon) {
                /* IPv6 literals are intentionally unsupported here. */
                return ERROR;
            }
            colon = p;
        }
        if (*p == '@') {
            /* Do not allow userinfo in a skill URL. */
            return ERROR;
        }
    }

    host_len = colon ? (size_t)(colon - authority)
                     : (size_t)(end - authority);
    if (host_len == 0 || host_len >= host_size) {
        return ERROR;
    }
    memcpy(host, authority, host_len);
    host[host_len] = '\0';

    if (colon) {
        port_len = (size_t)(end - colon - 1);
        if (port_len == 0 || port_len >= port_size) {
            return ERROR;
        }
        for (size_t i = 0; i < port_len; i++) {
            if (colon[1 + i] < '0' || colon[1 + i] > '9') {
                return ERROR;
            }
        }
        memcpy(port, colon + 1, port_len);
        port[port_len] = '\0';
    } else {
        if (port_size < 4) {
            return ERROR;
        }
        strcpy(port, "443");
    }

    if (*end == '\0') {
        if (path_size < 2) {
            return ERROR;
        }
        strcpy(path, "/");
        return OK;
    }

    path_len = strlen(end);
    if (path_len == 0 || path_len >= path_size) {
        return ERROR;
    }
    memcpy(path, end, path_len + 1);
    return OK;
}

static void cmd_install_skill(int argc, char** argv)
{
    if (argc < 3) {
        printf("Usage: install_skill <name> <url>\n"
               "  name: skill filename (without .md)\n"
               "  url:  HTTPS URL to download skill markdown\n");
        return;
    }

    const char* name = argv[1];
    const char* url = argv[2];

    if (skill_name_valid(name) != OK) {
        printf("Invalid skill name: use a-z, 0-9, hyphens\n");
        return;
    }

    char host[128];
    char port[8];
    char path_url[512];
    if (parse_https_url(url, host, sizeof(host), port, sizeof(port),
            path_url, sizeof(path_url)) != OK) {
        printf("Invalid HTTPS URL (host/path required; no credentials)\n");
        return;
    }

    char path[128];
    if (skill_path(name, ".md", path, sizeof(path)) != OK) {
        printf("Skill name too long\n");
        return;
    }

    char* buf = malloc(8192);
    if (!buf) {
        printf("OOM\n");
        return;
    }

    memset(buf, 0, 8192);
    int rc = vela_https_get(host, port, path_url, buf, 8192);
    if (rc < 200 || rc >= 300) {
        printf("Download failed: HTTP %d\n", rc);
        free(buf);
        return;
    }

    /* Validate content looks like a markdown skill file */
    if (buf[0] == '\0') {
        printf("Downloaded content is empty\n");
        free(buf);
        return;
    }

    /* Basic content validation: must start with # or --- (frontmatter) */
    const char* p = buf;
    while (*p == ' ' || *p == '\n' || *p == '\r')
        p++;
    if (*p != '#' && strncmp(p, "---", 3) != 0) {
        printf("Invalid skill format: must start with # or ---\n");
        free(buf);
        return;
    }

    /* Size sanity check */
    size_t content_len = strlen(buf);
    if (content_len < 10) {
        printf("Downloaded content too small (%zu bytes)\n", content_len);
        free(buf);
        return;
    }

    FILE* f = fopen(path, "w");
    if (!f) {
        printf("Cannot write: %s\n", path);
        free(buf);
        return;
    }

    fputs(buf, f);
    fclose(f);
    free(buf);
    printf("Skill installed: %s (%zu bytes)\n", path, content_len);
}

/*
 * Offline provisioning path for boards whose Wi-Fi is client-isolated from
 * the build host.  The only writable target is AGENT_SKILLS_DIR/<name>.part;
 * skill_write_commit() renames it to .md after a small markdown sanity check.
 * Chunks are hex-encoded so the NSH tokenizer never has to parse Skill text.
 */
static void cmd_skill_write_begin(int argc, char** argv)
{
    char path[128];
    FILE* f;

    if (argc != 2) {
        printf("Usage: skill_write_begin <name>\n");
        return;
    }
    if (skill_path(argv[1], ".part", path, sizeof(path)) != OK) {
        printf("Invalid skill name\n");
        return;
    }

    f = fopen(path, "w");
    if (!f) {
        printf("Cannot stage: %s\n", path);
        return;
    }
    fclose(f);

    strncpy(g_skill_import_name, argv[1], sizeof(g_skill_import_name) - 1);
    g_skill_import_name[sizeof(g_skill_import_name) - 1] = '\0';
    g_skill_import_size = 0;
    g_skill_import_active = 1;
    printf("Skill staging started: %s\n", path);
}

static void cmd_skill_write_hex(int argc, char** argv)
{
    char path[128];
    unsigned char decoded[SKILL_IMPORT_HEX_MAX / 2];
    const char* hex;
    size_t hex_len;
    size_t decoded_len;
    FILE* f;

    if (argc != 3) {
        printf("Usage: skill_write_hex <name> <hex>\n");
        return;
    }
    if (!g_skill_import_active || strcmp(argv[1], g_skill_import_name) != 0) {
        printf("No matching Skill staging session\n");
        return;
    }
    hex = argv[2];
    hex_len = strlen(hex);
    if (hex_len == 0 || hex_len > SKILL_IMPORT_HEX_MAX
        || (hex_len & 1) != 0) {
        printf("Invalid hex chunk (1..%d bytes)\n",
               SKILL_IMPORT_HEX_MAX / 2);
        return;
    }
    decoded_len = hex_len / 2;
    if (g_skill_import_size + decoded_len > SKILL_IMPORT_MAX_BYTES) {
        printf("Skill exceeds %d-byte limit\n", SKILL_IMPORT_MAX_BYTES);
        return;
    }

    for (size_t i = 0; i < decoded_len; i++) {
        int high = skill_hex_value(hex[i * 2]);
        int low = skill_hex_value(hex[i * 2 + 1]);
        if (high < 0 || low < 0) {
            printf("Invalid hex chunk\n");
            return;
        }
        decoded[i] = (unsigned char)((high << 4) | low);
    }

    if (skill_path(g_skill_import_name, ".part", path, sizeof(path)) != OK) {
        printf("Invalid skill name\n");
        return;
    }
    f = fopen(path, "ab");
    if (!f) {
        printf("Cannot append: %s\n", path);
        return;
    }
    if (fwrite(decoded, 1, decoded_len, f) != decoded_len) {
        fclose(f);
        printf("Skill chunk write failed\n");
        return;
    }
    fclose(f);
    g_skill_import_size += decoded_len;
    printf("Skill staging: %zu bytes\n", g_skill_import_size);
}

static void cmd_skill_write_commit(int argc, char** argv)
{
    char part_path[128];
    char final_path[128];
    FILE* f;
    int first;
    int second;

    if (argc != 2) {
        printf("Usage: skill_write_commit <name>\n");
        return;
    }
    if (!g_skill_import_active || strcmp(argv[1], g_skill_import_name) != 0) {
        printf("No matching Skill staging session\n");
        return;
    }
    if (skill_path(argv[1], ".part", part_path, sizeof(part_path)) != OK
        || skill_path(argv[1], ".md", final_path, sizeof(final_path)) != OK) {
        printf("Invalid skill name\n");
        return;
    }
    if (g_skill_import_size < 10) {
        printf("Skill is too small (%zu bytes)\n", g_skill_import_size);
        return;
    }

    f = fopen(part_path, "rb");
    if (!f) {
        printf("Cannot read staged Skill\n");
        return;
    }
    first = fgetc(f);
    second = fgetc(f);
    fclose(f);
    if (first != '#' && !(first == '-' && second == '-')) {
        printf("Invalid skill format: must start with # or ---\n");
        return;
    }
    if (rename(part_path, final_path) != 0) {
        printf("Cannot activate Skill: %s\n", final_path);
        return;
    }

    printf("Skill committed: %s (%zu bytes)\n", final_path,
           g_skill_import_size);
    g_skill_import_name[0] = '\0';
    g_skill_import_size = 0;
    g_skill_import_active = 0;
}

#if AGENT_SKILL_SYNC_ENABLED
static void cmd_skill_sync(void)
{
    printf("Syncing skills from Bitable...\n");
    int rc = skill_sync_from_bitable();
    if (rc == OK) {
        printf("Skill sync completed successfully.\n");
    } else {
        printf("Skill sync failed (check syslog for details).\n");
    }
}
#endif

/* ── CLI thread ───────────────────────────────────────────────── */

static void* cli_thread(void* arg)
{
    (void)arg;
    char line[LINE_LEN];
    char* argv[MAX_ARGS];
    printf("[CLI] thread stack=%p\n", (void *)&line);
    fflush(stdout);

    syslog(LOG_INFO, "[%s] NSH CLI started. Type 'help' for commands.\n", TAG);
    pthread_mutex_lock(&g_stdout_lock);
    printf("vela> ");
    fflush(stdout);
    pthread_mutex_unlock(&g_stdout_lock);

    while (fgets(line, sizeof(line), stdin) != NULL) {
        /* Strip trailing newline */
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
            line[--len] = '\0';

        if (len == 0) {
            pthread_mutex_lock(&g_stdout_lock);
            printf("vela> ");
            fflush(stdout);
            pthread_mutex_unlock(&g_stdout_lock);
            continue;
        }

        int argc = tokenise(line, argv, MAX_ARGS);
        if (argc == 0) {
            pthread_mutex_lock(&g_stdout_lock);
            printf("vela> ");
            fflush(stdout);
            pthread_mutex_unlock(&g_stdout_lock);
            continue;
        }

        const char* cmd = argv[0];

        if (strcmp(cmd, "help") == 0)
            cmd_help();
        else if (strcmp(cmd, "net_status") == 0)
            cmd_net_status();
        else if (strcmp(cmd, "net_test") == 0)
            cmd_net_test(argc, argv);
        else if (strcmp(cmd, "tcp_probe") == 0)
            cmd_tcp_probe(argc, argv);
        else if (strcmp(cmd, "tls_replay") == 0)
            cmd_tls_replay(argc, argv);
        else if (strcmp(cmd, "set_feishu_app") == 0)
            cmd_set_feishu_app(argc, argv);
        else if (strcmp(cmd, "set_feishu_user_token") == 0)
            cmd_set_feishu_user_token(argc, argv);
        else if (strcmp(cmd, "set_llm") == 0)
            cmd_set_llm(argc, argv);
        else if (strcmp(cmd, "set_vision_llm") == 0)
            cmd_set_vision_llm(argc, argv);
        else if (strcmp(cmd, "list_models") == 0)
            cmd_list_models(argc, argv);
        else if (strcmp(cmd, "memory_read") == 0)
            cmd_memory_read();
        else if (strcmp(cmd, "memory_write") == 0)
            cmd_memory_write(argc, argv);
        else if (strcmp(cmd, "session_list") == 0)
            cmd_session_list();
        else if (strcmp(cmd, "session_clear") == 0)
            cmd_session_clear(argc, argv);
        else if (strcmp(cmd, "session_clear_all") == 0)
            cmd_session_clear_all();
        else if (strcmp(cmd, "heap_info") == 0)
            cmd_heap_info();
        else if (strcmp(cmd, "set_proxy") == 0)
            cmd_set_proxy(argc, argv);
        else if (strcmp(cmd, "clear_proxy") == 0)
            cmd_clear_proxy();
        else if (strcmp(cmd, "set_wifi") == 0)
            cmd_set_wifi(argc, argv);
        else if (strcmp(cmd, "wifi_reconnect") == 0)
            cmd_wifi_reconnect();
#ifdef CONFIG_AI_AGENT_NET_RPMSG
        else if (strcmp(cmd, "net_diag") == 0)
            cmd_net_diag(argc, argv);
        else if (strcmp(cmd, "net_reconnect") == 0)
            cmd_net_reconnect();
        else if (strcmp(cmd, "set_netproxy") == 0)
            cmd_set_netproxy(argc, argv);
        else if (strcmp(cmd, "set_dns") == 0)
            cmd_set_dns(argc, argv);
#endif
        else if (strcmp(cmd, "set_search_key") == 0)
            cmd_set_search_key(argc, argv);
        else if (strcmp(cmd, "set_exa_key") == 0)
            cmd_set_exa_key(argc, argv);
        else if (strcmp(cmd, "set_news_key") == 0)
            cmd_set_news_key(argc, argv);
        else if (strcmp(cmd, "set_tavily_key") == 0)
            cmd_set_tavily_key(argc, argv);
        else if (strcmp(cmd, "config_show") == 0)
            cmd_config_show();
        else if (strcmp(cmd, "config_reset") == 0)
            cmd_config_reset();
        else if (strcmp(cmd, "heartbeat_trigger") == 0)
            cmd_heartbeat_trigger();
        else if (strcmp(cmd, "ask") == 0)
            cmd_ask(argc, argv);
        else if (strcmp(cmd, "cron_start") == 0)
            cmd_cron_start();
#ifdef CONFIG_AI_AGENT_NODE
        else if (strcmp(cmd, "node_list") == 0)
            cmd_node_list();
        else if (strcmp(cmd, "set_gateway") == 0)
            cmd_set_gateway(argc, argv);
        else if (strcmp(cmd, "node_start") == 0)
            cmd_node_start();
        else if (strcmp(cmd, "node_stop") == 0)
            cmd_node_stop();
#endif
        else if (strcmp(cmd, "quit") == 0) {
            cmd_quit();
            break;
        } else if (strcmp(cmd, "set_mqtt") == 0)
            cmd_set_mqtt(argc, argv);
        else if (strcmp(cmd, "set_volc_key") == 0)
            cmd_set_volc_key(argc, argv);
        else if (strcmp(cmd, "set_volc_asr") == 0)
            cmd_set_volc_asr(argc, argv);
        else if (strcmp(cmd, "set_volc_speaker") == 0)
            cmd_set_volc_speaker(argc, argv);
        else if (strcmp(cmd, "voice_start") == 0)
            cmd_voice_start();
        else if (strcmp(cmd, "voice_stop") == 0)
            cmd_voice_stop();
        else if (strcmp(cmd, "voice_test_tts") == 0)
            cmd_voice_test_tts(argc, argv);
        else if (strcmp(cmd, "voice_test_asr") == 0)
            cmd_voice_test_asr(argc, argv);
        else if (strcmp(cmd, "media_probe") == 0)
            cmd_media_probe(argc, argv);
        else if (strcmp(cmd, "audio_stream") == 0)
            cmd_audio_stream(argc, argv);
        else if (strcmp(cmd, "i2sknob") == 0)
            cmd_i2sknob(argc, argv);
        else if (strcmp(cmd, "wake_loop") == 0)
            cmd_wake_loop(argc, argv);
        else if (strcmp(cmd, "wake_kws") == 0)
            cmd_wake_kws(argc, argv);
        else if (strcmp(cmd, "kws_listen") == 0)
            cmd_kws_listen(argc, argv);
        else if (strcmp(cmd, "intent_send") == 0)
            cmd_intent_send(argc, argv);
        else if (strcmp(cmd, "vision_loop") == 0)
            cmd_vision_loop(argc, argv);
        else if (strcmp(cmd, "wake_rec") == 0)
            cmd_wake_rec(argc, argv);
        else if (strcmp(cmd, "kws_dump") == 0)
            cmd_kws_dump(argc, argv);
        else if (strcmp(cmd, "set_media") == 0)
            cmd_set_media(argc, argv);
        else if (strcmp(cmd, "vision") == 0)
            cmd_vision(argc, argv);
        else if (strcmp(cmd, "voice") == 0)
            cmd_voice(argc, argv);
        else if (strcmp(cmd, "set_voice_tts") == 0)
            cmd_set_voice_tts(argc, argv);
        else if (strcmp(cmd, "set_voice_asr") == 0)
            cmd_set_voice_asr(argc, argv);
        else if (strcmp(cmd, "set_weixin_token") == 0)
            cmd_set_weixin_token(argc, argv);
        else if (strcmp(cmd, "weixin_login") == 0)
            cmd_weixin_login();
        else if (strcmp(cmd, "launch_app") == 0)
            cmd_launch_app(argc, argv);
        else if (strcmp(cmd, "exit_app") == 0)
            cmd_exit_app();
        else if (strcmp(cmd, "install_skill") == 0)
            cmd_install_skill(argc, argv);
        else if (strcmp(cmd, "skill_write_begin") == 0)
            cmd_skill_write_begin(argc, argv);
        else if (strcmp(cmd, "skill_write_hex") == 0)
            cmd_skill_write_hex(argc, argv);
        else if (strcmp(cmd, "skill_write_commit") == 0)
            cmd_skill_write_commit(argc, argv);
#if AGENT_SKILL_SYNC_ENABLED
        else if (strcmp(cmd, "skill_sync") == 0)
            cmd_skill_sync();
#endif
#ifdef CONFIG_AI_AGENT_MCP
        else if (strcmp(cmd, "mcp_add") == 0)
            cmd_mcp_add(argc, argv);
        else if (strcmp(cmd, "mcp_remove") == 0)
            cmd_mcp_remove(argc, argv);
        else if (strcmp(cmd, "mcp_discover") == 0)
            cmd_mcp_discover();
        else if (strcmp(cmd, "mcp_status") == 0)
            cmd_mcp_status();
        else if (strcmp(cmd, "mcp_tools") == 0)
            cmd_mcp_tools();
#endif
#ifdef CONFIG_AI_AGENT_LVGL_UI
        else if (strcmp(cmd, "show_chat") == 0)
            lvgl_ui_channel_show();
#endif
#ifdef CONFIG_AI_AGENT_TEST
        else if (strcmp(cmd, "claw_test") == 0)
            cmd_claw_test(argc, argv);
#endif
        else if (strcmp(cmd, "router_status") == 0)
            cmd_router_status();
        else if (strcmp(cmd, "router_set") == 0)
            cmd_router_set(argc, argv);
        else if (strcmp(cmd, "router_model") == 0)
            cmd_router_model(argc, argv);
        else if (strcmp(cmd, "router_profile") == 0)
            cmd_router_profile(argc, argv);
        else if (strcmp(cmd, "router_clear") == 0)
            cmd_router_clear(argc, argv);
        else if (strcmp(cmd, "restart") == 0)
            cmd_restart();
#ifdef CONFIG_AI_AGENT_BLE_GATT
        else if (strcmp(cmd, "ble_gatt_test") == 0)
            cmd_ble_gatt_test(argc, argv);
#endif
        else
            printf("Unknown command: %s (type 'help')\n", cmd);

        pthread_mutex_lock(&g_stdout_lock);
        printf("vela> ");
        fflush(stdout);
        pthread_mutex_unlock(&g_stdout_lock);
    }

    syslog(LOG_INFO, "[%s] CLI thread exiting\n", TAG);
    return NULL;
}

/* ── Init / Start ─────────────────────────────────────────────── */

int nsh_commands_init(void)
{
    /* Pure registration — no thread spawned here.
     * Commands are statically defined; nothing to do at the moment.
     * Kept as a hook for future dynamic command registration. */
    syslog(LOG_INFO, "NSH commands registered");
    return OK;
}

int nsh_commands_start(void)
{
    return agent_task_create(cli_thread, "agent_cli", AGENT_CLI_STACK, NULL, AGENT_CLI_PRIO);
}
