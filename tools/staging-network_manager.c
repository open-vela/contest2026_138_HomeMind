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

#include "network_manager.h"
#include "agent_compat.h"

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

static const char* TAG = "netmgr";

static char s_ip_str[INET_ADDRSTRLEN] = "0.0.0.0";

bool network_is_connected(void)
{
    struct ifaddrs* ifa_list = NULL;
    if (getifaddrs(&ifa_list) == 0) {
        for (struct ifaddrs* ifa = ifa_list; ifa; ifa = ifa->ifa_next) {
            if (!ifa->ifa_addr)
                continue;
            if (ifa->ifa_addr->sa_family != AF_INET)
                continue;
            if (ifa->ifa_name && strncmp(ifa->ifa_name, "lo", 2) == 0)
                continue;

            struct sockaddr_in* sin = (struct sockaddr_in*)ifa->ifa_addr;
            uint32_t addr = ntohl(sin->sin_addr.s_addr);
            if ((addr >> 24) == 127)
                continue; /* 127.x.x.x loopback */

            /* Skip 0.0.0.0 — interface exists but has no IP yet */
            if (addr == 0)
                continue;

            inet_ntop(AF_INET, &sin->sin_addr, s_ip_str, sizeof(s_ip_str));
            syslog(LOG_INFO, "[%s] Found iface %s addr %s\n", TAG,
                ifa->ifa_name ? ifa->ifa_name : "?", s_ip_str);
            freeifaddrs(ifa_list);
            return true;
        }
        freeifaddrs(ifa_list);
    }

    return false;
}

int network_wait_connected(uint32_t timeout_ms)
{
    if (timeout_ms == 0) {
        return network_is_connected() ? OK : ERROR;
    }

    syslog(LOG_INFO, "[HM-NET] Waiting for network (timeout=%ums)\n", timeout_ms);

    struct timespec deadline;
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += (time_t)(timeout_ms / 1000);
    deadline.tv_nsec += (long)((timeout_ms % 1000) * 1000000L);
    if (deadline.tv_nsec >= 1000000000L) {
        deadline.tv_sec++;
        deadline.tv_nsec -= 1000000000L;
    }

    int poll_count = 0;
    while (1) {
        if (network_is_connected()) {
            syslog(LOG_INFO, "[HM-NET] Network connected: IP=%s (after %d polls)\n",
                s_ip_str, poll_count);
            return OK;
        }

        poll_count++;
        if (poll_count % 10 == 0) {
            syslog(LOG_DEBUG, "[HM-NET] Still waiting... (poll #%d)\n", poll_count);
        }

        struct timespec now;
        clock_gettime(CLOCK_REALTIME, &now);
        if (now.tv_sec > deadline.tv_sec || (now.tv_sec == deadline.tv_sec && now.tv_nsec >= deadline.tv_nsec)) {
            syslog(LOG_WARNING, "[HM-NET] Timed out waiting for network after %d polls\n", poll_count);
            return ERROR;
        }

        usleep(500000); /* poll every 500 ms */
    }
}

const char* network_get_ip(void)
{
    network_is_connected(); /* refresh */
    return s_ip_str;
}

/* ── WiFi connect (real hardware only) ───────────────────────── */

#if defined(CONFIG_ARCH_CHIP_GOLDFISH_ARM64) || defined(CONFIG_ARCH_CHIP_QEMU_ARM)
/* On QEMU, virtio-net provides connectivity automatically — but needs
 * ifup/renew */
#include <net/if.h>
#include <nuttx/net/netconfig.h>
#include <sys/ioctl.h>

int network_wifi_connect(const char* iface, const char* ssid,
    const char* pass)
{
    (void)iface;
    (void)ssid;
    (void)pass;
    syslog(LOG_INFO,
        "[%s] QEMU: skipping wifi_connect (virtio-net handles networking)\n",
        TAG);
    return OK;
}

int network_wifi_reconnect(void)
{
    syslog(LOG_INFO, "[%s] QEMU: Initializing eth0...\n", TAG);

    /* Bring up eth0 interface */
    int ret = system("ifup eth0");
    if (ret != 0) {
        syslog(LOG_WARNING, "[%s] ifup eth0 failed: %d\n", TAG, ret);
    }

    /* Request DHCP lease */
    ret = system("renew eth0");
    if (ret != 0) {
        syslog(LOG_WARNING, "[%s] renew eth0 failed: %d\n", TAG, ret);
    }

    /* Wait a bit for network to come up */
    usleep(500000);

    return network_wait_connected(5000);
}

#elif defined(CONFIG_AI_AGENT_NET_RPMSG)
/* ── RPMSG/TUN network via BLE proxy ─────────────────────────── */

#include "config/config_store.h"

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

/* ── Static state ─────────────────────────────────────────────── */

typedef struct {
    char proxy_mode[16];    /* "usrsock" or "tun" */
    char rpmsg_cpu[16];     /* RPMSG target CPU name */
    int connect_timeout;    /* TLS connect timeout (seconds) */
    int read_timeout;       /* TLS read timeout (seconds) */
    int retry_max;          /* HTTP max retries */
    int retry_base_sec;     /* Retry backoff base (seconds) */
} net_config_t;

static net_config_t g_net_config;
static net_status_t g_net_status;

typedef struct {
    net_state_cb_t cb;
    void* arg;
} net_listener_t;

static net_listener_t g_listeners[NET_MAX_LISTENERS];
static int g_listener_count;

static pthread_mutex_t g_net_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_net_cond = PTHREAD_COND_INITIALIZER;
static pthread_t g_poll_thread;
static volatile bool g_poll_running;
static volatile bool g_force_check;

/* ── Internal helpers ─────────────────────────────────────────── */

static void notify_listeners(net_state_t state)
{
    for (int i = 0; i < g_listener_count; i++) {
        if (g_listeners[i].cb) {
            g_listeners[i].cb(state, g_listeners[i].arg);
        }
    }
}

