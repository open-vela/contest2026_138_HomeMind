/*
 * HomeMind local tool: onboard LED control (ESP32-S3-EYE).
 *
 * The board's LED sits on GPIO3, registered as /dev/gpio0 (output pin) by
 * boards/xtensa/esp32s3/esp32s3-eye/src/esp32s3_gpio.c when CONFIG_DEV_GPIO
 * is enabled.  This tool drives it so the agent can control real hardware
 * without any cloud round-trip.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tools/tool_led.h"

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <syslog.h>
#include <unistd.h>

#include <nuttx/ioexpander/gpio.h>

#include "cJSON.h"

#define LED_GPIO_DEV "/dev/gpio0"

static int led_set(int fd, unsigned value)
{
    return ioctl(fd, GPIOC_WRITE, (unsigned long)value);
}

static int led_get(int fd, unsigned *value)
{
    *value = 0;
    return ioctl(fd, GPIOC_READ, (unsigned long)value);
}

int tool_led_control_execute(const char *input_json, char *output,
                             size_t output_size)
{
    cJSON *root = cJSON_Parse(input_json);
    const char *action = NULL;

    if (root) {
        action = cJSON_GetStringValue(cJSON_GetObjectItem(root, "action"));
    }
    if (!action || !action[0]) {
        cJSON_Delete(root);
        snprintf(output, output_size,
                 "{\"error\":\"missing action (on|off|toggle|status)\"}");
        return ERROR;
    }

    int fd = open(LED_GPIO_DEV, O_RDWR);
    if (fd < 0) {
        cJSON_Delete(root);
        snprintf(output, output_size,
                 "{\"error\":\"LED device %s unavailable\"}", LED_GPIO_DEV);
        return ERROR;
    }

    unsigned val = 0;
    int ret = OK;
    const char *state = NULL;

    if (strcmp(action, "status") == 0) {
        if (led_get(fd, &val) < 0) {
            ret = ERROR;
        }
        state = val ? "on" : "off";
    } else if (strcmp(action, "on") == 0) {
        ret = led_set(fd, 1) < 0 ? ERROR : OK;
        state = "on";
    } else if (strcmp(action, "off") == 0) {
        ret = led_set(fd, 0) < 0 ? ERROR : OK;
        state = "off";
    } else if (strcmp(action, "toggle") == 0) {
        if (led_get(fd, &val) < 0) {
            ret = ERROR;
        } else {
            ret = led_set(fd, val ? 0 : 1) < 0 ? ERROR : OK;
            state = val ? "off" : "on";
        }
    } else {
        ret = ERROR;
        state = NULL;
        snprintf(output, output_size,
                 "{\"error\":\"unknown action '%s'\"}", action);
    }

    if (ret == OK) {
        snprintf(output, output_size, "{\"led\":\"%s\"}", state);
    }
    close(fd);
    cJSON_Delete(root);
    return ret;
}
