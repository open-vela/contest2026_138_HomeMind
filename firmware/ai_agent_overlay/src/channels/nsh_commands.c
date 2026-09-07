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
    unsigned char *frames[3] = { NULL, NULL, NULL };

    video_fd = open(video_path, O_RDWR | O_NONBLOCK);
    if (video_fd < 0)
        return -1;

    for (i = 0; i < 3; i++)
        frames[i] = memalign(32, out_len);
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
        buf.length = out_len;
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
    for (i = 0; i < 3; i++)
        if (frames[i])
            free(frames[i]);
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

static void cmd_vision(int argc, char **argv)
{
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

#define HM_VOICE_SECONDS 3

/* 按驱动声明的 nbuffers/buffer_size 做标准多缓冲流式采集：
 * poll -> DEQUEUE 取满缓冲 -> 拷贝 -> 重新 ENQUEUE，直到录满目标字节数。 */
static int hm_voice_record(unsigned char *buf, int want_bytes, int rate)
{
    const char *audio_path = "/dev/audio/pcm_in0";
    struct audio_caps_desc_s caps;
    struct audio_buf_desc_s desc;
    struct ap_buffer_info_s info;
    struct pollfd pfd;
    struct ap_buffer_s **apbs = NULL;
    unsigned char *dst = buf;
    int remain = want_bytes;
    int fd = -1;
    int started = 0;
    int nbuf = 0;
    int bsize = 0;
    int ret = -1;
    int i;
    int idle_polls = 0;

    fd = open(audio_path, O_RDWR | O_NONBLOCK);
    if (fd < 0) {
        printf("[Voice-DBG] open fail errno=%d\n", errno);
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
        printf("[Voice-DBG] config fail\n");
        goto out;
    }

    memset(&info, 0, sizeof(info));
    if (ioctl(fd, AUDIOIOC_GETBUFFERINFO, (uintptr_t)&info) < 0) {
        printf("[Voice-DBG] bufferinfo fail\n");
        goto out;
    }
    nbuf = info.nbuffers;
    bsize = info.buffer_size;
    printf("[Voice-DBG] driver nbuffers=%d buffer_size=%d\n", nbuf, bsize);
    /* 保底采集路径（2026-09-03 验证过的唯一稳定形态）：
     * 每个 640 字节块都完整走 开->配置->分配->入队->启动->排空->停->释放->关，
     * 不重入队、不多缓冲（该 I2S 下半层重入队不再产生数据）。
     * 块间存在间隙，音质有损；ASR 容忍度实测决定后续是否深挖驱动。 */
    bsize = 640;
    {
        int cycles = want_bytes / bsize;
        int i;
        for (i = 0; i < cycles && remain > 0; i++) {
            struct audio_caps_desc_s caps;
            struct audio_buf_desc_s desc;
            struct ap_buffer_info_s info;
            struct pollfd pfd;
            struct ap_buffer_s *apb = NULL;
            int cfd = open(audio_path, O_RDWR | O_NONBLOCK);
            int started = 0;
            int waited = 0;
            int got = -1;

            if (cfd < 0)
                break;
            memset(&caps, 0, sizeof(caps));
            caps.caps.ac_len = sizeof(struct audio_caps_s);
            caps.caps.ac_type = AUDIO_TYPE_INPUT;
            caps.caps.ac_controls.w = rate;
            caps.caps.ac_controls.b[2] = 16;
            caps.caps.ac_channels = 1;
            caps.caps.ac_format.hw = AUDIO_FMT_PCM;
            if (ioctl(cfd, AUDIOIOC_CONFIGURE, (uintptr_t)&caps) < 0) {
                close(cfd);
                break;
            }
            memset(&info, 0, sizeof(info));
            (void)ioctl(cfd, AUDIOIOC_GETBUFFERINFO, (uintptr_t)&info);
            memset(&desc, 0, sizeof(desc));
            desc.numbytes = bsize;
            desc.u.pbuffer = &apb;
            if (ioctl(cfd, AUDIOIOC_ALLOCBUFFER, (uintptr_t)&desc) !=
                (int)sizeof(desc) || !apb) {
                close(cfd);
                break;
            }
            memset(&desc, 0, sizeof(desc));
            desc.u.buffer = apb;
            desc.numbytes = bsize;
            if (ioctl(cfd, AUDIOIOC_ENQUEUEBUFFER, (uintptr_t)&desc) < 0) {
                memset(&desc, 0, sizeof(desc));
                desc.u.buffer = apb;
                ioctl(cfd, AUDIOIOC_FREEBUFFER, (uintptr_t)&desc);
                close(cfd);
                break;
            }
            if (ioctl(cfd, AUDIOIOC_START, 0) == 0) {
                started = 1;
                memset(&pfd, 0, sizeof(pfd));
                pfd.fd = cfd;
                pfd.events = POLLIN;
                waited = 0;
                while (apb->nbytes == 0 && waited++ < 6) {
                    if (poll(&pfd, 1, 200) <= 0 && apb->nbytes == 0)
                        break;
                }
                if (apb->nbytes > 0) {
                    int n = apb->nbytes < bsize ? apb->nbytes : bsize;
                    if (n > remain)
                        n = remain;
                    memcpy(dst, apb->samp, n);
                    dst += n;
                    remain -= n;
                    got = n;
                }
            }
            if (started)
                ioctl(cfd, AUDIOIOC_STOP, 0);
            memset(&desc, 0, sizeof(desc));
            desc.u.buffer = apb;
            ioctl(cfd, AUDIOIOC_FREEBUFFER, (uintptr_t)&desc);
            close(cfd);
            if (got <= 0)
                break;
            if (i % 50 == 49)
                printf("[Voice]: %d/%d chunks\n", i + 1, cycles);
        }
    }

    ret = want_bytes - remain;
    printf("[Voice-DBG] captured %d bytes\n", ret);

out:
    if (started)
        ioctl(fd, AUDIOIOC_STOP, 0);
    for (i = 0; i < nbuf; i++) {
        if (apbs && apbs[i]) {
            memset(&desc, 0, sizeof(desc));
            desc.u.buffer = apbs[i];
            ioctl(fd, AUDIOIOC_FREEBUFFER, (uintptr_t)&desc);
        }
    }
    if (apbs)
        free(apbs);
    close(fd);
    return ret;
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

static void cmd_voice(int argc, char **argv)
{
    char host[64] = "api.hfy-ai.cloud";
    char port[8] = "443";
    char token[128] = "";
    char llm_host[64] = "";
    char llm_port[8] = "443";
    char llm_path[64] = "/v1/chat/completions";
    char api_key[160] = "";
    char model[64] = "mimo-v2.5";
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
    int i;

    claw_config_get("media_host", host, sizeof(host));
    claw_config_get("media_port", port, sizeof(port));
    claw_config_get("media_token", token, sizeof(token));
    if (!token[0]) {
        printf("[Voice-ERR]: media_token not set (set_media first)\n");
        return;
    }
    if (claw_config_get("llm_host", llm_host, sizeof(llm_host)) != OK ||
        !llm_host[0]) {
        printf("[Voice-ERR]: llm not configured (set_llm first)\n");
        return;
    }
    claw_config_get("llm_port", llm_port, sizeof(llm_port));
    claw_config_get("llm_path", llm_path, sizeof(llm_path));
    claw_config_get("api_key", api_key, sizeof(api_key));
    claw_config_get("model", model, sizeof(model));
    if (!api_key[0]) {
        printf("[Voice-ERR]: api_key missing\n");
        return;
    }

    pcm = malloc(HM_VOICE_SECONDS * 16000 * 2);
    if (!pcm) {
        printf("[Voice-ERR]: alloc failed\n");
        return;
    }
    printf("[Voice]: recording ~3s, speak now...\n");
    hm_lcd_show_text("SPEAK NOW");
    pcm_len = hm_voice_record(pcm, HM_VOICE_SECONDS * 16000 * 2, 16000);
    if (pcm_len < HM_VOICE_SECONDS * 16000) {
        printf("[Voice-ERR]: captured too little (%d bytes)\n", pcm_len);
    hm_lcd_show_text("MIC ERR");
    hm_lcd_release();
        free(pcm);
        pcm = NULL;
        return;
    }
    printf("[Voice]: captured %d bytes, transcribing...\n", pcm_len);
    hm_lcd_show_text("THINKING");

    snprintf(path, sizeof(path), "/v1/media/audio?rate=16000");
    snprintf(auth, sizeof(auth), "Bearer %s", token);
    status = vela_https_request(host, port, "POST", path, bin_hdr,
                                (const char *)pcm, pcm_len,
                                resp, sizeof(resp), &rlen);
    if (rlen < sizeof(resp))
        resp[rlen] = '\0';
    else
        resp[sizeof(resp) - 1] = '\0';
    if (status != 200) {
        printf("[Voice-ERR]: asr http=%d body=%.200s\n", status, resp);
        free(pcm);
        pcm = NULL;
        return;
    }
    hm_media_json_str(resp, "\"text\":\"", trans, sizeof(trans));
    if (!trans[0]) {
        printf("[Voice-ERR]: empty transcript\n");
        free(pcm);
        pcm = NULL;
        return;
    }
    printf("[Voice]: %s\n", trans);

    /* MiMo 问答（同步，一句话回答） */
    hm_json_escape(trans, esc_t, sizeof(esc_t));
    body = malloc(2048);
    if (!body) {
        printf("[Voice-ERR]: body alloc failed\n");
        free(pcm);
        pcm = NULL;
        return;
    }
    snprintf(body, 2048,
             "{\"model\":\"%s\",\"messages\":[{\"role\":\"user\","
             "\"content\":\"%s（你是家庭机器人HomeMind，请用不超过60字的"
             "中文口语回答）\"}]}",
             model, esc_t);
    snprintf(path, sizeof(path), "%s", llm_path);
    snprintf(auth, sizeof(auth), "Bearer %s", api_key);
    printf("[Voice]: asking %s:%s%s ...\n", llm_host, llm_port, llm_path);
    status = vela_https_request(llm_host, llm_port, "POST", path, json_hdr,
                                body, strlen(body),
                                resp, sizeof(resp), &rlen);
    free(body);
    body = NULL;
    free(pcm);
    pcm = NULL;
    if (rlen < sizeof(resp))
        resp[rlen] = '\0';
    else
        resp[sizeof(resp) - 1] = '\0';
    if (status != 200) {
        printf("[Voice-ERR]: llm http=%d body=%.200s\n", status, resp);
        return;
    }
    hm_media_json_str(resp, "\"content\":\"", esc_a, sizeof(esc_a));
    if (!esc_a[0]) {
        printf("[Voice-ERR]: empty answer\n");
        return;
    }
    printf("[Agent]: %s\n", esc_a);
    hm_lcd_release();
    hm_lcd_show_text("DONE");

    /* 让小爱音箱播报回答（announce 端点 -> MQTT speak -> 网关 -> HA） */
    snprintf(auth, sizeof(auth), "Bearer %s", token);
    hm_json_escape(esc_a, esc_t, sizeof(esc_t));
    body = malloc(1024);
    if (!body)
        return;
    snprintf(body, 1024,
             "{\"device_id\":\"esp32s3-eye\",\"text\":\"%s\"}", esc_t);
    status = vela_https_request(host, port, "POST", "/v1/media/announce",
                                json_hdr, body, strlen(body),
                                resp, sizeof(resp), &rlen);
    free(body);
    body = NULL;
    printf("[Voice]: announce http=%d（音箱应已播报）\n", status);

    /* 原文送入真实 agent 管线：本地意图/工具（如开灯）仍会被执行 */
    {
        agent_msg_t msg = { 0 };
        strncpy(msg.channel, "cli", sizeof(msg.channel) - 1);
        strncpy(msg.chat_id, "console", sizeof(msg.chat_id) - 1);
        msg.content = strdup(trans);
        if (msg.content)
            message_bus_push_inbound(&msg);
    }
    (void)i;
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

    video_fd = open(video_path, O_RDWR | O_NONBLOCK);
    if (video_fd < 0) {
        printf("MEDIA_VIDEO_FRAME_FAIL stage=open errno=%d\n", errno);
    } else {
        frames[0] = memalign(32, video_bytes);
        frames[1] = memalign(32, video_bytes);
        frames[2] = memalign(32, video_bytes);
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
            if (video_ret < 0) {
                printf("MEDIA_VIDEO_FRAME_FAIL stage=s_fmt errno=%d\n",
                       errno);
            } else {
                memset(&req, 0, sizeof(req));
                req.type = type;
                req.memory = V4L2_MEMORY_USERPTR;
                req.count = 3;
                req.mode = V4L2_BUF_MODE_RING;
                video_ret = ioctl(video_fd, VIDIOC_REQBUFS,
                                  (uintptr_t)&req);
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
                    if (video_ret < 0) {
                        printf("MEDIA_VIDEO_FRAME_FAIL stage=qbuf index=%u errno=%d\n",
                               (unsigned int)buf.index, errno);
                    } else {
                        video_ret = ioctl(video_fd, VIDIOC_STREAMON,
                                          (uintptr_t)&type);
                        if (video_ret < 0) {
                            printf("MEDIA_VIDEO_FRAME_FAIL stage=streamon errno=%d\n",
                                   errno);
                        } else {
                            streaming = 1;
                            memset(&pfd, 0, sizeof(pfd));
                            pfd.fd = video_fd;
                            pfd.events = POLLIN;
                            video_ret = poll(&pfd, 1, 5000);
                            if (video_ret <= 0) {
                                printf("MEDIA_VIDEO_FRAME_FAIL stage=poll ret=%d revents=0x%x errno=%d\n",
                                       video_ret, pfd.revents, errno);
                            } else {
                                memset(&buf, 0, sizeof(buf));
                                buf.type = type;
                                buf.memory = V4L2_MEMORY_USERPTR;
                                video_ret = ioctl(video_fd, VIDIOC_DQBUF,
                                                  (uintptr_t)&buf);
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
