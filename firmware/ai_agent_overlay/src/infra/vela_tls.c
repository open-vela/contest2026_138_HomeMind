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

#include "infra/vela_tls.h"
#include "infra/http_proxy.h"
#include "agent_compat.h"
#include "agent_config.h"

#ifdef CONFIG_AI_AGENT_NET_RPMSG
#include "network/network_manager.h"
#endif

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* POSIX networking */
#include <arpa/inet.h>
#include <netdb.h>
#ifdef CONFIG_NETDB_DNSCLIENT
#include <nuttx/net/dns.h>
#endif
#include <nuttx/mm/iob.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

/* mbedTLS */
/* Allow direct access to mbedTLS context internals for the debug
 * threshold (mbedtls_debug_set_threshold is not compiled in). */

#define MBEDTLS_ALLOW_PRIVATE_ACCESS
#include "mbedtls/ctr_drbg.h"
#if defined(MBEDTLS_DEBUG_C)
#include "mbedtls/debug.h"
#endif
#include "mbedtls/entropy.h"
#include "mbedtls/error.h"
#include "mbedtls/net_sockets.h"
#include "mbedtls/ssl.h"
#include "psa/crypto.h"

#include <fcntl.h>
#include <pthread.h>
#include <time.h>

#ifdef CONFIG_NETDB_DNSCLIENT
static int log_dns_nameserver(void* arg, struct sockaddr* addr,
    socklen_t addrlen)
{
    char text[INET6_ADDRSTRLEN] = "?";
    const void* raw_addr = NULL;

    (void)arg;
    (void)addrlen;
    if (addr->sa_family == AF_INET) {
        raw_addr = &((struct sockaddr_in*)addr)->sin_addr;
    }
#ifdef CONFIG_NET_IPv6
    else if (addr->sa_family == AF_INET6) {
        raw_addr = &((struct sockaddr_in6*)addr)->sin6_addr;
    }
#endif

    if (raw_addr != NULL) {
        inet_ntop(addr->sa_family, raw_addr, text, sizeof(text));
    }
    printf("[HM-DNS] nameserver: %s\n", text);
    return 0;
}

static void log_dns_configuration(void)
{
    int ret = dns_foreach_nameserver(log_dns_nameserver, NULL);
    if (ret < 0) {
        printf("[HM-DNS] no nameserver configured: %d\n", ret);
    }
}
#else
static void log_dns_configuration(void)
{
}
#endif

static int simple_entropy_func(void* data, unsigned char* output, size_t len)
{
    (void)data;
    if (agent_secure_random(output, len) == 0) {
        return 0;
    }
    /* No fallback — cryptographic entropy is mandatory for TLS.
     * Returning an error forces the TLS handshake to fail safely
     * rather than proceeding with predictable key material. */
    syslog(LOG_ERR, "[vela_tls] CRITICAL: No secure entropy source available\n");
    return -1;  /* Generic error - TLS handshake will fail safely */
}

static const char* TAG = "vela_tls";

/* ── Chunked transfer decoding ───────────────────────────────── */

/**
 * Decode chunked transfer encoding in-place.
 * Format: <hex-size>\r\n<data>\r\n ... 0\r\n\r\n
 * Returns the decoded length.
 */
static size_t decode_chunked(char* buf, size_t len)
{
    char* src = buf;
    char* end = buf + len;
    char* dst = buf;

    while (src < end) {
        /* Find end of chunk-size line */
        char* crlf = (char*)memmem(src, (size_t)(end - src), "\r\n", 2);
        if (!crlf)
            break;

        /* Parse hex chunk size */
        char* endptr;
        long chunk_sz = strtol(src, &endptr, 16);

        /* Validate: endptr should reach the CRLF (skip spaces) */
        while (endptr < crlf && *endptr == ' ')
            endptr++;

        if (endptr != crlf || chunk_sz < 0 || chunk_sz > (long)(end - crlf - 2))
            break;  /* malformed or oversized chunk header */

        if (chunk_sz == 0)
            break;  /* final chunk */

        src = crlf + 2;  /* skip past chunk-size CRLF */

        /* Clamp to available data */
        if (src + chunk_sz > end)
            chunk_sz = (long)(end - src);

        memmove(dst, src, (size_t)chunk_sz);
        dst += chunk_sz;
        src += chunk_sz;

        /* Skip trailing CRLF after chunk data */
        if (src + 2 <= end && src[0] == '\r' && src[1] == '\n')
            src += 2;
    }

    return (size_t)(dst - buf);
}

/* Return 1 only after the complete chunked body, including the final
 * trailer terminator, is present in buf.  A keep-alive response has no EOF
 * to delimit its body, so waiting for the peer to close would otherwise
 * leave the reader spinning on WANT_READ until the socket timeout. */
static int chunked_body_complete(const char* buf, size_t len)
{
    const char* src = buf;
    const char* end = buf + len;

    while (src < end) {
        const char* crlf = (const char*)memmem(
            src, (size_t)(end - src), "\r\n", 2);
        if (!crlf)
            return 0;

        const char* size_end = crlf;
        const char* extension = (const char*)memchr(
            src, ';', (size_t)(crlf - src));
        if (extension)
            size_end = extension;

        char* endptr;
        long chunk_sz = strtol(src, &endptr, 16);
        while (endptr < size_end
            && (*endptr == ' ' || *endptr == '\t')) {
            endptr++;
        }
        if (endptr != size_end || chunk_sz < 0)
            return -1;

        src = crlf + 2;
        if (chunk_sz == 0) {
            /* No trailers: the empty trailer section is just CRLF. */
            if ((size_t)(end - src) >= 2
                && src[0] == '\r' && src[1] == '\n') {
                return 1;
            }
            /* With trailers, the section ends at an additional CRLF. */
            return memmem(src, (size_t)(end - src), "\r\n\r\n", 4)
                ? 1 : 0;
        }

        if ((size_t)(end - src) < (size_t)chunk_sz + 2)
            return 0;
        src += chunk_sz;
        if (src[0] != '\r' || src[1] != '\n')
            return -1;
        src += 2;
    }

    return 0;
}

/* ── TLS context ─────────────────────────────────────────────── */

typedef struct {
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config cfg;
    mbedtls_net_context net;
    mbedtls_ctr_drbg_context ctr_drbg;
    int recv_timeout_ms;
    char peer_host[128];
    char peer_port[8];
    int bio_probe_done;
} tls_ctx_t;

#define TLS_DIAG_CAPTURE_MAX 512
static unsigned char s_tls_diag_capture[TLS_DIAG_CAPTURE_MAX];
static size_t s_tls_diag_capture_len;

/* ── TLS BIO: use send()/recv(), NEVER write()/read() ──────────────────
 *
 * ROOT CAUSE (hardware-proven 2026-08-26, /tmp/hm_diag.log):
 * mbedtls_net_send()/mbedtls_net_recv() are implemented on top of POSIX
 * write()/read().  On OpenVela/NuttX + ESP32-S3 a read() on a *socket* fd
 * never returns queued TCP payload — it just yields EAGAIN forever, so every
 * TLS handshake died reading the 5-byte ServerHello record header (-0x004C,
 * errno=11), for both www.baidu.com and api.xiaomimimo.com.
 *
 * Raw-socket drain tests on the very same stack, using send()/recv(), work
 * perfectly:
 *   www.baidu.com:80        total=20068 calls=27 maxseg=1024   (large, multi-segment)
 *   api.xiaomimimo.com:443  total=403   calls=2                (port 443 fine)
 * So the transport, the MTU, the 443 path and large transfers are all fine;
 * only the write()/read() file-descriptor route is broken.
 *
 * Therefore the BIO callbacks below bypass mbedtls_net_* and call the socket
 * API directly.  Do NOT "simplify" these back to mbedtls_net_send/recv. */
