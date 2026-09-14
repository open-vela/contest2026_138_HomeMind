/*
 * HomeMind local tool: onboard LED control.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __TOOLS_TOOL_LED_H
#define __TOOLS_TOOL_LED_H

#include <stddef.h>

int tool_led_control_execute(const char *input_json, char *output,
                             size_t output_size);

#endif /* __TOOLS_TOOL_LED_H */