static void set_net_state(net_state_t new_state)
{
    pthread_mutex_lock(&g_net_mutex);
    if (g_net_status.state != new_state) {
        net_state_t old = g_net_status.state;
        g_net_status.state = new_state;

        if (new_state == NET_STATE_CONNECTED) {
            g_net_status.connected_since = time(NULL);
        }

        syslog(LOG_INFO, "[%s] State: %s -> %s\n", TAG,
            old == NET_STATE_CONNECTED ? "CONNECTED" : "DISCONNECTED",
            new_state == NET_STATE_CONNECTED ? "CONNECTED" : "DISCONNECTED");

        notify_listeners(new_state);
        pthread_cond_broadcast(&g_net_cond);
    }
    pthread_mutex_unlock(&g_net_mutex);
}

/**
 * Check network interfaces via getifaddrs().
 * Returns true if a valid non-loopback IPv4 interface is found.
 * Accepts rpmsg*, tun*, eth* etc. Skips lo* and 127.x.x.x / 0.0.0.0.
 */
static bool check_interfaces(void)
{
    struct ifaddrs* ifa_list = NULL;
    bool found = false;

    if (getifaddrs(&ifa_list) != 0) {
        syslog(LOG_WARNING, "[%s] getifaddrs failed: %d\n", TAG, errno);
        return false;
    }

    for (struct ifaddrs* ifa = ifa_list; ifa; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr) {
            continue;
        }
        if (ifa->ifa_addr->sa_family != AF_INET) {
            continue;
        }
        if (ifa->ifa_name && strncmp(ifa->ifa_name, "lo", 2) == 0) {
            continue;
        }

        struct sockaddr_in* sin = (struct sockaddr_in*)ifa->ifa_addr;
        uint32_t addr = ntohl(sin->sin_addr.s_addr);

        /* Skip 127.x.x.x loopback */
        if ((addr >> 24) == 127) {
            continue;
        }
        /* Skip 0.0.0.0 — interface exists but has no IP yet */
        if (addr == 0) {
            continue;
        }

        /* Valid interface found */
        pthread_mutex_lock(&g_net_mutex);
        inet_ntop(AF_INET, &sin->sin_addr,
            g_net_status.ip_addr, sizeof(g_net_status.ip_addr));
        if (ifa->ifa_name) {
            strncpy(g_net_status.iface_name, ifa->ifa_name,
                sizeof(g_net_status.iface_name) - 1);
            g_net_status.iface_name[sizeof(g_net_status.iface_name) - 1] = '\0';
        }
        g_net_status.last_check = time(NULL);
        pthread_mutex_unlock(&g_net_mutex);

        /* Also update the shared s_ip_str for network_get_ip() */
        inet_ntop(AF_INET, &sin->sin_addr, s_ip_str, sizeof(s_ip_str));

        syslog(LOG_DEBUG, "[%s] Found iface %s addr %s\n", TAG,
            ifa->ifa_name ? ifa->ifa_name : "?",
            g_net_status.ip_addr);

        found = true;
        break;
    }

    freeifaddrs(ifa_list);
    return found;
}

/* ── Config loading ────────────────────────────────────────────── */

static void load_net_config(void)
{
    char buf[32] = { 0 };

    /* proxy_mode: default "usrsock" */
    if (claw_config_get("net.proxy_mode", g_net_config.proxy_mode,
            sizeof(g_net_config.proxy_mode))
            != OK
        || g_net_config.proxy_mode[0] == '\0') {
        strncpy(g_net_config.proxy_mode, "usrsock",
            sizeof(g_net_config.proxy_mode) - 1);
        g_net_config.proxy_mode[sizeof(g_net_config.proxy_mode) - 1] = '\0';
    }

    /* rpmsg_cpu: default "ap" */
    if (claw_config_get("net.rpmsg_cpu", g_net_config.rpmsg_cpu,
            sizeof(g_net_config.rpmsg_cpu))
            != OK
        || g_net_config.rpmsg_cpu[0] == '\0') {
        strncpy(g_net_config.rpmsg_cpu, "ap",
            sizeof(g_net_config.rpmsg_cpu) - 1);
        g_net_config.rpmsg_cpu[sizeof(g_net_config.rpmsg_cpu) - 1] = '\0';
    }

    /* connect_timeout: default 15 */
    if (claw_config_get("net.connect_timeout", buf, sizeof(buf)) == OK
        && buf[0] != '\0') {
        g_net_config.connect_timeout = atoi(buf);
    } else {
        g_net_config.connect_timeout = 15;
    }

    /* read_timeout: default 30 */
    memset(buf, 0, sizeof(buf));
    if (claw_config_get("net.read_timeout", buf, sizeof(buf)) == OK
        && buf[0] != '\0') {
        g_net_config.read_timeout = atoi(buf);
    } else {
        g_net_config.read_timeout = 30;
    }

    /* retry_max: default 3 */
    memset(buf, 0, sizeof(buf));
    if (claw_config_get("net.retry_max", buf, sizeof(buf)) == OK
        && buf[0] != '\0') {
        g_net_config.retry_max = atoi(buf);
    } else {
        g_net_config.retry_max = 3;
    }

    /* retry_base_sec: default 2 */
    memset(buf, 0, sizeof(buf));
    if (claw_config_get("net.retry_base_sec", buf, sizeof(buf)) == OK
        && buf[0] != '\0') {
        g_net_config.retry_base_sec = atoi(buf);
    } else {
        g_net_config.retry_base_sec = 2;
    }

    syslog(LOG_INFO,
        "[%s] Config loaded: proxy=%s cpu=%s timeout=%d/%d retry=%d base=%d\n",
        TAG, g_net_config.proxy_mode, g_net_config.rpmsg_cpu,
        g_net_config.connect_timeout, g_net_config.read_timeout,
        g_net_config.retry_max, g_net_config.retry_base_sec);
}

int network_save_proxy_config(const char* mode, const char* cpu_name)
{
    if (!mode || mode[0] == '\0') {
        syslog(LOG_ERR, "[%s] save_proxy_config: mode required\n", TAG);
        return -EINVAL;
    }

    claw_config_set("net.proxy_mode", mode);
    strncpy(g_net_config.proxy_mode, mode,
        sizeof(g_net_config.proxy_mode) - 1);
    g_net_config.proxy_mode[sizeof(g_net_config.proxy_mode) - 1] = '\0';

    if (cpu_name && cpu_name[0] != '\0') {
        claw_config_set("net.rpmsg_cpu", cpu_name);
        strncpy(g_net_config.rpmsg_cpu, cpu_name,
            sizeof(g_net_config.rpmsg_cpu) - 1);
        g_net_config.rpmsg_cpu[sizeof(g_net_config.rpmsg_cpu) - 1] = '\0';
    }

    syslog(LOG_INFO, "[%s] Proxy config saved: mode=%s cpu=%s\n",
        TAG, g_net_config.proxy_mode, g_net_config.rpmsg_cpu);
    return OK;
}