static int tls_net_send_trace(void* context, const unsigned char* buf, size_t len)
{
    tls_ctx_t* ctx = (tls_ctx_t*)context;

    if (s_tls_diag_capture_len == 0 && len <= sizeof(s_tls_diag_capture)) {
        memcpy(s_tls_diag_capture, buf, len);
        s_tls_diag_capture_len = len;
        printf("[HM-TLS] captured first record len=%zu\n", len);
    }

    errno = 0;
    ssize_t ret = send(ctx->net.fd, buf, len, 0);
    printf("[HM-TLS-TX] want=%zu ret=%zd errno=%d\n", len, ret, errno);

    if (ret < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return MBEDTLS_ERR_SSL_WANT_WRITE;
        }
        return MBEDTLS_ERR_NET_SEND_FAILED;
    }

    return (int)ret;
}

int vela_tls_diag_replay(const char* host, const char* port)
{
    if (s_tls_diag_capture_len == 0) {
        printf("[HM-TLS-REPLAY] no captured record\n");
        return -1;
    }

    struct addrinfo hints;
    struct addrinfo* peer = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    int gai = getaddrinfo(host, port, &hints, &peer);
    if (gai != 0 || peer == NULL) {
        printf("[HM-TLS-REPLAY] resolve failed gai=%d errno=%d\n", gai, errno);
        return -1;
    }

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0 || connect(fd, peer->ai_addr, peer->ai_addrlen) != 0) {
        printf("[HM-TLS-REPLAY] connect failed errno=%d\n", errno);
        if (fd >= 0) {
            close(fd);
        }
        freeaddrinfo(peer);
        return -1;
    }
    freeaddrinfo(peer);

    struct timeval timeout = { .tv_sec = 8, .tv_usec = 0 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    unsigned char response[512];
    errno = 0;
    ssize_t sent = send(fd, s_tls_diag_capture, s_tls_diag_capture_len, 0);
    int send_errno = errno;
    errno = 0;
    ssize_t received = recv(fd, response, sizeof(response), 0);
    int recv_errno = errno;
    printf("[HM-TLS-REPLAY] len=%zu sent=%zd send_errno=%d recv=%zd recv_errno=%d\n",
        s_tls_diag_capture_len, sent, send_errno, received, recv_errno);
    close(fd);
    return received > 0 ? 0 : -1;
}

/* Blocking recv() bounded by SO_RCVTIMEO.  See the note above tls_net_send_trace:
 * recv() works on this stack while read() does not.  On SO_RCVTIMEO expiry NuttX
 * yields EAGAIN; report WANT_READ so mbedTLS retries inside the outer handshake
 * deadline instead of tearing the session down.  A return of 0 means the peer
 * closed the connection, which mbedTLS interprets as EOF. */
static int tls_net_recv_trace(void* context, unsigned char* buf, size_t len)
{
    tls_ctx_t* ctx = (tls_ctx_t*)context;
    errno = 0;
    ssize_t ret = recv(ctx->net.fd, buf, len, 0);
    printf("[HM-TLS-RX] want=%zu ret=%zd errno=%d\n", len, ret, errno);

    if (ret < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return MBEDTLS_ERR_SSL_WANT_READ;
        }
        return MBEDTLS_ERR_NET_RECV_FAILED;
    }
    return (int)ret;
}

static void tls_ctx_free(tls_ctx_t* ctx)
{
    mbedtls_ssl_close_notify(&ctx->ssl);
    mbedtls_net_free(&ctx->net);
    mbedtls_ssl_free(&ctx->ssl);
    mbedtls_ssl_config_free(&ctx->cfg);
    mbedtls_ctr_drbg_free(&ctx->ctr_drbg);

#ifdef CONFIG_AI_AGENT_NET_RPMSG
    network_release_resource();
#endif
}

/* ── Persistent connection pool (keep-alive) ─────────────────── */
/* Keeps one live TLS connection per host:port so repeated calls to
 * the same endpoint (Feishu REST, LLM API) skip the TLS handshake. */

#ifdef CONFIG_AI_AGENT_TLS_CONN_POOL_SIZE
#define CONN_POOL_SIZE CONFIG_AI_AGENT_TLS_CONN_POOL_SIZE
#else
#define CONN_POOL_SIZE 2
#endif

typedef struct {
    tls_ctx_t ctx;
    char host[128];
    char port[8];
    bool in_use; /* locked by a request */
    bool valid; /* connection is alive */
} conn_slot_t;

static conn_slot_t s_pool[CONN_POOL_SIZE];
static pthread_mutex_t s_pool_lock = PTHREAD_MUTEX_INITIALIZER;

/* Acquire a slot for host:port.  Returns a locked, connected ctx or NULL. */
static conn_slot_t* pool_acquire(const char* host, const char* port)
{
    pthread_mutex_lock(&s_pool_lock);

    /* 1. Find an existing valid slot for this host:port */
    for (int i = 0; i < CONN_POOL_SIZE; i++) {
        conn_slot_t* s = &s_pool[i];
        if (s->valid && !s->in_use && strcmp(s->host, host) == 0 && strcmp(s->port, port) == 0) {
            s->in_use = true;
            pthread_mutex_unlock(&s_pool_lock);
            return s;
        }
    }

    /* 2. Find an empty slot */
    for (int i = 0; i < CONN_POOL_SIZE; i++) {
        if (!s_pool[i].valid && !s_pool[i].in_use) {
            s_pool[i].in_use = true;
            pthread_mutex_unlock(&s_pool_lock);
            return &s_pool[i];
        }
    }

    /* 3. Evict the first non-in-use slot */
    for (int i = 0; i < CONN_POOL_SIZE; i++) {
        if (!s_pool[i].in_use) {
            tls_ctx_free(&s_pool[i].ctx);
            s_pool[i].valid = false;
            s_pool[i].in_use = true;
            pthread_mutex_unlock(&s_pool_lock);
            return &s_pool[i];
        }
    }

    pthread_mutex_unlock(&s_pool_lock);
    return NULL; /* all slots busy — caller falls back to ephemeral */
}

/* Return a slot to the pool.  keep=true means the connection is still alive. */
static void pool_release(conn_slot_t* s, const char* host, const char* port, bool keep)
{
    pthread_mutex_lock(&s_pool_lock);
    if (keep) {
        strncpy(s->host, host, sizeof(s->host) - 1);
        strncpy(s->port, port, sizeof(s->port) - 1);
        s->valid = true;
    } else {
        tls_ctx_free(&s->ctx);
        s->valid = false;
        s->host[0] = '\0';
    }
    s->in_use = false;
    pthread_mutex_unlock(&s_pool_lock);
}

void vela_tls_pool_cleanup(void)
{
    pthread_mutex_lock(&s_pool_lock);
    for (int i = 0; i < CONN_POOL_SIZE; i++) {
        if (s_pool[i].in_use) {
            syslog(LOG_WARNING,
                "[vela_tls] Pool slot %d still in use at shutdown, skipping\n", i);
            continue;
        }
        if (s_pool[i].valid) {
            tls_ctx_free(&s_pool[i].ctx);
            s_pool[i].valid = false;
        }
        s_pool[i].host[0] = '\0';
    }
    pthread_mutex_unlock(&s_pool_lock);
}

