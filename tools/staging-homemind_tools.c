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
 * HomeMind device tools for ai_agent
 * 
 * Provides 3 white-listed tools:
 *   - device_status: network, IP, heap, uptime
 *   - light_set: control board LED or GPIO
 *   - scene_set: home/sleep/away scenes
 *
 * Safety: All tool calls are parameter-validated. No arbitrary shell execution.
 * GPIO numbers and scene names are white-listed.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <nuttx/mm/mm.h>

/* ── Tool: device_status ──────────────────────────────────────── */

/*
 * Returns JSON with device status:
 * {
 *   "network": { "connected": true, "ip": "192.168.1.100", "iface": "wlan0" },
 *   "memory": { "heap_free_kb": 1234, "heap_total_kb": 4096 },
 *   "uptime_sec": 3600
 * }
 */
int tool_device_status(int argc, char** argv, char* result, int result_size)
{
    (void)argc;
    (void)argv;

    char ip_str[INET_ADDRSTRLEN] = "0.0.0.0";
    char iface_name[16] = "none";
    bool connected = false;

    /* Get network status */
    struct ifaddrs* ifa_list = NULL;
    if (getifaddrs(&ifa_list) == 0) {
        for (struct ifaddrs* ifa = ifa_list; ifa; ifa = ifa->ifa_next) {
            if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET)
                continue;
            if (ifa->ifa_name && strncmp(ifa->ifa_name, "lo", 2) == 0)
                continue;

            struct sockaddr_in* sin = (struct sockaddr_in*)ifa->ifa_addr;
            uint32_t addr = ntohl(sin->sin_addr.s_addr);
            if (addr == 0 || (addr >> 24) == 127)
                continue;

            inet_ntop(AF_INET, &sin->sin_addr, ip_str, sizeof(ip_str));
            strncpy(iface_name, ifa->ifa_name ? ifa->ifa_name : "?",
                sizeof(iface_name) - 1);
            connected = true;
            break;
        }
        freeifaddrs(ifa_list);
    }

    /* Get memory info */
    struct mallinfo mi = mallinfo();
    int heap_free_kb = mi.fordblks / 1024;
    int heap_total_kb = (mi.arena + mi.fordblks) / 1024;

    /* Get uptime */
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    long uptime_sec = ts.tv_sec;

    /* Format JSON result */
    snprintf(result, result_size,
        "{\n"
        "  \"network\": {\n"
        "    \"connected\": %s,\n"
        "    \"ip\": \"%s\",\n"
        "    \"iface\": \"%s\"\n"
        "  },\n"
        "  \"memory\": {\n"
        "    \"heap_free_kb\": %d,\n"
        "    \"heap_total_kb\": %d\n"
        "  },\n"
        "  \"uptime_sec\": %ld\n"
        "}",
        connected ? "true" : "false",
        ip_str,
        iface_name,
        heap_free_kb,
        heap_total_kb,
        uptime_sec);

    syslog(LOG_INFO, "[HM-LLM] device_status: connected=%s ip=%s heap=%dKB uptime=%lds\n",
        connected ? "true" : "false", ip_str, heap_free_kb, uptime_sec);

    return 0;
}

/* ── Tool: light_set ──────────────────────────────────────────── */

/* White-listed GPIO pins for LED control */
static const int LED_GPIO_PINS[] = { 2, 4, 5, 12, 13, 14, 15, 16, 17, 18 };
#define LED_GPIO_COUNT (sizeof(LED_GPIO_PINS) / sizeof(LED_GPIO_PINS[0]))

/* Default LED pin for ESP32-S3-EYE (board LED) */
#define DEFAULT_LED_PIN 2

/*
 * Control LED/GPIO
 * Args: { "pin": 2, "state": "on"|"off"|"toggle" }
 * Returns: { "pin": 2, "state": "on", "success": true }
 */
int tool_light_set(int argc, char** argv, char* result, int result_size)
{
    if (argc < 2) {
        snprintf(result, result_size,
            "{\"error\": \"Usage: light_set <pin> <on|off|toggle>\"}");
        return -1;
    }

    int pin = atoi(argv[0]);
    const char* state = argv[1];

    /* Validate pin is in white-list */
    bool pin_valid = false;
    for (int i = 0; i < (int)LED_GPIO_COUNT; i++) {
        if (LED_GPIO_PINS[i] == pin) {
            pin_valid = true;
            break;
        }
    }

    if (!pin_valid) {
        snprintf(result, result_size,
            "{\"error\": \"Invalid pin %d. Allowed pins: 2,4,5,12,13,14,15,16,17,18\"}",
            pin);
        return -1;
    }

    /* Validate state */
    if (strcmp(state, "on") != 0 && strcmp(state, "off") != 0 &&
        strcmp(state, "toggle") != 0) {
        snprintf(result, result_size,
            "{\"error\": \"Invalid state '%s'. Use: on, off, toggle\"}", state);
        return -1;
    }

    /* Execute GPIO control via NuttX GPIO driver */
    char cmd[64];
    if (strcmp(state, "on") == 0) {
        snprintf(cmd, sizeof(cmd), "gpio -w 1 %d", pin);
    } else if (strcmp(state, "off") == 0) {
        snprintf(cmd, sizeof(cmd), "gpio -w 0 %d", pin);
    } else {
        /* toggle: read current, flip */
        snprintf(cmd, sizeof(cmd), "gpio -t %d", pin);
    }

    int ret = system(cmd);

    syslog(LOG_INFO, "[HM-LLM] light_set: pin=%d state=%s ret=%d\n",
        pin, state, ret);

    snprintf(result, result_size,
        "{\n"
        "  \"pin\": %d,\n"
        "  \"state\": \"%s\",\n"
        "  \"success\": %s\n"
        "}",
        pin, state, (ret == 0) ? "true" : "false");

    return (ret == 0) ? 0 : -1;
}