/* ── Config getters ───────────────────────────────────────────── */

int network_get_connect_timeout(void)
{
    return g_net_config.connect_timeout;
}

int network_get_read_timeout(void)
{
    return g_net_config.read_timeout;
}

int network_get_retry_max(void)
{
    return g_net_config.retry_max;
}

int network_get_retry_base_sec(void)
{
    return g_net_config.retry_base_sec;
}

const char* network_get_proxy_mode(void)
{
    return g_net_config.proxy_mode;
}

const char* network_get_rpmsg_cpu(void)
{
    return g_net_config.rpmsg_cpu;
}

/* ── Interface poll thread ────────────────────────────────────── */

static void* iface_poll_thread(void* arg)
{
    (void)arg;
    syslog(LOG_INFO, "[%s] O74I interface poll thread started\n", TAG);

    /* Startup timeout: 30 seconds to get an IP */
    struct timespec start;
    clock_gettime(CLOCK_MONOTONIC, &start);
    bool startup_warned = false;

    while (g_poll_running) {
        bool has_ip = check_interfaces();

        if (has_ip) {
            set_net_state(NET_STATE_CONNECTED);
            startup_warned = false;
        } else {
            set_net_state(NET_STATE_DISCONNECTED);

            /* Check startup timeout */
            if (!startup_warned) {
                struct timespec now;
                clock_gettime(CLOCK_MONOTONIC, &now);
                long elapsed_ms = (now.tv_sec - start.tv_sec) * 1000L
                    + (now.tv_nsec - start.tv_nsec) / 1000000L;
                if (elapsed_ms >= NET_STARTUP_TIMEOUT_MS) {
                    syslog(LOG_WARNING,
                        "[%s] No IP after %d ms startup timeout, "
                        "continuing poll\n",
                        TAG, NET_STARTUP_TIMEOUT_MS);
                    startup_warned = true;
                }
            }
        }

        /* Sleep for poll interval, but wake early on force check */
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += NET_POLL_INTERVAL_MS / 1000;
        ts.tv_nsec += (NET_POLL_INTERVAL_MS % 1000) * 1000000L;
        if (ts.tv_nsec >= 1000000000L) {
            ts.tv_sec++;
            ts.tv_nsec -= 1000000000L;
        }

        pthread_mutex_lock(&g_net_mutex);
        while (!g_force_check && g_poll_running) {
            int rc = pthread_cond_timedwait(&g_net_cond, &g_net_mutex, &ts);
            if (rc == ETIMEDOUT) {
                break;
            }
        }
        g_force_check = false;
        pthread_mutex_unlock(&g_net_mutex);
    }

    syslog(LOG_INFO, "[%s] O74I interface poll thread exiting\n", TAG);
    return NULL;
}

/* ── Public API ───────────────────────────────────────────────── */

int network_register_listener(net_state_cb_t cb, void* arg)
{
    if (!cb) {
        return -EINVAL;
    }

    pthread_mutex_lock(&g_net_mutex);
    if (g_listener_count >= NET_MAX_LISTENERS) {
        pthread_mutex_unlock(&g_net_mutex);
        syslog(LOG_ERR, "[%s] Max listeners (%d) reached\n",
            TAG, NET_MAX_LISTENERS);
        return -ENOMEM;
    }

    g_listeners[g_listener_count].cb = cb;
    g_listeners[g_listener_count].arg = arg;
    g_listener_count++;
    pthread_mutex_unlock(&g_net_mutex);

    return OK;
}

net_state_t network_get_state(void)
{
    net_state_t state;
    pthread_mutex_lock(&g_net_mutex);
    state = g_net_status.state;
    pthread_mutex_unlock(&g_net_mutex);
    return state;
}

int network_get_active_conns(void)
{
    int conns;
    pthread_mutex_lock(&g_net_mutex);
    conns = g_net_status.active_conns;
    pthread_mutex_unlock(&g_net_mutex);
    return conns;
}

int network_get_iob_usage(void)
{
    FILE* fp = NULL;
    int usage_pct = 0;
    char line[128];
    int total = 0;
    int free_cnt = 0;

    fp = fopen("/proc/net/iob", "r");
    if (!fp) {
        syslog(LOG_DEBUG, "[%s] Cannot open /proc/net/iob\n", TAG);
        return 0;
    }

    /* Parse IOB stats — look for total and free counts.
     * Format varies by NuttX version; try common patterns. */
    while (fgets(line, sizeof(line), fp)) {
        if (sscanf(line, "  total:%d", &total) == 1) {
            /* found total */
        } else if (sscanf(line, "  free:%d", &free_cnt) == 1) {
            /* found free */
        }
    }

    fclose(fp);

    if (total > 0) {
        usage_pct = ((total - free_cnt) * 100) / total;
        if (usage_pct < 0) {
            usage_pct = 0;
        }
        if (usage_pct > 100) {
            usage_pct = 100;
        }
    }

    pthread_mutex_lock(&g_net_mutex);
    g_net_status.iob_usage_pct = usage_pct;
    pthread_mutex_unlock(&g_net_mutex);

    return usage_pct;
}

int network_reconnect(void)
{
    syslog(LOG_INFO, "[%s] Reconnect requested\n", TAG);

    pthread_mutex_lock(&g_net_mutex);
    g_net_status.state = NET_STATE_DISCONNECTED;
    memset(g_net_status.ip_addr, 0, sizeof(g_net_status.ip_addr));
    memset(g_net_status.iface_name, 0, sizeof(g_net_status.iface_name));
    g_force_check = true;
    pthread_cond_signal(&g_net_cond);
    pthread_mutex_unlock(&g_net_mutex);

    return OK;
}

