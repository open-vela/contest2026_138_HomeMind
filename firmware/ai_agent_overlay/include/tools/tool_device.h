/*
 * HomeMind local tools: device info + BOOT button state.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __TOOLS_TOOL_DEVICE_H
#define __TOOLS_TOOL_DEVICE_H

#include <stddef.h>

int tool_get_device_info_execute(const char *input_json, char *output,
                                 size_t output_size);

int tool_get_button_execute(const char *input_json, char *output,
                            size_t output_size);

#endif /* __TOOLS_TOOL_DEVICE_H */