/* ── Tool: scene_set ──────────────────────────────────────────── */

/* White-listed scene names */
static const char* const SCENE_NAMES[] = { "home", "sleep", "away" };
#define SCENE_COUNT (sizeof(SCENE_NAMES) / sizeof(SCENE_NAMES[0]))

/*
 * Set home scene
 * Args: { "scene": "home"|"sleep"|"away" }
 * Returns: { "scene": "home", "actions": [...], "success": true }
 */
int tool_scene_set(int argc, char** argv, char* result, int result_size)
{
    if (argc < 1) {
        snprintf(result, result_size,
            "{\"error\": \"Usage: scene_set <home|sleep|away>\"}");
        return -1;
    }

    const char* scene = argv[0];

    /* Validate scene name */
    bool scene_valid = false;
    for (int i = 0; i < (int)SCENE_COUNT; i++) {
        if (strcmp(SCENE_NAMES[i], scene) == 0) {
            scene_valid = true;
            break;
        }
    }

    if (!scene_valid) {
        snprintf(result, result_size,
            "{\"error\": \"Invalid scene '%s'. Allowed: home, sleep, away\"}", scene);
        return -1;
    }

    /* Execute scene actions */
    const char* actions = "";
    if (strcmp(scene, "home") == 0) {
        /* Home: turn on main LED, set comfortable brightness */
        system("gpio -w 1 2");  /* Main LED on */
        actions = "[\"LED2:on\"]";
    } else if (strcmp(scene, "sleep") == 0) {
        /* Sleep: turn off all LEDs */
        system("gpio -w 0 2");  /* Main LED off */
        actions = "[\"LED2:off\"]";
    } else if (strcmp(scene, "away") == 0) {
        /* Away: turn off all LEDs (security mode) */
        system("gpio -w 0 2");  /* Main LED off */
        actions = "[\"LED2:off\"]";
    }

    syslog(LOG_INFO, "[HM-LLM] scene_set: scene=%s actions=%s\n", scene, actions);

    snprintf(result, result_size,
        "{\n"
        "  \"scene\": \"%s\",\n"
        "  \"actions\": %s,\n"
        "  \"success\": true\n"
        "}",
        scene, actions);

    return 0;
}

/* ── Tool registration ────────────────────────────────────────── */

/*
 * Register HomeMind tools with ai_agent's tool system.
 * Call this from ai_agent initialization.
 *
 * The tool definitions should be added to the ai_agent's tool registry:
 *
 *   static ai_tool_def_t homemind_tools[] = {
 *       { "device_status", tool_device_status, "Get device status (network, memory, uptime)" },
 *       { "light_set", tool_light_set, "Control LED/GPIO (pin, on/off/toggle)" },
 *       { "scene_set", tool_scene_set, "Set home scene (home/sleep/away)" },
 *   };
 *
 *   for (int i = 0; i < 3; i++) {
 *       ai_agent_register_tool(&homemind_tools[i]);
 *   }
 */

/* ── System prompt addition for LLM ───────────────────────────── */

/*
 * Add to SOUL.md or system prompt:
 *
 * ## Available Tools
 *
 * You have access to the following device tools:
 *
 * ### device_status
 * Returns the current device status including network connection, IP address,
 * available memory, and uptime.
 * Usage: No arguments needed.
 *
 * ### light_set
 * Controls the LED on the device.
 * Parameters:
 *   - pin (number): GPIO pin number (allowed: 2,4,5,12,13,14,15,16,17,18)
 *   - state (string): "on", "off", or "toggle"
 * Example: light_set(2, "on") turns on the main LED.
 *
 * ### scene_set
 * Sets a predefined home scene.
 * Parameters:
 *   - scene (string): "home", "sleep", or "away"
 * Example: scene_set("sleep") activates sleep mode (all LEDs off).
 *
 * IMPORTANT: Only use the tools listed above. Do not attempt to execute
 * shell commands or access devices not listed here.
 */