int network_set_dns(const char* primary, const char* secondary)
{
    FILE* fp = NULL;
    int ret = ERROR;

    if (!primary || primary[0] == '\0') {
        syslog(LOG_ERR, "[%s] set_dns: primary DNS required\n", TAG);
        return -EINVAL;
    }

    fp = fopen("/tmp/resolv.conf", "w");
    if (!fp) {
        syslog(LOG_ERR, "[%s] Cannot open /tmp/resolv.conf: %d\n",
            TAG, errno);
        return -errno;
    }

    fprintf(fp, "nameserver %s\n", primary);
    if (secondary && secondary[0] != '\0') {
        fprintf(fp, "nameserver %s\n", secondary);
    }

    fclose(fp);

    /* Persist to config_store */
    claw_config_set("net.dns_primary", primary);
    if (secondary && secondary[0] != '\0') {
        claw_config_set("net.dns_secondary", secondary);
    }

    syslog(LOG_INFO, "[%s] DNS configured: %s %s\n", TAG,
        primary, (secondary && secondary[0]) ? secondary : "");

    ret = OK;
    return ret;
}

/* ── BLE state callback stub ──────────────────────────────────── */

static void ble_state_callback(bool connected)
{
    syslog(LOG_INFO, "[%s] BLE state: %s\n", TAG,
        connected ? "connected" : "disconnected");

    pthread_mutex_lock(&g_net_mutex);
    g_net_status.ble_connected = connected;
    pthread_mutex_unlock(&g_net_mutex);

    if (!connected) {
        set_net_state(NET_STATE_DISCONNECTED);
    } else {
        /* BLE reconnected — trigger immediate interface check */
        pthread_mutex_lock(&g_net_mutex);
        g_force_check = true;
        pthread_cond_signal(&g_net_cond);
        pthread_mutex_unlock(&g_net_mutex);
    }
}

/* ── RPMSG init ───────────────────────────────────────────────── */

int network_rpmsg_init(void)
{
    int ret = ERROR;
    char dns_primary[64] = { 0 };
    char dns_secondary[64] = { 0 };

    syslog(LOG_INFO, "[%s] O74I: Initializing RPMSG/TUN network\n", TAG);

    /* Load network config from config_store */
    load_net_config();

    /* Initialize state */
    memset(&g_net_status, 0, sizeof(g_net_status));
    g_net_status.state = NET_STATE_DISCONNECTED;
    g_net_status.ble_connected = true; /* assume BLE up at boot */
    g_listener_count = 0;
    g_force_check = false;

    /* Load DNS config from config_store, use defaults if absent */
    if (claw_config_get("net.dns_primary", dns_primary,
            sizeof(dns_primary))
            != OK
        || dns_primary[0] == '\0') {
        strncpy(dns_primary, "223.5.5.5", sizeof(dns_primary) - 1);
        dns_primary[sizeof(dns_primary) - 1] = '\0';
    }

    if (claw_config_get("net.dns_secondary", dns_secondary,
            sizeof(dns_secondary))
            != OK
        || dns_secondary[0] == '\0') {
        strncpy(dns_secondary, "8.8.8.8", sizeof(dns_secondary) - 1);
        dns_secondary[sizeof(dns_secondary) - 1] = '\0';
    }

    /* Configure DNS */
    network_set_dns(dns_primary, dns_secondary);

    /* Start interface poll thread */
    g_poll_running = true;

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 4096);

    ret = pthread_create(&g_poll_thread, &attr, iface_poll_thread, NULL);
    pthread_attr_destroy(&attr);

    if (ret != 0) {
        syslog(LOG_ERR, "[%s] Failed to create poll thread: %d\n",
            TAG, ret);
        g_poll_running = false;
        return ERROR;
    }

    pthread_setname_np(g_poll_thread, "net_poll");

    /* Register BLE state callback (stub — replace with real BLE API) */
    syslog(LOG_INFO,
        "[%s] BLE callback registered (stub, awaiting BLE framework)\n",
        TAG);
    (void)ble_state_callback; /* suppress unused warning */

    syslog(LOG_INFO, "[%s] O74I network init complete\n", TAG);
    return OK;
}

/* ── Resource guard ───────────────────────────────────────────── */

int network_acquire_resource(uint32_t timeout_ms)
{
    pthread_mutex_lock(&g_net_mutex);

    /* Fast-fail if BLE is disconnected */
    if (!g_net_status.ble_connected) {
        pthread_mutex_unlock(&g_net_mutex);
        return -ENETUNREACH;
    }

    /* Check TCP connection limit and IOB watermark */
    struct timespec deadline;
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += (time_t)(timeout_ms / 1000);
    deadline.tv_nsec += (long)((timeout_ms % 1000) * 1000000L);
    if (deadline.tv_nsec >= 1000000000L) {
        deadline.tv_sec++;
        deadline.tv_nsec -= 1000000000L;
    }

    while (g_net_status.active_conns >= NET_MAX_TCP_CONNS
        || network_get_iob_usage() >= NET_IOB_HIGH_WATERMARK) {
        int rc = pthread_cond_timedwait(&g_net_cond, &g_net_mutex, &deadline);
        if (rc == ETIMEDOUT) {
            pthread_mutex_unlock(&g_net_mutex);
            syslog(LOG_WARNING, "[%s] acquire_resource timeout\n", TAG);
            return -ETIMEDOUT;
        }
        /* Re-check BLE after wakeup */
        if (!g_net_status.ble_connected) {
            pthread_mutex_unlock(&g_net_mutex);
            return -ENETUNREACH;
        }
    }

    g_net_status.active_conns++;
    pthread_mutex_unlock(&g_net_mutex);
    return OK;
}

void network_release_resource(void)
{
    pthread_mutex_lock(&g_net_mutex);
    if (g_net_status.active_conns > 0) {
        g_net_status.active_conns--;
    }
    pthread_cond_broadcast(&g_net_cond);
    pthread_mutex_unlock(&g_net_mutex);
}

/* ── WiFi stubs (redirect to rpmsg_init) ──────────────────────── */