#if defined(MBEDTLS_DEBUG_C)
static void vela_tls_dbg(void* ctx, int level, const char* file, int line,
                         const char* str)
{
    /* file paths are long; print only the basename */
    const char* base = file;
    const char* p;
    for (p = file; *p; p++)
        {
            if (*p == '/' || *p == '\\')
                {
                    base = p + 1;
                }
        }

    printf("[HM-TLS-DBG] %s:%d: %s", base, line, str);
}
#endif

static int tls_prepare_ssl(tls_ctx_t* ctx, const char* host)
{
    int ret = mbedtls_ssl_config_defaults(&ctx->cfg,
        MBEDTLS_SSL_IS_CLIENT,
        MBEDTLS_SSL_TRANSPORT_STREAM,
        MBEDTLS_SSL_PRESET_DEFAULT);
    if (ret != 0) {
        printf("[HM-TLS] ssl_config_defaults FAILED ret=-0x%04x\n", -ret);
        return VELA_TLS_ERR_HANDSHAKE;
    }

    mbedtls_ssl_conf_min_tls_version(&ctx->cfg, MBEDTLS_SSL_VERSION_TLS1_2);
#if defined(MBEDTLS_SSL_PROTO_TLS1_3)
    mbedtls_ssl_conf_max_tls_version(&ctx->cfg, MBEDTLS_SSL_VERSION_TLS1_3);
#else
    mbedtls_ssl_conf_max_tls_version(&ctx->cfg, MBEDTLS_SSL_VERSION_TLS1_2);
#endif

#if defined(MBEDTLS_SSL_ALPN)
    static const char* alpn_protos[] = { "http/1.1", NULL };
    ret = mbedtls_ssl_conf_alpn_protocols(&ctx->cfg, alpn_protos);
    if (ret != 0) {
        syslog(LOG_WARNING,
            "[%s] Failed to set ALPN protocols: -0x%04x (non-fatal)\n",
            TAG, -ret);
    }
#endif

    mbedtls_ssl_conf_authmode(&ctx->cfg, MBEDTLS_SSL_VERIFY_OPTIONAL);
    mbedtls_ssl_conf_rng(&ctx->cfg, mbedtls_ctr_drbg_random, &ctx->ctr_drbg);

#if defined(MBEDTLS_DEBUG_C)
    mbedtls_ssl_conf_dbg(&ctx->cfg, vela_tls_dbg, NULL);

    mbedtls_debug_set_threshold(2);
#endif

    ret = mbedtls_ssl_setup(&ctx->ssl, &ctx->cfg);
    if (ret != 0) {
        printf("[HM-TLS] ssl_setup FAILED ret=-0x%04x\n", -ret);
        return VELA_TLS_ERR_HANDSHAKE;
    }

    return 0;
}

