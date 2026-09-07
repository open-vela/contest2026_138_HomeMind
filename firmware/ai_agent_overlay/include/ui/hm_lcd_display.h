/*
 * HomeMind LCD status display (ST7789 via /dev/fb0).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __UI_HM_LCD_DISPLAY_H
#define __UI_HM_LCD_DISPLAY_H

int hm_lcd_display_start(void);
int hm_lcd_show_text(const char *s);
int hm_lcd_release(void);

#endif /* __UI_HM_LCD_DISPLAY_H */