int network_wifi_connect(const char* iface, const char* ssid,
    const char* pass)
{
    (void)iface;
    (void)ssid;
    (void)pass;
    syslog(LOG_INFO,
        "[%s] O74I: wifi_connect redirected to rpmsg_init\n", TAG);
    return network_rpmsg_init();
}

int network_wifi_reconnect(void)
{
    syslog(LOG_INFO,
        "[%s] O74I: wifi_reconnect redirected to rpmsg_init\n", TAG);
    return network_rpmsg_init();
}

#else
/* Real hardware: use NuttX wapi shell command to join WiFi */
#include "infra/config_store.h"
#include "agent_config.h"
#include <netutils/netlib.h>
#include <wireless/wapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <sched.h>
#include <spawn.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <nuttx/lib/builtin.h>
#include <nuttx/mm/iob.h>

/* ── Wi-Fi state machine ──────────────────────────────────────── */

typedef enum {
    WIFI_IDLE = 0,       /* Not started */
    WIFI_CONNECTING,     /* Executing wapi commands */
    WIFI_ASSOCIATED,     /* wapi essid sent, waiting for AP association */
    WIFI_DHCP,           /* Running renew, waiting for IP */
    WIFI_READY,          /* IP address obtained */
    WIFI_RETRY_WAIT,     /* Backoff before retry */
    WIFI_FAILED,         /* Max retries exceeded */
} wifi_state_t;

typedef struct {
    wifi_state_t state;
    char iface[16];
    char ssid[64];
    char pass[128];
    int retry_count;
    int backoff_sec;          /* Current backoff: 5, 10, 20, 40, cap 60 */
    time_t state_enter_time;  /* When we entered current state */
    time_t last_dhcp_renew;   /* Last DHCP request time, 0 before first try */
    bool running;             /* State machine thread running */
    pthread_t thread;
    pthread_mutex_t lock;
} wifi_context_t;

#define WIFI_MAX_RETRIES      5
#define WIFI_BACKOFF_INIT     5
#define WIFI_BACKOFF_MAX      60
#define WIFI_ASSOC_WAIT_SEC   2   /* Start DHCP while the association event is fresh */
#define WIFI_DHCP_WAIT_SEC    30  /* Allow slow router DHCP responses */
#define WIFI_DHCP_POLL_SEC    2   /* Poll interval during DHCP */
#define WIFI_DHCP_RENEW_SEC   6   /* Retry DHCP within the same attempt */

static wifi_context_t g_wifi_ctx = {
    .lock = PTHREAD_MUTEX_INITIALIZER,
};

static const char* wifi_state_name(wifi_state_t s)
{
    switch (s) {
    case WIFI_IDLE:         return "IDLE";
    case WIFI_CONNECTING:   return "CONNECTING";
    case WIFI_ASSOCIATED:   return "ASSOCIATED";
    case WIFI_DHCP:         return "DHCP";
    case WIFI_READY:        return "READY";
    case WIFI_RETRY_WAIT:   return "RETRY_WAIT";
    case WIFI_FAILED:       return "FAILED";
    default:                return "UNKNOWN";
    }
}

static void wifi_set_state(wifi_state_t new_state)
{
    wifi_state_t old = g_wifi_ctx.state;
    g_wifi_ctx.state = new_state;
    g_wifi_ctx.state_enter_time = time(NULL);
    if (new_state == WIFI_DHCP) {
        g_wifi_ctx.last_dhcp_renew = 0;
    }
    syslog(LOG_INFO, "[HM-WIFI] State: %s -> %s (retry=%d, backoff=%ds)\n",
        wifi_state_name(old), wifi_state_name(new_state),
        g_wifi_ctx.retry_count, g_wifi_ctx.backoff_sec);
}

/* Execute a shell command and log it */
static int wifi_run_cmd(const char* label, const char* cmd)
{
    /* Never print the PSK command: it contains the Wi-Fi password. */
    if (strcmp(label, "psk") == 0) {
        syslog(LOG_INFO, "[HM-WIFI] psk: <redacted>\n");
    } else {
        syslog(LOG_INFO, "[HM-WIFI] %s: %s\n", label, cmd);
    }
    int ret = system(cmd);
    syslog(LOG_INFO, "[HM-WIFI] %s: ret=%d\n", label, ret);
    printf("[HM-WIFI] %s ret=%d\n", label, ret);
    return ret;
}

static int wifi_run_builtin_task(const char* name, char* const argv[])
{
    int index = builtin_isavail(name);
    if (index < 0)
        return -ENOENT;

    const struct builtin_s* builtin = builtin_for_index(index);
    if (!builtin || !builtin->main)
        return -ENOENT;

    posix_spawn_file_actions_t actions;
    posix_spawnattr_t attr;
    struct sched_param sched = { .sched_priority = builtin->priority };
    int ret = posix_spawn_file_actions_init(&actions);
    if (ret != 0)
        return -ret;
    ret = posix_spawnattr_init(&attr);
    if (ret != 0) {
        posix_spawn_file_actions_destroy(&actions);
        return -ret;
    }
    ret = posix_spawnattr_setschedparam(&attr, &sched);
    if (ret == 0)
        ret = posix_spawnattr_setstacksize(&attr, builtin->stacksize);
    if (ret != 0) {
        posix_spawnattr_destroy(&attr);
        posix_spawn_file_actions_destroy(&actions);
        return -ret;
    }

    pid_t pid = task_spawn(builtin->name, builtin->main,
        &actions, &attr, argv, NULL);
    posix_spawnattr_destroy(&attr);
    posix_spawn_file_actions_destroy(&actions);
    if (pid < 0)
        return pid;

    int status = 0;
    int tc = ioctl(STDIN_FILENO, TIOCSCTTY, pid);
    do {
        ret = waitpid(pid, &status, WUNTRACED);
    } while (ret < 0 && errno == EINTR);

    if (tc == 0)
        ioctl(STDIN_FILENO, TIOCNOTTY, 0);

    if (ret < 0)
        return errno == ECHILD ? OK : -errno;
    if (!WIFEXITED(status))
        return ERROR;
    return WEXITSTATUS(status);
}

