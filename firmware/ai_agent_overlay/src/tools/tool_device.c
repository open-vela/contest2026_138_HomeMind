/*
 * HomeMind local tools: device info + BOOT button state.
 *
 * get_device_info aggregates uptime (CLOCK_MONOTONIC — the board has no
 * RTC and gettimeofday jumps on SNTP sync), heap stats, the current IP and
 * the firmware build string, so the LLM can answer "how long has the device
 * been up / how much memory is free".
 *
 * get_button reads the BOOT button (GPIO0, pull-up, /dev/gpio1 input pin
 * registered by the esp32s3-eye board gpio driver; pressed = level 0).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tools/tool_device.h"

#include <fcntl.h>
#include <malloc.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include <nuttx/ioexpander/gpio.h>

#include "agent_config.h"
#include "infra/network_manager.h"

#include "cJSON.h"

#define BUTTON_GPIO_DEV "/dev/gpio1"

int tool_get_device_info_execute(const char *input_json, char *output,
                                 size_t output_size)
{
    (void)input_json;

    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    long up_s = (long)ts.tv_sec;

    struct mallinfo mi = mallinfo();
    const char *ip = network_get_ip();
    if (!ip || !ip[0]) {
        ip = "0.0.0.0";
    }

    cJSON *r = cJSON_CreateObject();
    if (!r) {
        snprintf(output, output_size, "{\"error\":\"oom\"}");
        return ERROR;
    }
    cJSON_AddNumberToObject(r, "uptime_s", up_s);
    cJSON_AddNumberToObject(r, "heap_free", (double)mi.fordblks);
    cJSON_AddNumberToObject(r, "heap_used", (double)mi.uordblks);
    cJSON_AddStringToObject(r, "ip", ip);
    cJSON_AddStringToObject(r, "firmware", AGENT_BUILD_VERSION);

    char *s = cJSON_PrintUnformatted(r);
    cJSON_Delete(r);
    if (!s) {
        snprintf(output, output_size, "{\"error\":\"oom\"}");
        return ERROR;
    }
    snprintf(output, output_size, "%s", s);
    cJSON_free(s);
    return OK;
}

int tool_get_button_execute(const char *input_json, char *output,
                            size_t output_size)
{
    (void)input_json;

    int fd = open(BUTTON_GPIO_DEV, O_RDONLY);
    if (fd < 0) {
        snprintf(output, output_size,
                 "{\"error\":\"button device %s unavailable\"}",
                 BUTTON_GPIO_DEV);
        return ERROR;
    }

    unsigned val = 1;
    int ret = ioctl(fd, GPIOC_READ, (unsigned long)&val);
    close(fd);
    if (ret < 0) {
        snprintf(output, output_size, "{\"error\":\"read failed\"}");
        return ERROR;
    }

    cJSON *r = cJSON_CreateObject();
    if (!r) {
        snprintf(output, output_size, "{\"error\":\"oom\"}");
        return ERROR;
    }
    /* Pull-up: pressed pulls the line low. */
    cJSON_AddStringToObject(r, "button", val ? "released" : "pressed");
    char *s = cJSON_PrintUnformatted(r);
    cJSON_Delete(r);
    if (!s) {
        snprintf(output, output_size, "{\"error\":\"oom\"}");
        return ERROR;
    }
    snprintf(output, output_size, "%s", s);
    cJSON_free(s);
    return OK;
}