static int tls_ctx_connect(tls_ctx_t* ctx, const char* host, const char* port)
{
    int ret;

#if defined(MBEDTLS_SSL_PROTO_TLS1_3)
    psa_status_t psa_status = psa_crypto_init();
    if (psa_status != PSA_SUCCESS) {
        syslog(LOG_ERR, "[%s] psa_crypto_init failed: %ld\n",
            TAG, (long)psa_status);
        printf("[HM-TLS] psa_crypto_init FAILED status=%ld\n", (long)psa_status);
        return VELA_TLS_ERR_HANDSHAKE;
    }
#endif

    mbedtls_ssl_init(&ctx->ssl);
    mbedtls_ssl_config_init(&ctx->cfg);
    mbedtls_net_init(&ctx->net);
    mbedtls_ctr_drbg_init(&ctx->ctr_drbg);
    ctx->peer_host[0] = '\0';
    ctx->peer_port[0] = '\0';
    ctx->bio_probe_done = 0;
    strncpy(ctx->peer_host, host, sizeof(ctx->peer_host) - 1);
    ctx->peer_host[sizeof(ctx->peer_host) - 1] = '\0';
    strncpy(ctx->peer_port, port, sizeof(ctx->peer_port) - 1);
    ctx->peer_port[sizeof(ctx->peer_port) - 1] = '\0';

    /* Seed RNG directly with our robust function */
    const char* pers = "vela_tls";
    if ((ret = mbedtls_ctr_drbg_seed(&ctx->ctr_drbg, simple_entropy_func, NULL,
             (const unsigned char*)pers,
             strlen(pers)))
        != 0) {
        syslog(LOG_ERR, "[%s] ctr_drbg_seed ret=0x%x\n", TAG, -ret);
        printf("[HM-TLS] ctr_drbg_seed FAILED ret=-0x%04x\n", -ret);
        return VELA_TLS_ERR_HANDSHAKE;
    }

    /* Check system time — crucial for TLS certificate validation. */
    time_t now = time(NULL);
    syslog(LOG_DEBUG, "[HM-TLS] Handshake start: Host=%s, UNIX=%ld\n", host, (long)now);

    if (now < 1704067200) { /* Jan 1 2024 */
        syslog(LOG_WARNING, "[%s] Clock too old, forcing to 2026\n", TAG);
        struct timespec ts = { .tv_sec = 1772275200, .tv_nsec = 0 };
        clock_settime(CLOCK_REALTIME, &ts);
    }

    /* TCP connect — use HTTP CONNECT proxy if configured */
#ifdef CONFIG_AI_AGENT_NET_RPMSG
    {
        int res_ret = network_acquire_resource(network_get_connect_timeout() * 1000);
        if (res_ret != 0) {
            syslog(LOG_ERR, "[HM-TLS] Resource acquire failed: %d\n", res_ret);
            return VELA_TLS_ERR_CONNECT;
        }
    }
#endif

    if (http_proxy_is_enabled()) {
        syslog(LOG_INFO, "[HM-NET] Using PROXY tunnel to %s:%s\n", host, port);
        int tunnel_fd = proxy_open_tunnel(host, atoi(port), 30000);
        if (tunnel_fd < 0) {
            syslog(LOG_ERR, "[HM-NET] Proxy tunnel to %s:%s FAILED\n", host, port);
            return VELA_TLS_ERR_CONNECT;
        }
        ctx->net.fd = tunnel_fd;
        syslog(LOG_INFO, "[HM-NET] Proxy tunnel OK, fd=%d\n", tunnel_fd);
    } else {
        syslog(LOG_INFO, "[HM-NET] Direct TCP connect to %s:%s\n", host, port);
        printf("[HM-IOB] available before DNS/TCP: %d\n", iob_navail(false));
        log_dns_configuration();
        printf("[HM-NET] DNS/TCP start: %s:%s\n", host, port);

        /* Connect ourselves instead of calling mbedtls_net_connect().
         *
         * mbedtls_net_connect() resolves with hints.ai_family = AF_UNSPEC, so
         * getaddrinfo() may hand back an IPv6 record first and mbedTLS then
         * builds an AF_INET6 socket.  This board has no usable IPv6 route, yet
         * connect() still reports success, so send() silently buffers and no
         * reply can ever arrive — precisely the symptom we saw (TX ok, every
         * recv EAGAIN).  The raw diagnostic socket worked because it pinned
         * AF_INET.  So pin AF_INET here too and log the address we picked. */
        {
            struct addrinfo chints, *clist = NULL, *ccur;
            memset(&chints, 0, sizeof(chints));
            chints.ai_family = AF_INET; /* IPv4 only — see note above */
            chints.ai_socktype = SOCK_STREAM;

            int cgai = getaddrinfo(host, port, &chints, &clist);
            if (cgai != 0 || clist == NULL) {
                syslog(LOG_ERR, "[HM-NET] getaddrinfo FAILED gai=%d errno=%d\n",
                    cgai, errno);
                printf("[HM-NET] DNS failed gai=%d errno=%d\n", cgai, errno);
                return VELA_TLS_ERR_CONNECT;
            }

            int cfd = -1;
            for (ccur = clist; ccur != NULL; ccur = ccur->ai_next) {
                char ipstr[INET_ADDRSTRLEN] = "?";
                struct sockaddr_in* sin = (struct sockaddr_in*)ccur->ai_addr;
                inet_ntop(AF_INET, &sin->sin_addr, ipstr, sizeof(ipstr));

                cfd = socket(AF_INET, SOCK_STREAM, 0);
                if (cfd < 0) {
                    printf("[HM-NET] socket() failed errno=%d\n", errno);
                    continue;
                }
                errno = 0;
                if (connect(cfd, ccur->ai_addr, ccur->ai_addrlen) == 0) {
                    printf("[HM-NET] connected ipv4=%s fd=%d\n", ipstr, cfd);
                    break;
                }
                printf("[HM-NET] connect %s failed errno=%d\n", ipstr, errno);
                close(cfd);
                cfd = -1;
            }
            freeaddrinfo(clist);

            if (cfd < 0) {
                syslog(LOG_ERR, "[HM-NET] TCP connect FAILED errno=%d\n", errno);
                printf("[HM-NET] DNS/TCP failed errno=%d\n", errno);
                return VELA_TLS_ERR_CONNECT;
            }
            ctx->net.fd = cfd;
        }
        syslog(LOG_INFO, "[HM-NET] TCP connect OK, fd=%d\n", ctx->net.fd);
        printf("[HM-NET] DNS/TCP OK: fd=%d\n", ctx->net.fd);

        /* === PLAIN TCP DIAGNOSTIC (pre-TLS) ===
         * Isolate whether TCP data RX works at all.
         * Socket is still BLOCKING here — perfect for a simple recv test.
         * We send a minimal HTTP/1.0 request to port 80 of the same host
         * and try to read the response.  If this fails too, the bug is in
         * NuttX TCP data-path (not TLS/mbedTLS).                     */
        /* Round 2 (2026-08-26): the blocking-vs-non-blocking hypothesis was
         * DISPROVED — a BLOCKING recv on the TLS socket also times out with
         * EAGAIN (ret=-76).  Two questions remain, both answered here:
         *   Q1  Does a raw TCP socket on port 443 receive anything at all?
         *       Round 1 only ever tested :80.  If :80 works but :443 receives
         *       nothing, the fault is specific to the 443 path (route/AP/
         *       firewall/connect never really completing), not TLS parsing.
         *   Q2  Does a LARGE, multi-segment response arrive intact?
         *       Round 1 read only 511 bytes = one small segment.  ServerHello
         *       + certificate is ~4 KB spread over ~3 full-MTU segments, which
         *       was never actually exercised.  So drain the whole body and
         *       report segment statistics (calls / max segment / total). */
        /* Keep this historical probe in source, but never run it between the
         * production connect() and ClientHello.  Holding the real TLS socket
         * idle while two unrelated probes run changes server and TCP timing,
         * so it cannot provide a valid handshake-path comparison. */
#if 0
        {
            static const int diag_ports[2] = { 80, 443 };
            char diag_buf[1024];
            char diag_hdr[256];

            for (int dp = 0; dp < 2; dp++) {
                int dport = diag_ports[dp];
                char portstr[8];
                snprintf(portstr, sizeof(portstr), "%d", dport);
                snprintf(diag_hdr, sizeof(diag_hdr),
                    "GET / HTTP/1.0\r\nHost: %s\r\nConnection: close\r\n\r\n",
                    host);

                printf("[HM-DIAG] === raw TCP drain -> %s:%d ===\n", host, dport);

                int pfd = socket(AF_INET, SOCK_STREAM, 0);
                if (pfd < 0) {
                    printf("[HM-DIAG] socket() failed errno=%d\n", errno);
                    continue;
                }

                struct addrinfo hints, *ai0;
                memset(&hints, 0, sizeof(hints));
                hints.ai_family = AF_INET;
                hints.ai_socktype = SOCK_STREAM;
                int gai = getaddrinfo(host, portstr, &hints, &ai0);
                if (gai != 0 || ai0 == NULL) {
                    printf("[HM-DIAG] getaddrinfo :%d failed gai=%d\n", dport, gai);
                    close(pfd);
                    continue;
                }
                struct sockaddr_in sa;
                memcpy(&sa, ai0->ai_addr, ai0->ai_addrlen);
                freeaddrinfo(ai0);

                if (connect(pfd, (struct sockaddr*)&sa, sizeof(sa)) != 0) {
                    printf("[HM-DIAG] connect :%d FAILED errno=%d\n", dport, errno);
                    close(pfd);
                    continue;
                }
                printf("[HM-DIAG] connect :%d OK fd=%d\n", dport, pfd);

                struct timeval dtv;
                dtv.tv_sec = 8;
                dtv.tv_usec = 0;
                setsockopt(pfd, SOL_SOCKET, SO_RCVTIMEO, &dtv, sizeof(dtv));

                errno = 0;
                ssize_t sent = send(pfd, diag_hdr, strlen(diag_hdr), 0);
                printf("[HM-DIAG] sent=%zd errno=%d\n", sent, errno);

                long total = 0;
                int calls = 0;
                ssize_t biggest = 0;
                int closed = 0;
                for (;;) {
                    errno = 0;
                    ssize_t n = recv(pfd, diag_buf, sizeof(diag_buf), 0);
                    calls++;
                    if (n > 0) {
                        total += n;
                        if (n > biggest) {
                            biggest = n;
                        }
                        if (calls <= 4) {
                            printf("[HM-DIAG]   recv#%d n=%zd total=%ld\n",
                                calls, n, total);
                        }
                        if (total > 20000) {
                            printf("[HM-DIAG]   (stop early at %ld bytes)\n", total);
                            break;
                        }
                    } else if (n == 0) {
                        closed = 1;
                        break;
                    } else {
                        printf("[HM-DIAG]   recv#%d err errno=%d (%s)\n",
                            calls, errno, strerror(errno));
                        break;
                    }
                }
                printf("[HM-DIAG] :%d RESULT total=%ld calls=%d maxseg=%zd closed=%d iob=%d\n",
                    dport, total, calls, biggest, closed, iob_navail(false));
                close(pfd);
            }
            printf("[HM-DIAG] drain tests done, resuming TLS...\n");
        }
        /* === END PLAIN TCP DIAGNOSTIC === */
#endif
    }

    /* Do NOT call mbedtls_net_set_block() / mbedtls_net_set_nonblock() here.
     *
     * Both are implemented as
     *     fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) [&~|] O_NONBLOCK)
     * and on OpenVela/NuttX the F_GETFL on a *socket* fd does not behave like
     * POSIX expects.  When it returns -1 the expression degenerates to writing
     * ~0 back through F_SETFL, which corrupts the descriptor's access mode: the
     * socket keeps accepting send() but every recv() then fails with EAGAIN.
     *
     * That is exactly the failure we chased for two days: the raw diagnostic
     * socket (which never touches fcntl) drains 20068 bytes from :80 and 403
     * bytes from :443 with plain send()/recv(), while the mbedTLS socket — same
     * host, same port, same API — always returned EAGAIN.  Non-blocking and
     * blocking variants both failed, because the fcntl call itself was the
     * common factor.
     *
     * A freshly connected NuttX socket is already blocking, so simply bound the
     * wait with SO_RCVTIMEO / SO_SNDTIMEO and leave the flags alone. */
    ctx->recv_timeout_ms = 10000;
    {
        struct timeval tv;
        tv.tv_sec = 10;
        tv.tv_usec = 0;
        if (setsockopt(ctx->net.fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) != 0) {
            printf("[HM-TLS] SO_RCVTIMEO failed errno=%d\n", errno);
        }
        /* Do not set SO_SNDTIMEO here.  On this NuttX ESP32-S3 socket
         * implementation, the timeout can make send() report success without
         * placing the TLS record on the wire.  The handshake deadline below
         * remains the bounded failure path for stalled writes. */
        /* Diagnostic: confirm what F_GETFL actually reports on this stack. */
        errno = 0;
        int fl = fcntl(ctx->net.fd, F_GETFL);
        printf("[HM-TLS] no-fcntl mode; F_GETFL=%d errno=%d O_NONBLOCK=%d rcvtimeo=10s\n",
            fl, errno, (int)O_NONBLOCK);
    }

    /* Configure SSL only after the TCP connection exists.  Hardware tests
     * show mbedtls_ssl_setup() prevents a new TCP SYN from completing on this
     * ESP32-S3/NuttX build, while the historical TLS 1.2 ordering could connect
     * and exchange the first server flight. */
    ret = tls_prepare_ssl(ctx, host);
    if (ret != 0) {
        return ret;
    }

    ret = mbedtls_ssl_set_hostname(&ctx->ssl, host);
    if (ret != 0) {
        printf("[HM-TLS] ssl_set_hostname FAILED ret=-0x%04x\n", -ret);
        return VELA_TLS_ERR_HANDSHAKE;
    }

    /* Use direct non-blocking I/O — no select()/poll().  Trace the BIO boundary so
     * hardware diagnostics can distinguish TCP delivery from TLS parsing. */
    mbedtls_ssl_set_bio(&ctx->ssl, ctx,
        tls_net_send_trace, tls_net_recv_trace, NULL);

    /* Handshake with overall deadline.
     * Individual reads are bounded by SO_RCVTIMEO, but we also need
     * an overall deadline to prevent infinite retry loops on
     * WANT_READ/WANT_WRITE. */
    syslog(LOG_INFO, "[HM-TLS] TLS handshake starting: host=%s\n", host);
    printf("[HM-TLS] Handshake start: %s\n", host);
    syslog(LOG_INFO, "[HM-TLS] TLS version range: 1.2 - %s\n",
#if defined(MBEDTLS_SSL_PROTO_TLS1_3)
        "1.3");
#else
        "1.2");