static int wifi_run_dhcp_task(const char* dev)
{
    printf("[HM-IOB] available before DHCP: %d\n", iob_navail(false));
    /* task_spawn() prepends the task name as argv[0] itself
     * (nxtask_setup_stackargs copies the caller argv AFTER it), so pass ONLY
     * the arguments: {dev, NULL} gives renew_main(argc=2,
     * argv={"renew", dev}).  Prepending "renew" here yields argc=3 and
     * renew aborts with "Invalid number of arguments" (seen on hardware
     * 2026-08-28). */
    char* argv[] = { (char*)dev, NULL };
    int ret = wifi_run_builtin_task("renew", argv);
    printf("[HM-DHCP] renew task ret=%d\n", ret);
    return ret;
}

/* Scan once and select the strongest BSSID matching the requested SSID.
 * Some routers expose the same SSID on multiple APs; the ESP32-S3 driver can
 * otherwise remain pinned to a weak BSSID and never complete association. */
static int wifi_find_strongest_ap(const char* dev, const char* ssid,
    char* bssid, size_t bssid_size)
{
    struct wapi_list_s list;
    struct wapi_scan_info_s* info;
    struct ether_addr best_ap;
    int best_signal = -1000;
    bool found = false;
    int ret;
    int sock = wapi_make_socket();

    if (sock < 0) {
        syslog(LOG_WARNING, "[HM-WIFI] Cannot create WAPI socket: %d\n",
            sock);
        return ERROR;
    }

    ret = wapi_scan_init(sock, dev, ssid);
    if (ret < 0) {
        close(sock);
        syslog(LOG_WARNING, "[HM-WIFI] AP scan start failed: %d\n", ret);
        return ERROR;
    }

    for (int tries = 0; tries < 25; tries++) {
        ret = wapi_scan_stat(sock, dev);
        if (ret <= 0) {
            break;
        }
        usleep(200000);
    }

    if (ret < 0) {
        close(sock);
        syslog(LOG_WARNING, "[HM-WIFI] AP scan status failed: %d\n", ret);
        return ERROR;
    }

    memset(&list, 0, sizeof(list));
    ret = wapi_scan_coll(sock, dev, &list);
    close(sock);
    if (ret < 0) {
        syslog(LOG_WARNING, "[HM-WIFI] AP scan collect failed: %d\n", ret);
        return ERROR;
    }

    for (info = list.head.scan; info; info = info->next) {
        if (info->has_essid && info->has_rssi
            && strcmp(info->essid, ssid) == 0
            && info->rssi > best_signal) {
            best_ap = info->ap;
            best_signal = info->rssi;
            found = true;
        }
    }

    if (found) {
        snprintf(bssid, bssid_size, "%02x:%02x:%02x:%02x:%02x:%02x",
            best_ap.ether_addr_octet[0], best_ap.ether_addr_octet[1],
            best_ap.ether_addr_octet[2], best_ap.ether_addr_octet[3],
            best_ap.ether_addr_octet[4], best_ap.ether_addr_octet[5]);
    }
    wapi_scan_coll_free(&list);

    if (!found) {
        syslog(LOG_WARNING, "[HM-WIFI] SSID '%s' not found in scan\n", ssid);
        printf("[HM-WIFI] SSID '%s' not found in scan\n", ssid);
        return ERROR;
    }

    syslog(LOG_INFO, "[HM-WIFI] Selected strongest AP %s (%d dBm)\n",
        bssid, best_signal);
    printf("[HM-WIFI] Selected strongest AP %s (%d dBm)\n",
        bssid, best_signal);
    return OK;
}

static int wifi_apply_native_config(const char* dev, const char* ssid,
    const char* pass, const char* bssid)
{
    struct ether_addr ap;
    int ret;
    int sock;

    if (!ether_aton_r(bssid, &ap)) {
        return -EINVAL;
    }

    /* Keep each WEXT operation on its own control socket.  This was the
     * strongest in-process variant on ESP32-S3 (ESSID ON, 72 Mbps); DHCP is
     * deliberately delegated to the real `renew` builtin below. */
    sock = wapi_make_socket();
    if (sock < 0)
        return sock;
    ret = wapi_set_mode(sock, dev, WAPI_MODE_MANAGED);
    close(sock);
    printf("[HM-WIFI] mode ret=%d\n", ret);
    if (ret < 0)
        return ret;
    usleep(3000000);

    sock = wapi_make_socket();
    if (sock < 0)
        return sock;
    ret = wpa_driver_wext_set_auth_param(sock, dev,
        IW_AUTH_WPA_VERSION, IW_AUTH_WPA_VERSION_WPA2);
    printf("[HM-WIFI] wpa_version ret=%d\n", ret);
    if (ret < 0) {
        close(sock);
        return ret;
    }
    ret = wpa_driver_wext_set_auth_param(sock, dev,
        IW_AUTH_CIPHER_PAIRWISE, IW_AUTH_CIPHER_CCMP);
    printf("[HM-WIFI] cipher ret=%d\n", ret);
    if (ret < 0) {
        close(sock);
        return ret;
    }
    if (pass && pass[0]) {
        ret = wpa_driver_wext_set_key_ext(sock, dev, WPA_ALG_CCMP,
            pass, strlen(pass));
        printf("[HM-WIFI] psk ret=%d\n", ret);
        if (ret < 0) {
            close(sock);
            return ret;
        }
    }
    close(sock);
    usleep(3000000);

    /* Prime the desired SSID.  On ESP32-S3 WEXT the SIOCSIWESSID ioctl
     * commonly returns -1 the first time(s) after the PSK step while the
     * driver is still settling; retry so the essid actually takes.  This
     * pre-association set is non-fatal. */
    for (int i = 0; i < 5; i++) {
        sock = wapi_make_socket();
        if (sock < 0)
            return sock;
        ret = wapi_set_essid(sock, dev, ssid, WAPI_ESSID_ON);
        close(sock);
        printf("[HM-WIFI] essid_pre_ap try=%d ret=%d\n", i + 1, ret);
        if (ret >= 0)
            break;
        usleep(1000000);
    }
    if (ret < 0)
        syslog(LOG_WARNING, "[HM-WIFI] essid_pre_ap non-fatal (%d), continuing\n", ret);
    usleep(8000000);

    /* Lock to the strongest BSSID (SIOCSIWAP).  Retry while the link comes
     * up; a transient -1 is common until the driver begins associating.  This
     * step is non-fatal: if the driver already associated to the chosen SSID
     * it may have selected this BSSID on its own. */
    for (int tries = 1; tries <= 8; tries++) {
        sock = wapi_make_socket();
        if (sock < 0)
            return sock;
        ret = wapi_set_ap(sock, dev, &ap);
        close(sock);
        printf("[HM-WIFI] ap try=%d ret=%d\n", tries, ret);
        if (ret >= 0)
            break;
        usleep(2000000);
    }
    if (ret < 0)
        syslog(LOG_WARNING, "[HM-WIFI] ap lock non-fatal (%d), continuing\n", ret);
    usleep(3000000);

    /* Re-affirm ESSID_ON — this is the step that actually brings the link
     * up (mirrors the final `wapi essid <ssid> on` in the proven manual
     * sequence, STATUS #35).  Retry so a transient -1 does not abort. */
    for (int i = 0; i < 5; i++) {
        sock = wapi_make_socket();
        if (sock < 0)
            return sock;
        ret = wapi_set_essid(sock, dev, ssid, WAPI_ESSID_ON);
        close(sock);
        printf("[HM-WIFI] essid ret=%d (try %d)\n", ret, i + 1);
        if (ret >= 0)
            break;
        usleep(1000000);
    }
    return ret;
}

static void* wifi_state_machine_thread(void* arg)
{
    (void)arg;
    syslog(LOG_INFO, "[HM-WIFI] State machine thread started\n");

    char cmd[512];
    while (g_wifi_ctx.running) {
        pthread_mutex_lock(&g_wifi_ctx.lock);
        wifi_state_t state = g_wifi_ctx.state;
        const char* dev = g_wifi_ctx.iface;
        pthread_mutex_unlock(&g_wifi_ctx.lock);

        switch (state) {
        case WIFI_IDLE:
            /* Nothing to do */
            usleep(500000);
            break;

        case WIFI_CONNECTING:
        {
            syslog(LOG_INFO, "[HM-WIFI] Connecting to SSID='%s' iface=%s\n",
                g_wifi_ctx.ssid, dev);

            int ret = netlib_ifup(dev);
            printf("[HM-WIFI] ifup ret=%d\n", ret);
            if (ret < 0) {
                g_wifi_ctx.retry_count++;
                wifi_set_state(WIFI_RETRY_WAIT);
                break;
            }
            usleep(500000);

            /* Resolve duplicate SSIDs deterministically by locking the
             * strongest visible BSSID. */
            char bssid[18] = { 0 };
            if (wifi_find_strongest_ap(dev, g_wifi_ctx.ssid,
                    bssid, sizeof(bssid))
                == OK) {
                /* A completed ESP32-S3 scan leaves WEXT in scan/old-assoc
                 * state.  Reset the interface before applying the selected
                 * BSSID; this matches the board-proven NSH sequence. */
                ret = netlib_ifdown(dev);
                printf("[HM-WIFI] ifdown_post_scan ret=%d\n", ret);
                usleep(6000000);
                ret = netlib_ifup(dev);
                printf("[HM-WIFI] ifup_post_scan ret=%d\n", ret);
                if (ret < 0) {
                    g_wifi_ctx.retry_count++;
                    wifi_set_state(WIFI_RETRY_WAIT);
                    break;
                }
                usleep(6000000);
                ret = wifi_apply_native_config(dev, g_wifi_ctx.ssid,
                    g_wifi_ctx.pass, bssid);
                if (ret < 0) {
                    g_wifi_ctx.retry_count++;
                    wifi_set_state(WIFI_RETRY_WAIT);
                    break;
                }
            } else {
                g_wifi_ctx.retry_count++;
                wifi_set_state(WIFI_RETRY_WAIT);
                break;
            }

            wifi_set_state(WIFI_ASSOCIATED);
            break;
        }

        case WIFI_ASSOCIATED:
        {
            /* Wait for AP association, then try DHCP.
             * Use polling instead of fixed sleep — check if interface
             * has a carrier-like state by trying renew periodically. */
            time_t elapsed = time(NULL) - g_wifi_ctx.state_enter_time;
            if (elapsed >= WIFI_ASSOC_WAIT_SEC) {
                syslog(LOG_INFO, "[HM-WIFI] Association wait done (%lds), trying DHCP\n",
                    (long)elapsed);
                /* HomeMind: dropped the diagnostic 'wapi show' here — it
                 * returned -1 even on success (noise in every connect log)
                 * and its result was never used; real association feedback
                 * comes from the DHCP renew result. */
                wifi_set_state(WIFI_DHCP);
            } else {
                usleep(1000000); /* 1s tick */
            }
            break;
        }

        case WIFI_DHCP:
        {
            /* Run renew and poll for IP address */
            time_t elapsed = time(NULL) - g_wifi_ctx.state_enter_time;

            time_t now = time(NULL);
            if (g_wifi_ctx.last_dhcp_renew == 0
                || now - g_wifi_ctx.last_dhcp_renew >= WIFI_DHCP_RENEW_SEC) {
                /* Run DHCP outside the small state-machine pthread.  The
                 * dedicated task also reports errno so board failures are
                 * diagnosable instead of collapsing into a generic -1. */
                syslog(LOG_INFO, "[HM-NET] Starting DHCP on %s\n", dev);
                int renew_ret = wifi_run_dhcp_task(dev);
                printf("[HM-WIFI] renew ret=%d\n", renew_ret);
                g_wifi_ctx.last_dhcp_renew = now;
            }

            /* Check if we got an IP */
            if (network_is_connected()) {
                syslog(LOG_INFO, "[HM-NET] DHCP success, IP=%s\n", network_get_ip());
                g_wifi_ctx.retry_count = 0;
                g_wifi_ctx.backoff_sec = WIFI_BACKOFF_INIT;

                /* Persist credentials */
                agent_config_set(AGENT_CFG_KEY_WIFI_SSID, g_wifi_ctx.ssid);
                agent_config_set(AGENT_CFG_KEY_WIFI_PASS, g_wifi_ctx.pass[0] ? g_wifi_ctx.pass : "");
                syslog(LOG_INFO, "[HM-WIFI] Credentials saved to config_store\n");

                wifi_set_state(WIFI_READY);
            } else if (elapsed >= WIFI_DHCP_WAIT_SEC) {
                syslog(LOG_WARNING, "[HM-NET] DHCP failed after %lds\n", (long)elapsed);
                g_wifi_ctx.retry_count++;
                if (g_wifi_ctx.retry_count >= WIFI_MAX_RETRIES) {
                    syslog(LOG_ERR, "[HM-WIFI] Max retries (%d) exceeded\n", WIFI_MAX_RETRIES);
                    wifi_set_state(WIFI_FAILED);
                } else {
                    wifi_set_state(WIFI_RETRY_WAIT);
                }
            } else {
                usleep(WIFI_DHCP_POLL_SEC * 1000000);
            }
            break;
        }

        case WIFI_READY:
            /* Network is up. Poll periodically to detect disconnection. */
            if (!network_is_connected()) {
                syslog(LOG_WARNING, "[HM-NET] Connection lost, will reconnect\n");
                g_wifi_ctx.retry_count = 0;
                g_wifi_ctx.backoff_sec = WIFI_BACKOFF_INIT;
                wifi_set_state(WIFI_CONNECTING);
            } else {
                usleep(5000000); /* Check every 5s */
            }
            break;

        case WIFI_RETRY_WAIT:
        {
            time_t elapsed = time(NULL) - g_wifi_ctx.state_enter_time;
            if (elapsed >= g_wifi_ctx.backoff_sec) {
                syslog(LOG_INFO, "[HM-WIFI] Backoff done (%lds), retrying\n", (long)elapsed);
                wifi_set_state(WIFI_CONNECTING);
            } else {
                usleep(1000000);
            }
            break;
        }

        case WIFI_FAILED:
            /* Stay failed until external reset */
            syslog(LOG_ERR, "[HM-WIFI] FAILED — use 'wifi_reconnect' to retry\n");
            usleep(10000000);
            break;
        }

        /* Update backoff for next retry */
        if (g_wifi_ctx.state == WIFI_RETRY_WAIT) {
            g_wifi_ctx.backoff_sec *= 2;
            if (g_wifi_ctx.backoff_sec > WIFI_BACKOFF_MAX) {
                g_wifi_ctx.backoff_sec = WIFI_BACKOFF_MAX;
            }
        }
    }

    syslog(LOG_INFO, "[HM-WIFI] State machine thread exiting\n");
    return NULL;
}