#endif
    syslog(LOG_INFO, "[HM-TLS] SNI hostname: %s\n", host);
    syslog(LOG_INFO, "[HM-TLS] Certificate verification: OPTIONAL (no CA bundle)\n");

    struct timespec handshake_start;
    clock_gettime(CLOCK_MONOTONIC, &handshake_start);
    const int HANDSHAKE_TIMEOUT_SEC = 30;

    while ((ret = mbedtls_ssl_handshake(&ctx->ssl)) != 0) {
        if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
#if defined(MBEDTLS_ERROR_C)
            char err_buf[128];
            mbedtls_strerror(ret, err_buf, sizeof(err_buf));
            syslog(LOG_ERR, "[HM-TLS] Handshake FAILED: ret=-0x%04x (%s), errno=%d\n",
                -ret, err_buf, errno);
            printf("[HM-TLS] Handshake error: -0x%04x: %s\n", -ret, err_buf);
            printf("[HM-TLS] socket errno: %d\n", errno);
#else
            syslog(LOG_ERR, "[HM-TLS] Handshake FAILED: ret=-0x%04x, errno=%d\n", -ret, errno);
            printf("[HM-TLS] Handshake error: -0x%04x\n", -ret);
#endif
            /* Log if it was a fatal alert */
            if (ret == MBEDTLS_ERR_SSL_FATAL_ALERT_MESSAGE) {
                syslog(LOG_ERR, "[HM-TLS] Server sent fatal alert message\n");
            }
            return VELA_TLS_ERR_HANDSHAKE;
        }

        /* Check overall handshake deadline */
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        long elapsed_sec = now.tv_sec - handshake_start.tv_sec;
        if (elapsed_sec >= HANDSHAKE_TIMEOUT_SEC) {
            syslog(LOG_ERR, "[HM-TLS] Handshake TIMEOUT after %lds\n", elapsed_sec);
            return VELA_TLS_ERR_HANDSHAKE;
        }
    }

    struct timespec handshake_end;
    clock_gettime(CLOCK_MONOTONIC, &handshake_end);
    long handshake_ms = (handshake_end.tv_sec - handshake_start.tv_sec) * 1000 +
        (handshake_end.tv_nsec - handshake_start.tv_nsec) / 1000000;

    syslog(LOG_INFO, "[HM-TLS] Handshake OK: version=%s, cipher=%s (%ldms)\n",
        mbedtls_ssl_get_version(&ctx->ssl),
        mbedtls_ssl_get_ciphersuite(&ctx->ssl),
        handshake_ms);
    printf("[HM-TLS] Handshake OK: %s, %s (%ldms)\n",
        mbedtls_ssl_get_version(&ctx->ssl),
        mbedtls_ssl_get_ciphersuite(&ctx->ssl),
        handshake_ms);

    /* Restore the longer application-response timeout after negotiation. */
#ifdef CONFIG_AI_AGENT_NET_RPMSG
    ctx->recv_timeout_ms = network_get_read_timeout() * 1000;
#else
    ctx->recv_timeout_ms = AGENT_LLM_SOCKET_TIMEOUT_SEC * 1000;
#endif

    return 0;
}

/* ── HTTP/1.1 framing ────────────────────────────────────────── */