int network_wifi_connect(const char* iface, const char* ssid,
    const char* pass)
{
    if (!ssid || ssid[0] == '\0') {
        syslog(LOG_ERR, "[HM-WIFI] wifi_connect: SSID required\n");
        return ERROR;
    }

    pthread_mutex_lock(&g_wifi_ctx.lock);

    /* Copy credentials */
    strncpy(g_wifi_ctx.iface, (iface && iface[0]) ? iface : "wlan0",
        sizeof(g_wifi_ctx.iface) - 1);
    g_wifi_ctx.iface[sizeof(g_wifi_ctx.iface) - 1] = '\0';
    strncpy(g_wifi_ctx.ssid, ssid, sizeof(g_wifi_ctx.ssid) - 1);
    g_wifi_ctx.ssid[sizeof(g_wifi_ctx.ssid) - 1] = '\0';
    strncpy(g_wifi_ctx.pass, pass ? pass : "", sizeof(g_wifi_ctx.pass) - 1);
    g_wifi_ctx.pass[sizeof(g_wifi_ctx.pass) - 1] = '\0';

    g_wifi_ctx.retry_count = 0;
    g_wifi_ctx.backoff_sec = WIFI_BACKOFF_INIT;

    /* Start state machine thread if not running */
    if (!g_wifi_ctx.running) {
        g_wifi_ctx.running = true;
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setstacksize(&attr, 4096);
        int ret = pthread_create(&g_wifi_ctx.thread, &attr,
            wifi_state_machine_thread, NULL);
        pthread_attr_destroy(&attr);
        if (ret != 0) {
            syslog(LOG_ERR, "[HM-WIFI] Failed to create state machine thread: %d\n", ret);
            g_wifi_ctx.running = false;
            pthread_mutex_unlock(&g_wifi_ctx.lock);
            return ERROR;
        }
        pthread_setname_np(g_wifi_ctx.thread, "wifi_sm");
    }

    /* Transition to CONNECTING */
    wifi_set_state(WIFI_CONNECTING);
    pthread_mutex_unlock(&g_wifi_ctx.lock);

    /* Wait for READY or FAILED */
    syslog(LOG_INFO, "[HM-WIFI] Waiting for connection (90s timeout)...\n");
    int wait_ms = 90000;
    while (wait_ms > 0) {
        usleep(1000000);
        wait_ms -= 1000;
        wifi_state_t s = g_wifi_ctx.state;
        if (s == WIFI_READY) {
            syslog(LOG_INFO, "[HM-WIFI] Connected! IP=%s\n", network_get_ip());
            return OK;
        }
        if (s == WIFI_FAILED) {
            syslog(LOG_ERR, "[HM-WIFI] Connection FAILED\n");
            return ERROR;
        }
    }

    /* Timeout — state machine continues in background */
    syslog(LOG_WARNING, "[HM-WIFI] Timeout waiting for connection, state machine continues in background\n");
    return network_is_connected() ? OK : ERROR;
}

int network_wifi_reconnect(void)
{
    char ssid[64] = { 0 };
    char pass[128] = { 0 };
    syslog(LOG_INFO, "[HM-WIFI] Reconnect: loading saved credentials\n");
    if (agent_config_get(AGENT_CFG_KEY_WIFI_SSID, ssid, sizeof(ssid)) != OK || !ssid[0]) {
        syslog(LOG_WARNING,
            "[HM-WIFI] No saved WiFi credentials. Use CLI: set_wifi <ssid> <pass>\n");
        return ERROR;
    }
    agent_config_get(AGENT_CFG_KEY_WIFI_PASS, pass, sizeof(pass));
    syslog(LOG_INFO, "[HM-WIFI] Reconnecting to saved SSID='%s'\n", ssid);
    return network_wifi_connect(NULL, ssid, pass);
}
#endif