static int tls_write_request(tls_ctx_t* ctx,
    const char* method, const char* host,
    const char* path,
    const vela_header_t* headers,
    const char* body, size_t body_len)
{
    printf("[HM-NET] HTTP write start: %s %s\n", method, path);
    /* Heap-allocate header buffer to reduce stack pressure.
     * This function is called from threads with limited stack
     * (outbound dispatch 16KB) and the TLS context already
     * consumes significant stack space. */
    char* hdr = malloc(4096);
    if (!hdr)
        return VELA_TLS_ERR_OVERFLOW;

    int pos = 0;
    int ret;

#define HDR_APPEND(fmt, ...)                                    \
    pos += snprintf(hdr + pos, 4096 - pos, fmt, ##__VA_ARGS__); \
    if (pos >= 4096) {                                          \
        free(hdr);                                              \
        return VELA_TLS_ERR_OVERFLOW;                           \
    }

    HDR_APPEND("%s %s HTTP/1.1\r\n", method, path);
    HDR_APPEND("Host: %s\r\n", host);
    /* LLM responses are frequently chunked.  Closing each HTTP/TLS
     * exchange avoids reusing a stale NuttX socket while also giving
     * responses without Content-Length a reliable EOF delimiter. */
    HDR_APPEND("Connection: close\r\n");
    HDR_APPEND("User-Agent: agent-vela/1.0\r\n");

    if (body && body_len > 0) {
        HDR_APPEND("Content-Length: %zu\r\n", body_len);
    }

    if (headers) {
        for (const vela_header_t* h = headers; h->name != NULL; h++) {
            HDR_APPEND("%s: %s\r\n", h->name, h->value);
        }
    }

    HDR_APPEND("\r\n");
#undef HDR_APPEND

    /* Write headers */
    int written = 0;
    while (written < pos) {
        ret = mbedtls_ssl_write(&ctx->ssl,
            (const unsigned char*)(hdr + written),
            (size_t)(pos - written));
        if (ret > 0) {
            written += ret;
        } else if (ret == 0) {
            free(hdr);
            return VELA_TLS_ERR_WRITE;
        } else if (ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
            free(hdr);
            return VELA_TLS_ERR_WRITE;
        }
    }

    free(hdr);

    /* Write body */
    if (body && body_len > 0) {
        size_t bw = 0;
        while (bw < body_len) {
            ret = mbedtls_ssl_write(&ctx->ssl,
                (const unsigned char*)(body + bw),
                body_len - bw);
            if (ret > 0) {
                bw += (size_t)ret;
            } else if (ret == 0) {
                return VELA_TLS_ERR_WRITE;
            } else if (ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
                return VELA_TLS_ERR_WRITE;
            }
        }
    }

    printf("[HM-NET] HTTP write OK\n");
    return 0;
}

/**
 * Read full HTTP/1.1 response.
 * Returns HTTP status code; writes body into resp_buf (NUL-terminated).
 * Handles Transfer-Encoding: chunked and Content-Length.
 */
#define TLS_RAW_BUF_SIZE 8192  /* 8KB: enough for HTTP headers + initial body */

/* Number of static raw buffers — at most 2, scaled to pool size.
 * Each buffer is 8KB; pool=1 uses 1 buffer, pool>=2 uses 2. */
#define TLS_RAW_BUF_COUNT (CONN_POOL_SIZE < 2 ? 1 : 2)

static char s_tls_raw_buf[TLS_RAW_BUF_COUNT][TLS_RAW_BUF_SIZE];
static pthread_mutex_t s_tls_raw_lock[TLS_RAW_BUF_COUNT];
static pthread_once_t s_tls_raw_once = PTHREAD_ONCE_INIT;

static void tls_raw_init_once(void)
{
    for (int i = 0; i < TLS_RAW_BUF_COUNT; i++) {
        pthread_mutex_init(&s_tls_raw_lock[i], NULL);
    }
}

static char* tls_raw_acquire(void)
{
    pthread_once(&s_tls_raw_once, tls_raw_init_once);
    for (int i = 0; i < TLS_RAW_BUF_COUNT; i++) {
        if (pthread_mutex_trylock(&s_tls_raw_lock[i]) == 0) {
            return s_tls_raw_buf[i];
        }
    }
    /* All busy — block on first */
    pthread_mutex_lock(&s_tls_raw_lock[0]);
    return s_tls_raw_buf[0];
}

static void tls_raw_release(char* buf)
{
    for (int i = 0; i < TLS_RAW_BUF_COUNT; i++) {
        if (buf == s_tls_raw_buf[i]) {
            pthread_mutex_unlock(&s_tls_raw_lock[i]);
            return;
        }
    }
}

static int tls_read_response(tls_ctx_t* ctx, char* resp_buf, size_t resp_cap,
    size_t* out_body_len, bool* out_keep_alive)
{
    printf("[HM-NET] HTTP read start\n");
    /* Use static buffers to avoid heap fragmentation */
    char* raw = tls_raw_acquire();
    size_t raw_len = 0;
    int eof = 0;
    int ret;

    /* Read until we have the full header (double CRLF) or buffer full */
    while (!eof && raw_len < TLS_RAW_BUF_SIZE - 1) {
        ret = mbedtls_ssl_read(&ctx->ssl,
            (unsigned char*)(raw + raw_len),
            TLS_RAW_BUF_SIZE - 1 - raw_len);
        if (ret > 0) {
            raw_len += (size_t)ret;
            if (memmem(raw, raw_len, "\r\n\r\n", 4))
                break;
        } else if (ret == 0 || ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {
            eof = 1;
            break;
        } else if (ret != MBEDTLS_ERR_SSL_WANT_READ) {
            syslog(LOG_ERR, "[%s] ssl_read (header) ret=0x%x\n", TAG, -ret);
            tls_raw_release(raw);
            return VELA_TLS_ERR_READ;
        }
    }
    raw[raw_len] = '\0';

    /* Parse status line */
    int http_status = 0;
    if (sscanf(raw, "HTTP/1.%*d %d", &http_status) != 1) {
        syslog(LOG_ERR, "[HM-LLM] Failed to parse HTTP status from: %.80s\n", raw);
        tls_raw_release(raw);
        return VELA_TLS_ERR_READ;
    }
    syslog(LOG_INFO, "[HM-LLM] HTTP response status: %d\n", http_status);
    printf("[HM-NET] HTTP status: %d\n", http_status);

    /* Find header/body split */
    char* body_start = (char*)memmem(raw, raw_len, "\r\n\r\n", 4);
    if (!body_start) {
        resp_buf[0] = '\0';
        tls_raw_release(raw);
        return http_status;
    }

    /* Determine keep-alive from Connection header (default true for HTTP/1.1) */
    if (out_keep_alive) {
        *out_keep_alive = true;  /* HTTP/1.1 default */
        char* conn_hdr = strcasestr(raw, "Connection:");
        if (conn_hdr && conn_hdr < body_start) {
            *out_keep_alive = (strcasestr(conn_hdr, "keep-alive") != NULL);
        }
    }
    body_start += 4; /* skip double CRLF */

    /* Extract content-length if present */
    long content_length = -1;
    {
        char* cl_hdr = strcasestr(raw, "Content-Length:");
        if (cl_hdr && cl_hdr < body_start) {
            cl_hdr += strlen("Content-Length:");
            content_length = strtol(cl_hdr, NULL, 10);
            if (content_length < 0 || content_length > 10 * 1024 * 1024) {
                content_length = -1; /* reject absurd values */
            }
        }
    }
    int chunked = 0;
    {
        char* te_hdr = strcasestr(raw, "Transfer-Encoding:");
        if (te_hdr && te_hdr < body_start) {
            chunked = (strcasestr(te_hdr, "chunked") != NULL);
        }
    }

    /* Initial fragment already in raw buffer */
    size_t initial = (size_t)(raw + raw_len - body_start);
    size_t resp_pos = 0;

    /* Copy initial fragment */
    size_t copy = initial < resp_cap - 1 ? initial : resp_cap - 1;
    memcpy(resp_buf, body_start, copy);
    resp_pos = copy;

    /* Keep reading body.  For chunked responses, stop at the terminating
     * zero-size chunk instead of waiting for EOF on a keep-alive socket. */
    int body_complete = false;
    if (http_status == 204 || http_status == 304
        || (http_status >= 100 && http_status < 200)) {
        body_complete = true;
    } else if (content_length >= 0) {
        body_complete = (resp_pos >= (size_t)content_length);
    } else if (chunked) {
        int chunk_state = chunked_body_complete(resp_buf, resp_pos);
        if (chunk_state < 0) {
            syslog(LOG_ERR, "[%s] malformed chunked response\n", TAG);
            tls_raw_release(raw);
            return VELA_TLS_ERR_READ;
        }
        body_complete = (chunk_state > 0);
    }

    if (!eof && !body_complete) {
        while (resp_pos < resp_cap - 1 && !body_complete) {
            ret = mbedtls_ssl_read(&ctx->ssl,
                (unsigned char*)(resp_buf + resp_pos),
                resp_cap - 1 - resp_pos);
            if (ret > 0) {
                resp_pos += (size_t)ret;
                if (content_length >= 0) {
                    body_complete = (resp_pos >= (size_t)content_length);
                } else if (chunked) {
                    int chunk_state = chunked_body_complete(
                        resp_buf, resp_pos);
                    if (chunk_state < 0) {
                        syslog(LOG_ERR,
                            "[%s] malformed chunked response\n", TAG);
                        tls_raw_release(raw);
                        return VELA_TLS_ERR_READ;
                    }
                    body_complete = (chunk_state > 0);
                }
            } else if (ret == 0
                || ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {
                eof = 1;
            } else if (ret == MBEDTLS_ERR_SSL_WANT_READ) {
                /* A body that is not framed yet is incomplete, not a
                 * successful HTTP response.  Let the caller close it so
                 * the next request starts with a fresh connection. */
                syslog(LOG_WARNING,
                    "[%s] response body read timed out before framing completed\n",
                    TAG);
                tls_raw_release(raw);
                return VELA_TLS_ERR_READ;
            } else {
                tls_raw_release(raw);
                return VELA_TLS_ERR_READ;
            }
        }
    }

    if (!body_complete && !eof && resp_pos >= resp_cap - 1) {
        syslog(LOG_ERR, "[%s] response body exceeds buffer\n", TAG);
        tls_raw_release(raw);
        return VELA_TLS_ERR_OVERFLOW;
    }

    resp_buf[resp_pos] = '\0';
    tls_raw_release(raw);

    /* Chunked decode */
    if (chunked) {
        resp_pos = decode_chunked(resp_buf, resp_pos);
        resp_buf[resp_pos] = '\0';
    }

    if (out_body_len)
        *out_body_len = resp_pos;

    return http_status;
}

/* ── Public API ──────────────────────────────────────────────── */

int vela_https_request(
    const char* host,
    const char* port,
    const char* method,
    const char* path,
    const vela_header_t* headers,
    const char* body,
    size_t body_len,
    char* resp_buf,
    size_t resp_cap,
    size_t* out_body_len)
{
    int ret;

    syslog(LOG_INFO, "[HM-LLM] HTTPS %s %s:%s%s (body=%zu bytes)\n",
        method, host, port, path, body_len);

    /* Try to get a pooled connection first */
    conn_slot_t* slot = pool_acquire(host, port);

    if (slot && slot->valid) {
        /* Drain any leftover data from previous response before reuse.
         * Without this, a partially-read response body (e.g. truncated
         * chunked data) would be misinterpreted as the next HTTP status. */
        if (slot->ctx.net.fd >= 0) {
            unsigned char drain[512];
            int dr;
            /* Temporarily set a very short socket timeout to drain without blocking */
            struct timeval tv_drain = { .tv_sec = 0, .tv_usec = 10000 }; /* 10ms */
            struct timeval tv_orig = { .tv_sec = AGENT_LLM_SOCKET_TIMEOUT_SEC,
                .tv_usec = 0 };
            setsockopt(slot->ctx.net.fd, SOL_SOCKET, SO_RCVTIMEO,
                &tv_drain, sizeof(tv_drain));
            while ((dr = mbedtls_ssl_read(&slot->ctx.ssl, drain, sizeof(drain))) > 0)
                ; /* discard leftover bytes */
            /* Restore original timeout */
            setsockopt(slot->ctx.net.fd, SOL_SOCKET, SO_RCVTIMEO,
                &tv_orig, sizeof(tv_orig));
            /* If peer closed the connection, reconnect */
            if (dr == 0 || dr == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {
                goto pool_reconnect;
            }
        } else {
            goto pool_reconnect;
        }

        /* Reuse existing connection — skip TLS handshake */
        syslog(LOG_DEBUG, "[%s] Reusing pooled connection to %s:%s\n", TAG, host, port);
        ret = tls_write_request(&slot->ctx, method, host, path, headers, body, body_len);
        if (ret == 0) {
            bool keep = false;
            ret = tls_read_response(&slot->ctx, resp_buf, resp_cap, out_body_len, &keep);
            if (ret > 0) {
                pool_release(slot, host, port, keep);
                return ret;
            }
        }
        /* Connection went stale — fall through to reconnect */
    pool_reconnect:
        syslog(LOG_INFO, "[%s] Pooled connection stale, reconnecting\n", TAG);
        tls_ctx_free(&slot->ctx);
        slot->valid = false;
    }

    /* New connection */
    if (!slot) {
        /* Pool full — use a temporary ephemeral context */
        tls_ctx_t tmp_ctx;
        if ((ret = tls_ctx_connect(&tmp_ctx, host, port)) != 0) {
            tls_ctx_free(&tmp_ctx);
            return ret;
        }
        if ((ret = tls_write_request(&tmp_ctx, method, host, path,
                 headers, body, body_len))
            != 0) {
            syslog(LOG_ERR, "[%s] Write request failed: %d\n", TAG, ret);
            tls_ctx_free(&tmp_ctx);
            return ret;
        }
        ret = tls_read_response(&tmp_ctx, resp_buf, resp_cap, out_body_len, NULL);
        tls_ctx_free(&tmp_ctx);
        return ret;
    }

    /* Connect into the slot */
    if ((ret = tls_ctx_connect(&slot->ctx, host, port)) != 0) {
        pool_release(slot, host, port, false);
        return ret;
    }

    if ((ret = tls_write_request(&slot->ctx, method, host, path,
             headers, body, body_len))
        != 0) {
        syslog(LOG_ERR, "[%s] Write request failed: %d\n", TAG, ret);
        pool_release(slot, host, port, false);
        return ret;
    }

    bool keep = false;
    ret = tls_read_response(&slot->ctx, resp_buf, resp_cap, out_body_len, &keep);
    pool_release(slot, host, port, ret > 0 && keep);
    return ret;
}

int vela_https_get(const char* host, const char* port, const char* path,
    char* resp_buf, size_t resp_cap)
{
    return vela_https_request(host, port, "GET", path, NULL, NULL, 0,
        resp_buf, resp_cap, NULL);
}

int vela_https_post_json(const char* host, const char* port, const char* path,
    const vela_header_t* extra_headers,
    const char* json_body,
    char* resp_buf, size_t resp_cap)
{
    /* Build a merged header list: Content-Type first, then caller extras */
    const int MAX_HDRS = 32;
    vela_header_t merged[MAX_HDRS];
    int n = 0;

    merged[n++] = (vela_header_t) { "Content-Type", "application/json" };

    if (extra_headers) {
        for (const vela_header_t* h = extra_headers; h->name && n < MAX_HDRS - 1; h++) {
            merged[n++] = *h;
        }
    }
    merged[n] = (vela_header_t) { NULL, NULL };

    size_t body_len = json_body ? strlen(json_body) : 0;
    return vela_https_request(host, port, "POST", path, merged,
        json_body, body_len, resp_buf, resp_cap, NULL);
}

int vela_https_head_date(const char* host, const char* port, const char* path,
    char* date_out, size_t date_cap)
{
    tls_ctx_t ctx;
    int ret;

    if ((ret = tls_ctx_connect(&ctx, host, port)) != 0) {
        tls_ctx_free(&ctx);
        return ret;
    }

    if ((ret = tls_write_request(&ctx, "HEAD", host, path, NULL, NULL, 0)) != 0) {
        tls_ctx_free(&ctx);
        return ret;
    }

    /* Read raw response until end of headers (\r\n\r\n) */
    char hdr_buf[2048];
    int total = 0;
    while (total < (int)sizeof(hdr_buf) - 1) {
        ret = mbedtls_ssl_read(&ctx.ssl,
            (unsigned char*)hdr_buf + total,
            sizeof(hdr_buf) - 1 - total);
        if (ret == MBEDTLS_ERR_SSL_WANT_READ)
            continue;
        if (ret <= 0)
            break;
        total += ret;
        hdr_buf[total] = '\0';
        if (strstr(hdr_buf, "\r\n\r\n"))
            break;
    }
    tls_ctx_free(&ctx);

    if (total <= 0)
        return VELA_TLS_ERR_READ;

    /* Find Date: header (case-insensitive prefix search) */
    char* p = strcasestr(hdr_buf, "\r\nDate: ");
    if (!p)
        return VELA_TLS_ERR_READ;
    p += 8; /* skip \r\nDate:  */
    char* eol = strstr(p, "\r\n");
    if (!eol)
        return VELA_TLS_ERR_READ;

    size_t dlen = (size_t)(eol - p);
    if (dlen >= date_cap)
        dlen = date_cap - 1;
    memcpy(date_out, p, dlen);
    date_out[dlen] = '\0';
    return 0;
}

/* ── Plain HTTP (no TLS) POST ─────────────────────────────── */

int vela_http_post_json(const char* host, const char* port, const char* path,
    const vela_header_t* extra_headers,
    const char* json_body,
    char* resp_buf, size_t resp_cap)
{
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    syslog(LOG_INFO, "[HM-NET] DNS resolve: %s:%s\n", host, port);
    int gai = getaddrinfo(host, port, &hints, &res);
    if (gai != 0 || !res) {
        syslog(LOG_ERR, "[HM-NET] DNS resolve FAILED: %s:%s, gai=%d\n", host, port, gai);
        return VELA_TLS_ERR_CONNECT;
    }

    char addr_str[INET_ADDRSTRLEN];
    struct sockaddr_in* sin = (struct sockaddr_in*)res->ai_addr;
    inet_ntop(AF_INET, &sin->sin_addr, addr_str, sizeof(addr_str));
    syslog(LOG_INFO, "[HM-NET] DNS result: %s → %s\n", host, addr_str);

    int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0) {
        freeaddrinfo(res);
        syslog(LOG_ERR, "[HM-NET] socket() FAILED: errno=%d\n", errno);
        return VELA_TLS_ERR_CONNECT;
    }

    syslog(LOG_INFO, "[HM-NET] TCP connect to %s:%s\n", addr_str, port);
    if (connect(fd, res->ai_addr, res->ai_addrlen) < 0) {
        syslog(LOG_ERR, "[HM-NET] TCP connect FAILED: %s:%s, errno=%d\n", addr_str, port, errno);
        close(fd);
        freeaddrinfo(res);
        return VELA_TLS_ERR_CONNECT;
    }
    freeaddrinfo(res);
    syslog(LOG_INFO, "[HM-NET] TCP connect OK, fd=%d\n", fd);

    /* Set read timeout */
    struct timeval tv = { .tv_sec = AGENT_LLM_SOCKET_TIMEOUT_SEC,
        .tv_usec = 0 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    /* Build HTTP request */
    size_t body_len = json_body ? strlen(json_body) : 0;
    char hdr[4096];
    int pos = 0;

#define HTTP_APPEND(fmt, ...)                                               \
    pos += snprintf(hdr + pos, (int)sizeof(hdr) - pos, fmt, ##__VA_ARGS__); \
    if (pos >= (int)sizeof(hdr)) {                                          \
        close(fd);                                                          \
        return VELA_TLS_ERR_OVERFLOW;                                       \
    }

    HTTP_APPEND("POST %s HTTP/1.1\r\n", path);
    HTTP_APPEND("Host: %s\r\n", host);
    HTTP_APPEND("Content-Type: application/json\r\n");
    HTTP_APPEND("Connection: close\r\n");
    HTTP_APPEND("User-Agent: agent/1.0\r\n");
    if (body_len > 0) {
        HTTP_APPEND("Content-Length: %zu\r\n", body_len);
    }
    if (extra_headers) {
        for (const vela_header_t* h = extra_headers; h->name; h++) {
            HTTP_APPEND("%s: %s\r\n", h->name, h->value);
        }
    }
    HTTP_APPEND("\r\n");
#undef HTTP_APPEND

    /* Send header + body */
    if (write(fd, hdr, (size_t)pos) != pos) {
        close(fd);
        return VELA_TLS_ERR_WRITE;
    }
    if (json_body && body_len > 0) {
        if (write(fd, json_body, body_len) != (ssize_t)body_len) {
            close(fd);
            return VELA_TLS_ERR_WRITE;
        }
    }

    /* Read response */
    char* raw = (char*)malloc(TLS_RAW_BUF_SIZE);
    if (!raw) {
        close(fd);
        return VELA_TLS_ERR_READ;
    }
    size_t raw_len = 0;
    int eof = 0;

    /* Read until we have the full header */
    while (!eof && raw_len < TLS_RAW_BUF_SIZE - 1) {
        ssize_t n = read(fd, raw + raw_len, TLS_RAW_BUF_SIZE - 1 - raw_len);
        if (n > 0) {
            raw_len += (size_t)n;
            raw[raw_len] = '\0';
            if (memmem(raw, raw_len, "\r\n\r\n", 4))
                break;
        } else {
            eof = 1;
        }
    }
    raw[raw_len] = '\0';

    /* Parse status */
    int http_status = 0;
    if (sscanf(raw, "HTTP/1.%*d %d", &http_status) != 1) {
        syslog(LOG_ERR, "[HM-LLM] Plain HTTP bad status: %.80s\n", raw);
        free(raw);
        close(fd);
        return VELA_TLS_ERR_READ;
    }
    syslog(LOG_INFO, "[HM-LLM] Plain HTTP response status: %d\n", http_status);

    /* Find body */
    char* body_start = (char*)memmem(raw, raw_len, "\r\n\r\n", 4);
    if (!body_start) {
        resp_buf[0] = '\0';
        free(raw);
        close(fd);
        return http_status;
    }
    body_start += 4;

    /* Check content-length / chunked */
    long content_length = -1;
    {
        char* cl = strcasestr(raw, "Content-Length:");
        if (cl && cl < body_start) {
            content_length = strtol(cl + strlen("Content-Length:"), NULL, 10);
            if (content_length < 0 || content_length > 10 * 1024 * 1024) {
                content_length = -1;
            }
        }
    }
    int chunked = 0;
    {
        char* te = strcasestr(raw, "Transfer-Encoding:");
        if (te && te < body_start) {
            chunked = (strcasestr(te, "chunked") != NULL);
        }
    }

    /* Copy initial fragment */
    size_t initial = (size_t)(raw + raw_len - body_start);
    size_t resp_pos = 0;
    size_t copy = initial < resp_cap - 1 ? initial : resp_cap - 1;
    memcpy(resp_buf, body_start, copy);
    resp_pos = copy;

    /* Keep reading body */
    if (!eof) {
        while (resp_pos < resp_cap - 1) {
            if (content_length >= 0 && (long)resp_pos >= content_length)
                break;
            ssize_t n = read(fd, resp_buf + resp_pos, resp_cap - 1 - resp_pos);
            if (n <= 0)
                break;
            resp_pos += (size_t)n;
        }
    }
    resp_buf[resp_pos] = '\0';
    free(raw);
    close(fd);

    /* Chunked decode */
    if (chunked) {
        resp_pos = decode_chunked(resp_buf, resp_pos);
        resp_buf[resp_pos] = '\0';
    }

    return http_status;
}
