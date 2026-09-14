/*
 * HomeMind LCD display v3 — "Calm Tech" face dashboard for ST7789 240x240.
 *
 * Layout (RGB565, direct /dev/fb0, no LVGL), see DESIGN.md:
 *
 *   +----------------------------------------+
 *   | [wifi]      H O M E M I N D    (MIMO)  |  header bar
 *   +----------------------------------------+
 *   |            .-~~~~~~~~-.                |
 *   |           /  o      o  \               |  robot face with accent halo,
 *   |          |    ____      |              |  mood follows system state:
 *   |           \  (____)   /                |    happy  = wifi + MiMo ok
 *   |            '-........-'                |    sad    = wifi down
 *   +----------------------------------------+    neutral= no LLM key
 *   |               一切就绪                  |  status sprite (Chinese)
 *   +----------------------------------------+
 *   | [灯 开]            | [助手 就绪]        |  bottom chips
 *   +----------------------------------------+
 *
 * The face blinks (eyes closed ~1s every 8s): the poll loop ticks at 1s
 * and re-renders when state or blink phase changed.
 *
 * Chinese text is pre-rendered 1bpp sprites.  Arcs use an integer sine
 * table; no libm dependency.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "ui/hm_lcd_display.h"

#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <syslog.h>
#include <unistd.h>

#include <nuttx/ioexpander/gpio.h>
#include <nuttx/video/fb.h>

#include "agent_compat.h"
#include "agent_config.h"
#include "infra/config_store.h"
#include "infra/network_manager.h"

#define LCD_DEV         "/dev/fb0"
#define LED_GPIO_DEV    "/dev/gpio0"
#define TICK_INTERVAL_S 1
#define BLINK_PERIOD    8      /* one closed-eye tick every 8s */
#define BLINK_PHASE     7

static int g_lcd_hold;

/* ── RGB565 palette (DESIGN.md "Calm Tech") ───────────────────── */
#define C_BG     0x0863u  /* #0A0E1A background               */
#define C_S1     0x10C5u  /* #121A2E header bar               */
#define C_S2     0x1907u  /* #18213A chips                    */
#define C_HAIR   0x218Au  /* #263252 hairline border          */
#define C_ACC    0x7BFEu  /* #7B7FF2 violet accent            */
#define C_MINT   0x4EF0u  /* #4ADE80 success                  */
#define C_CORAL  0xFB90u  /* #FB7185 danger                   */
#define C_AMBER  0xFDE4u  /* #FBBF24 warn / LED on            */
#define C_HI     0xEF9Fu  /* #EDF1FB primary text             */
#define C_LO     0x5B71u  /* #5E6C8F muted text               */
#define C_FACE   0x1928u  /* #1A2440 face plate               */
#define C_RING   0x320Du  /* #34406B face ring                */
#define C_FEAT   0xA75Fu  /* #A5E8FF eyes / mouth             */
#define C_BLUSH  0xFD59u  /* #F9A8C9 blush                    */
#define C_OFF    0x1928u  /* disabled element (= face plate)  */

static const char *TAG = "lcddisp";

typedef struct
{
    int fd;
    int w;
    int h;
    int stride;
    int bpp;
    uint16_t *fb;             /* flat build: kernel fbmem == userspace ptr */
    bool wifi;
    bool llm;
    bool led;
} lcd_state_t;

static lcd_state_t g_lcd;

/* ── integer sine table: sin(deg) * 1000, 360 entries ─────────── */

static const int16_t g_sin1000[360] = {
       0,    17,    35,    52,    70,    87,   105,   122,
     139,   156,   174,   191,   208,   225,   242,   259,
     276,   292,   309,   326,   342,   358,   375,   391,
     407,   423,   438,   454,   469,   485,   500,   515,
     530,   545,   559,   574,   588,   602,   616,   629,
     643,   656,   669,   682,   695,   707,   719,   731,
     743,   755,   766,   777,   788,   799,   809,   819,
     829,   839,   848,   857,   866,   875,   883,   891,
     899,   906,   914,   921,   927,   934,   940,   946,
     951,   956,   961,   966,   970,   974,   978,   982,
     985,   988,   990,   993,   995,   996,   998,   999,
     999,  1000,  1000,  1000,   999,   999,   998,   996,
     995,   993,   990,   988,   985,   982,   978,   974,
     970,   966,   961,   956,   951,   946,   940,   934,
     927,   921,   914,   906,   899,   891,   883,   875,
     866,   857,   848,   839,   829,   819,   809,   799,
     788,   777,   766,   755,   743,   731,   719,   707,
     695,   682,   669,   656,   643,   629,   616,   602,
     588,   574,   559,   545,   530,   515,   500,   485,
     469,   454,   438,   423,   407,   391,   375,   358,
     342,   326,   309,   292,   276,   259,   242,   225,
     208,   191,   174,   156,   139,   122,   105,    87,
      70,    52,    35,    17,     0,   -17,   -35,   -52,
     -70,   -87,  -105,  -122,  -139,  -156,  -174,  -191,
    -208,  -225,  -242,  -259,  -276,  -292,  -309,  -326,
    -342,  -358,  -375,  -391,  -407,  -423,  -438,  -454,
    -469,  -485,  -500,  -515,  -530,  -545,  -559,  -574,
    -588,  -602,  -616,  -629,  -643,  -656,  -669,  -682,
    -695,  -707,  -719,  -731,  -743,  -755,  -766,  -777,
    -788,  -799,  -809,  -819,  -829,  -839,  -848,  -857,
    -866,  -875,  -883,  -891,  -899,  -906,  -914,  -921,
    -927,  -934,  -940,  -946,  -951,  -956,  -961,  -966,
    -970,  -974,  -978,  -982,  -985,  -988,  -990,  -993,
    -995,  -996,  -998,  -999,  -999, -1000, -1000, -1000,
    -999,  -999,  -998,  -996,  -995,  -993,  -990,  -988,
    -985,  -982,  -978,  -974,  -970,  -966,  -961,  -956,
    -951,  -946,  -940,  -934,  -927,  -921,  -914,  -906,
    -899,  -891,  -883,  -875,  -866,  -857,  -848,  -839,
    -829,  -819,  -809,  -799,  -788,  -777,  -766,  -755,
    -743,  -731,  -719,  -707,  -695,  -682,  -669,  -656,
    -643,  -629,  -616,  -602,  -588,  -574,  -559,  -545,
    -530,  -515,  -500,  -485,  -469,  -454,  -438,  -423,
    -407,  -391,  -375,  -358,  -342,  -326,  -309,  -292,
    -276,  -259,  -242,  -225,  -208,  -191,  -174,  -156,
    -139,  -122,  -105,   -87,   -70,   -52,   -35,   -17,
};

/* ── Chinese text sprites (generated, 1bpp row-packed MSB-left) ─ */

typedef struct
{
    int w;
    int h;
    int stride;
    const uint8_t *bits;
} hm_sprite_t;

static const uint8_t g_s_ready_bits[] = {
    0x00, 0x00, 0x00, 0x60, 0x00, 0x00, 0x40, 0x60, 0x02, 0x03, 0x00, 0x00,
    0x00, 0x00, 0x63, 0xFF, 0xE0, 0x60, 0x6C, 0x06, 0x03, 0x04, 0x00, 0x00,
    0x00, 0x63, 0xFF, 0xE0, 0x20, 0x67, 0x06, 0x03, 0x0C, 0x00, 0x00, 0x00,
    0x60, 0x30, 0x67, 0xFF, 0x63, 0x0C, 0x1F, 0xFC, 0x00, 0x00, 0x00, 0x60,
    0x30, 0x67, 0xFE, 0x60, 0x0C, 0x03, 0x38, 0x00, 0x00, 0x01, 0xFE, 0x30,
    0x40, 0x00, 0x60, 0x18, 0xC3, 0x30, 0x00, 0x00, 0x03, 0xFE, 0x30, 0x43,
    0xFD, 0xFF, 0x91, 0xC3, 0x60, 0x00, 0x00, 0x00, 0x60, 0x30, 0x43, 0x0D,
    0xFF, 0xBF, 0xBF, 0xFE, 0x00, 0x00, 0x00, 0x60, 0x20, 0x42, 0x04, 0x68,
    0x13, 0x03, 0xC0, 0xFF, 0xFF, 0xFC, 0x60, 0x20, 0xC2, 0x04, 0x68, 0x06,
    0x03, 0x00, 0xFF, 0xFF, 0xFC, 0x60, 0x20, 0xC3, 0xFC, 0x68, 0x04, 0x0F,
    0xFC, 0x00, 0x00, 0x00, 0x60, 0x60, 0xC3, 0x6C, 0x48, 0x0D, 0xBC, 0x1C,
    0x00, 0x00, 0x00, 0x62, 0x60, 0xC0, 0x60, 0xC8, 0x1F, 0xFC, 0x0C, 0x00,
    0x00, 0x00, 0x66, 0x60, 0xC0, 0x68, 0xC8, 0x98, 0x0F, 0xFC, 0x00, 0x00,
    0x00, 0x7E, 0x60, 0xC3, 0x6C, 0x88, 0xC0, 0x0F, 0xFC, 0x00, 0x00, 0x00,
    0x78, 0xC0, 0xC3, 0x65, 0x88, 0xC1, 0xCC, 0x0C, 0x00, 0x00, 0x00, 0x70,
    0xC0, 0xC6, 0x61, 0x88, 0x9F, 0xCC, 0x0C, 0x00, 0x00, 0x00, 0x41, 0x81,
    0xC4, 0x63, 0x09, 0x9C, 0x0F, 0xFC, 0x00, 0x00, 0x00, 0x03, 0x1F, 0x81,
    0xE6, 0x0F, 0x80, 0x0C, 0x1C, 0x00, 0x00, 0x00, 0x06, 0x1F, 0x81, 0x84,
    0x07, 0x00, 0x0C, 0x0C, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00,
};
static const hm_sprite_t g_s_ready = { 88, 21, 11, g_s_ready_bits };

static const uint8_t g_s_nowifi_bits[] = {
    0x00, 0x00, 0x03, 0x06, 0x00, 0x00, 0x00, 0x04, 0x06, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x03, 0x06, 0x00, 0x00, 0xC0, 0x06, 0x06, 0x00, 0x30,
    0x10, 0x00, 0x7F, 0xFF, 0x86, 0x0F, 0xF8, 0x00, 0xC0, 0x06, 0x0C, 0x00,
    0x30, 0x18, 0x00, 0x60, 0x01, 0x86, 0x4C, 0x18, 0x00, 0xC0, 0x03, 0x7F,
    0xFC, 0x33, 0xFF, 0xC0, 0x40, 0x00, 0x8C, 0xDE, 0x30, 0x7F, 0xFF, 0x81,
    0x1C, 0x01, 0xFC, 0x00, 0x00, 0x71, 0x82, 0x8C, 0xF2, 0x70, 0x7F, 0xFF,
    0x80, 0x19, 0x80, 0x30, 0x81, 0x00, 0x59, 0x46, 0x8F, 0xA3, 0xE0, 0x00,
    0xC0, 0x00, 0x19, 0x80, 0x30, 0xC3, 0x00, 0x5B, 0x64, 0x83, 0x01, 0xC0,
    0x00, 0xC0, 0x1E, 0x31, 0x80, 0x30, 0x46, 0x00, 0x4E, 0x3C, 0x83, 0x07,
    0x70, 0x00, 0xC0, 0x06, 0x31, 0x80, 0x37, 0xFF, 0xE0, 0x46, 0x38, 0x86,
    0x3C, 0x3F, 0xFF, 0xFF, 0xE2, 0x3F, 0xFC, 0x3C, 0x00, 0x00, 0x46, 0x18,
    0x84, 0x30, 0x04, 0xFF, 0xFF, 0xC2, 0x01, 0x81, 0xF8, 0x20, 0x00, 0x4F,
    0x18, 0x8F, 0xDF, 0xF8, 0x01, 0xE0, 0x02, 0x01, 0x81, 0xF7, 0xFF, 0xE0,
    0x49, 0x3C, 0x8E, 0x18, 0x18, 0x02, 0xD0, 0x02, 0x01, 0x80, 0x30, 0xC3,
    0x00, 0x59, 0xE4, 0x80, 0x18, 0x08, 0x0C, 0xD8, 0x02, 0x7F, 0xFE, 0x31,
    0x82, 0x00, 0x70, 0x66, 0x81, 0xD8, 0x08, 0x18, 0xCE, 0x02, 0x01, 0x80,
    0x31, 0xE6, 0x00, 0x70, 0x42, 0x8F, 0x98, 0x08, 0x30, 0xC3, 0x06, 0x01,
    0x80, 0x30, 0x3C, 0x00, 0x40, 0x00, 0x8C, 0x1F, 0xF8, 0xE0, 0xC1, 0xCF,
    0x01, 0x80, 0x30, 0x7F, 0x00, 0x40, 0x00, 0x80, 0x18, 0x18, 0x80, 0xC0,
    0x59, 0xFC, 0xFC, 0x23, 0xE3, 0xC0, 0x40, 0x0F, 0x80, 0x00, 0x00, 0x00,
    0xC0, 0x08, 0x7F, 0xFC, 0xE7, 0x00, 0xC0, 0x40, 0x0E, 0x00, 0x00, 0x00,
    0x00, 0xC0, 0x00, 0x00, 0x00, 0xC0, 0x00, 0x00,
};
static const hm_sprite_t g_s_nowifi = { 99, 20, 13, g_s_nowifi_bits };

static const uint8_t g_s_nokey_bits[] = {
    0x0C, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0C,
    0x06, 0x00, 0x10, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F, 0xFF,
    0xF8, 0x18, 0x18, 0x07, 0xFF, 0x7F, 0x0F, 0xFF, 0xFC, 0x30, 0x1C, 0x00,
    0x30, 0x18, 0x00, 0xD8, 0x7F, 0x0C, 0x61, 0x8C, 0x32, 0x19, 0x80, 0x63,
    0xFF, 0xE0, 0xD8, 0x01, 0x0C, 0x21, 0x0C, 0x63, 0x30, 0x80, 0xE3, 0xFF,
    0xC0, 0xD8, 0x01, 0x0F, 0xFF, 0xFC, 0x40, 0x30, 0x01, 0xC0, 0x18, 0x07,
    0xFF, 0x01, 0x00, 0x0C, 0x00, 0x3F, 0xFF, 0xF3, 0x98, 0x18, 0x07, 0xFF,
    0x01, 0x1F, 0xFF, 0xFE, 0x00, 0x30, 0x01, 0x38, 0x18, 0x06, 0xDB, 0x01,
    0x00, 0x0C, 0x00, 0x00, 0x30, 0x00, 0x37, 0xFF, 0xF6, 0xDB, 0x7F, 0x00,
    0x0C, 0x00, 0x00, 0x30, 0x00, 0x67, 0xFF, 0xE6, 0xDB, 0x7F, 0x07, 0xFF,
    0xF8, 0xFF, 0xFF, 0xFC, 0xE0, 0x00, 0x06, 0x9B, 0x61, 0x06, 0x00, 0x18,
    0x7F, 0xFF, 0xF9, 0xE0, 0x03, 0x07, 0x9F, 0x60, 0x06, 0x00, 0x18, 0x00,
    0x01, 0x03, 0xE7, 0xFF, 0xF7, 0x1F, 0x60, 0x07, 0xFF, 0xF8, 0x00, 0x01,
    0x01, 0x67, 0xFF, 0xE6, 0x03, 0x60, 0x07, 0xFF, 0xF8, 0x7F, 0xFF, 0xF8,
    0x61, 0x03, 0x07, 0xFF, 0x60, 0x86, 0x00, 0x18, 0x3F, 0xFF, 0xF0, 0x61,
    0x83, 0x07, 0xFF, 0x60, 0xC6, 0x00, 0x18, 0x02, 0x01, 0x00, 0x60, 0xC3,
    0x06, 0x03, 0x60, 0x86, 0x00, 0x18, 0x03, 0x81, 0x00, 0x60, 0x43, 0x06,
    0x03, 0x60, 0x87, 0xFF, 0xF8, 0x01, 0x83, 0x00, 0x60, 0x03, 0x07, 0xFF,
    0x61, 0xBF, 0xFF, 0xFF, 0x00, 0x3F, 0x00, 0x60, 0x3F, 0x07, 0xFF, 0x3F,
    0x80, 0x00, 0x00, 0x00, 0x3E, 0x00, 0x60, 0x3E, 0x06, 0x03, 0x1E, 0x00,
    0x00, 0x00,
};
static const hm_sprite_t g_s_nokey = { 88, 22, 11, g_s_nokey_bits };

static const uint8_t g_s_led_bits[] = {
    0x10, 0x00, 0x00, 0x11, 0xFF, 0xC0, 0x10, 0x0C, 0x00, 0xD0, 0x0C, 0x00,
    0xD2, 0x0C, 0x00, 0x52, 0x0C, 0x00, 0x56, 0x0C, 0x00, 0x54, 0x0C, 0x00,
    0x10, 0x0C, 0x00, 0x10, 0x0C, 0x00, 0x1C, 0x0C, 0x00, 0x3C, 0x0C, 0x00,
    0x36, 0x0C, 0x00, 0x23, 0x0C, 0x00, 0x63, 0x0C, 0x00, 0x40, 0x0C, 0x00,
    0x40, 0xF8, 0x00, 0x00, 0x60, 0x00,
};
static const hm_sprite_t g_s_led = { 18, 18, 3, g_s_led_bits };

static const uint8_t g_s_on_bits[] = {
    0x7F, 0xFF, 0x80, 0x06, 0x1C, 0x00, 0x04, 0x08, 0x00, 0x04, 0x08, 0x00,
    0x04, 0x08, 0x00, 0x04, 0x08, 0x00, 0x04, 0x08, 0x00, 0xFF, 0xFF, 0xC0,
    0x0E, 0x1C, 0x00, 0x04, 0x08, 0x00, 0x04, 0x08, 0x00, 0x0C, 0x08, 0x00,
    0x0C, 0x08, 0x00, 0x18, 0x08, 0x00, 0x70, 0x08, 0x00, 0x40, 0x08, 0x00,
};
static const hm_sprite_t g_s_on = { 18, 16, 3, g_s_on_bits };

static const uint8_t g_s_off_bits[] = {
    0x08, 0x0C, 0x00, 0x0C, 0x0C, 0x00, 0x06, 0x18, 0x00, 0x02, 0x30, 0x00,
    0x3F, 0xFF, 0x00, 0x00, 0xC0, 0x00, 0x00, 0xC0, 0x00, 0x00, 0xC0, 0x00,
    0x00, 0xC0, 0x00, 0x7F, 0xFF, 0x80, 0x01, 0xC0, 0x00, 0x01, 0xE0, 0x00,
    0x01, 0x30, 0x00, 0x07, 0x18, 0x00, 0x0C, 0x0C, 0x00, 0x38, 0x07, 0x80,
    0x60, 0x01, 0x80,
};
static const hm_sprite_t g_s_off = { 18, 17, 3, g_s_off_bits };

static const uint8_t g_s_asst_bits[] = {
    0x00, 0x10, 0x00, 0x0F, 0xC0, 0x3F, 0x10, 0x1F, 0xFF, 0xC0, 0x23, 0x10,
    0x00, 0x30, 0x00, 0x21, 0x10, 0x00, 0x30, 0x00, 0x21, 0x7F, 0x80, 0x30,
    0x00, 0x3F, 0x19, 0x8F, 0xFF, 0xC0, 0x23, 0x11, 0x80, 0x30, 0x00, 0x21,
    0x11, 0x80, 0x30, 0x00, 0x21, 0x11, 0x80, 0x30, 0x00, 0x3F, 0x11, 0x80,
    0x30, 0x00, 0x23, 0x31, 0xBF, 0xFF, 0xF0, 0x21, 0x31, 0x80, 0x30, 0x00,
    0x21, 0x21, 0x80, 0x30, 0x00, 0x7F, 0xE1, 0x80, 0x30, 0x00, 0xF8, 0x41,
    0x00, 0x30, 0x00, 0x00, 0xDF, 0x03, 0xF0, 0x00, 0x00, 0x80, 0x01, 0xC0,
    0x00,
};
static const hm_sprite_t g_s_asst = { 36, 17, 5, g_s_asst_bits };

static const uint8_t g_s_ok_bits[] = {
    0x08, 0x10, 0x04, 0x08, 0x60, 0x0C, 0x16, 0x04, 0x08, 0x40, 0xFF, 0xD3,
    0x0C, 0x7F, 0xC0, 0x00, 0x11, 0x19, 0x09, 0x80, 0x00, 0x10, 0x13, 0x0B,
    0x00, 0x7F, 0xFF, 0xBE, 0xFF, 0xE0, 0x61, 0xB4, 0x06, 0x0C, 0x00, 0x61,
    0x94, 0x0C, 0x18, 0x00, 0x7F, 0xB4, 0x0B, 0x3F, 0xC0, 0x6D, 0xB4, 0x1E,
    0xF0, 0xC0, 0x2F, 0x34, 0x10, 0x20, 0x40, 0x6D, 0x24, 0xC0, 0x3F, 0xC0,
    0x4D, 0xE4, 0xCF, 0xB0, 0xC0, 0x4C, 0x44, 0xDE, 0x20, 0x40, 0x0C, 0xC4,
    0x80, 0x3F, 0xC0, 0x38, 0x87, 0x80, 0x30, 0xC0, 0x01, 0x80, 0x00, 0x00,
    0x00,
};
static const hm_sprite_t g_s_ok = { 36, 17, 5, g_s_ok_bits };

static const uint8_t g_s_unset_bits[] = {
    0x00, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC0, 0x3F, 0xFF, 0xE3,
    0xFF, 0xF0, 0x00, 0xC0, 0x06, 0x80, 0x62, 0x21, 0x10, 0x7F, 0xFF, 0x86,
    0x80, 0x63, 0xFF, 0xF0, 0x00, 0xC0, 0x06, 0x80, 0x60, 0x0C, 0x00, 0x00,
    0xC0, 0x1F, 0xF0, 0x67, 0xFF, 0xF8, 0x00, 0xC0, 0x16, 0xB0, 0x60, 0x0C,
    0x00, 0x00, 0xC0, 0x16, 0xB7, 0xE1, 0xFF, 0xE0, 0xFF, 0xFF, 0xD4, 0xB4,
    0x61, 0x00, 0x20, 0x01, 0xE0, 0x1C, 0xF4, 0x01, 0xFF, 0xE0, 0x01, 0xE0,
    0x1C, 0x34, 0x01, 0x00, 0x20, 0x02, 0xD0, 0x10, 0x34, 0x01, 0xFF, 0xE0,
    0x0C, 0xCC, 0x1F, 0xF4, 0x21, 0x00, 0x20, 0x18, 0xC6, 0x10, 0x34, 0x31,
    0xFF, 0xE0, 0x70, 0xC3, 0x90, 0x34, 0x31, 0x00, 0x20, 0x40, 0xC0, 0x9F,
    0xF4, 0x2F, 0xFF, 0xFC, 0x00, 0xC0, 0x10, 0x37, 0xE0, 0x00, 0x00, 0x00,
    0x00, 0x10, 0x30, 0x00, 0x00, 0x00,
};
static const hm_sprite_t g_s_unset = { 54, 18, 7, g_s_unset_bits };

/* ── minimal 5x7 Latin glyphs (HOMEMIND / MIMO wordmarks) ─────── */

static const uint8_t g_latin[13][7] = {
    [0]  = { 0x38,0x44,0x44,0x7C,0x44,0x44,0x44 }, /* A */
    [2]  = { 0x78,0x44,0x44,0x44,0x44,0x44,0x78 }, /* D */
    [3]  = { 0x7C,0x40,0x40,0x78,0x40,0x40,0x7C }, /* E */
    [4]  = { 0x7C,0x40,0x40,0x78,0x40,0x40,0x40 }, /* F */
    [6]  = { 0x44,0x44,0x44,0x7C,0x44,0x44,0x44 }, /* H */
    [7]  = { 0x38,0x10,0x10,0x10,0x10,0x10,0x38 }, /* I */
    [9]  = { 0x44,0x6C,0x54,0x54,0x44,0x44,0x44 }, /* M */
    [11] = { 0x44,0x64,0x54,0x4C,0x44,0x44,0x44 }, /* N */
    [12] = { 0x38,0x44,0x44,0x44,0x44,0x44,0x38 }, /* O */
};

static int latin_idx(char c)
{
    switch (c)
    {
    case 'A': return 0;
    case 'D': return 2;
    case 'E': return 3;
    case 'F': return 4;
    case 'H': return 6;
    case 'I': return 7;
    case 'M': return 9;
    case 'N': return 11;
    case 'O': return 12;
    default:  return -1;
    }
}

/* ── pixel primitives ─────────────────────────────────────────── */

static void px(int x, int y, uint16_t c)
{
    if (x >= 0 && x < g_lcd.w && y >= 0 && y < g_lcd.h) {
        uint16_t *p = (uint16_t *)((uint8_t *)g_lcd.fb + y * g_lcd.stride);
        p[x] = c;
    }
}

static void fill_rect(int x0, int y0, int x1, int y1, uint16_t c)
{
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            px(x, y, c);
        }
    }
}

static void fill_circle(int cx, int cy, int r, uint16_t c)
{
    for (int y = cy - r; y <= cy + r; y++) {
        for (int x = cx - r; x <= cx + r; x++) {
            int dx = x - cx;
            int dy = y - cy;
            if (dx * dx + dy * dy <= r * r) {
                px(x, y, c);
            }
        }
    }
}

static void ring(int cx, int cy, int r, uint16_t c)
{
    int inner = (r - 1) * (r - 1);
    int outer = r * r;
    for (int y = cy - r; y <= cy + r; y++) {
        for (int x = cx - r; x <= cx + r; x++) {
            int dx = x - cx;
            int dy = y - cy;
            int d2 = dx * dx + dy * dy;
            if (d2 <= outer && d2 >= inner) {
                px(x, y, c);
            }
        }
    }
}

/* circular arc, degrees (0 = +x axis, clockwise on screen) */
static void draw_arc(int cx, int cy, int r, int w,
                     int a0, int a1, uint16_t c)
{
    for (int a = a0; a <= a1; a++) {
        int deg = ((a % 360) + 360) % 360;
        int s = g_sin1000[deg];                     /* sin  */
        int co = g_sin1000[(deg + 90) % 360];       /* cos  */
        for (int rr = r - w + 1; rr <= r; rr++) {
            px(cx + (co * rr) / 1000, cy + (s * rr) / 1000, c);
        }
    }
}

static void line(int x0, int y0, int x1, int y1, uint16_t c, int w)
{
    int steps = (abs(x1 - x0) > abs(y1 - y0) ? abs(x1 - x0)
                                             : abs(y1 - y0)) + 1;
    for (int i = 0; i <= steps; i++) {
        int x = x0 + (x1 - x0) * i / steps;
        int y = y0 + (y1 - y0) * i / steps;
        fill_rect(x - w / 2, y - w / 2, x + w / 2, y + w / 2, c);
    }
}

static bool rr_in(int x, int y, int x0, int y0, int x1, int y1, int r)
{
    if (x < x0 || x > x1 || y < y0 || y > y1) {
        return false;
    }
    int dx = x0 + r - x;
    if (x - (x1 - r) > dx) {
        dx = x - (x1 - r);
    }
    if (dx < 0) {
        dx = 0;
    }
    int dy = y0 + r - y;
    if (y - (y1 - r) > dy) {
        dy = y - (y1 - r);
    }
    if (dy < 0) {
        dy = 0;
    }
    return dx * dx + dy * dy <= r * r;
}

static void round_rect(int x0, int y0, int x1, int y1, int r,
                       uint16_t fill, uint16_t edge)
{
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            if (rr_in(x, y, x0, y0, x1, y1, r)) {
                px(x, y, edge);
            }
        }
    }
    if (x1 - x0 > 4 && y1 - y0 > 4) {
        int ri = r > 2 ? r - 2 : 1;
        for (int y = y0 + 2; y <= y1 - 2; y++) {
            for (int x = x0 + 2; x <= x1 - 2; x++) {
                if (rr_in(x, y, x0 + 2, y0 + 2, x1 - 2, y1 - 2, ri)) {
                    px(x, y, fill);
                }
            }
        }
    } else {
        fill_rect(x0, y0, x1, y1, fill);
    }
}

/* ── text drawing ─────────────────────────────────────────────── */

static void draw_sprite(const hm_sprite_t *s, int x, int y, uint16_t c)
{
    for (int yy = 0; yy < s->h; yy++) {
        const uint8_t *row = s->bits + yy * s->stride;
        for (int xx = 0; xx < s->w; xx++) {
            if (row[xx / 8] & (0x80 >> (xx % 8))) {
                px(x + xx, y + yy, c);
            }
        }
    }
}

static void draw_latin(const char *s, int x, int y, uint16_t c, int scale)
{
    for (; *s; s++) {
        int idx = latin_idx(*s);
        if (idx >= 0) {
            const uint8_t *g = g_latin[idx];
            for (int ry = 0; ry < 7; ry++) {
                for (int rx = 0; rx < 5; rx++) {
                    if (g[ry] & (0x40 >> rx)) {
                        fill_rect(x + rx * scale, y + ry * scale,
                                  x + rx * scale + scale - 1,
                                  y + ry * scale + scale - 1, c);
                    }
                }
            }
        }
        x += 6 * scale;
    }
}

static int latin_w(const char *s, int scale)
{
    int n = 0;
    while (*s++) {
        n++;
    }
    return n * 6 * scale;
}

static void draw_latin_centered(const char *s, int cx, int y,
                                uint16_t c, int scale)
{
    draw_latin(s, cx - latin_w(s, scale) / 2, y, c, scale);
}

/* ── screen sections ──────────────────────────────────────────── */

static void icon_wifi(int cx, int cy, bool up, int r)
{
    uint16_t c = up ? C_MINT : C_CORAL;
    draw_arc(cx, cy + r / 2, r, 3, 200, 340, c);
    draw_arc(cx, cy + r / 2, r - 7, 3, 200, 340, c);
    fill_circle(cx, cy + r / 3, 2, c);
    if (!up) {
        line(cx - r, cy - r, cx + r, cy + r, C_CORAL, 2);
    }
}

/* mood: 0 happy, 1 sad, 2 neutral */
static void draw_face(int cx, int cy, int mood, bool blink)
{
    ring(cx, cy, 64, C_ACC);          /* accent halo */
    fill_circle(cx, cy, 62, C_RING);
    fill_circle(cx, cy, 59, C_FACE);

    int ex = 25;
    int ey = -12;

    if (mood == 0 && !blink)          /* happy: round eyes with glint */
    {
        fill_circle(cx - ex, cy + ey, 13, C_FEAT);
        fill_circle(cx + ex, cy + ey, 13, C_FEAT);
        fill_circle(cx - ex - 4, cy + ey - 5, 4, C_FACE);
        fill_circle(cx + ex - 4, cy + ey - 5, 4, C_FACE);
    }
    else                              /* closed / droopy eyes */
    {
        draw_arc(cx - ex, cy + ey, 11, 4, 190, 350, C_FEAT);
        draw_arc(cx + ex, cy + ey, 11, 4, 190, 350, C_FEAT);
    }

    if (mood == 0)
    {
        draw_arc(cx, cy + 16, 25, 6, 25, 155, C_FEAT);   /* smile */
        fill_circle(cx - 44, cy + 10, 6, C_BLUSH);
        fill_circle(cx + 44, cy + 10, 6, C_BLUSH);
    }
    else if (mood == 1)
    {
        draw_arc(cx, cy + 40, 22, 5, 205, 335, C_FEAT);  /* frown */
    }
    else
    {
        fill_rect(cx - 12, cy + 28, cx + 11, cy + 29, C_FEAT);
    }
}

static void draw_header(bool wifi, bool llm)
{
    fill_rect(0, 0, g_lcd.w - 1, 27, C_S1);
    fill_rect(0, 28, g_lcd.w - 1, 28, C_HAIR);
    icon_wifi(24, 14, wifi, 11);
    draw_latin_centered("HOMEMIND", 120, 7, C_ACC, 2);
    round_rect(182, 6, 234, 22, 8, llm ? C_S2 : C_OFF,
               llm ? C_MINT : C_HAIR);
    draw_latin_centered("MIMO", 208, 10, llm ? C_MINT : C_LO, 1);
}

static void render(bool blink)
{
    fill_rect(0, 0, g_lcd.w - 1, g_lcd.h - 1, C_BG);
    draw_header(g_lcd.wifi, g_lcd.llm);

    int mood;
    const hm_sprite_t *st;
    uint16_t scol;

    if (g_lcd.wifi && g_lcd.llm) {
        mood = 0;
        st = &g_s_ready;
        scol = C_MINT;
    } else if (!g_lcd.wifi) {
        mood = 1;
        st = &g_s_nowifi;
        scol = C_CORAL;
    } else {
        mood = 2;
        st = &g_s_nokey;
        scol = C_AMBER;
    }

    draw_face(120, 106, mood, blink);
    draw_sprite(st, (g_lcd.w - st->w) / 2, 172, scol);

    /* bottom chips: LED (left) and assistant (right) */
    round_rect(12, 194, 114, 232, 12, C_S2, C_HAIR);
    draw_sprite(&g_s_led, 24, 203, C_HI);
    draw_sprite(g_lcd.led ? &g_s_on : &g_s_off,
                114 - 12 - (g_lcd.led ? g_s_on.w : g_s_off.w), 203,
                g_lcd.led ? C_AMBER : C_LO);

    round_rect(126, 194, 228, 232, 12, C_S2, C_HAIR);
    draw_sprite(&g_s_asst, 138, 203, C_HI);
    draw_sprite(g_lcd.llm ? &g_s_ok : &g_s_unset,
                138 + g_s_asst.w + 6, 203, g_lcd.llm ? C_MINT : C_LO);

    struct fb_area_s area;
    area.x = 0;
    area.y = 0;
    area.w = g_lcd.w;
    area.h = g_lcd.h;
    ioctl(g_lcd.fd, FBIO_UPDATE, (unsigned long)((uintptr_t)&area));
}

/* ── state helpers ────────────────────────────────────────────── */

static bool read_llm_ok(void)
{
    char key[64];
    return claw_config_get(AGENT_CFG_KEY_API_KEY, key, sizeof(key)) == OK &&
           key[0] != '\0';
}

static bool read_led(void)
{
    int fd = open(LED_GPIO_DEV, O_RDONLY);
    if (fd < 0) {
        return false;
    }
    unsigned val = 0;
    ioctl(fd, GPIOC_READ, (unsigned long)&val);
    close(fd);
    return val != 0;
}

/* ── display thread ───────────────────────────────────────────── */

/* Post-mortem stage readout (DIAGNOSTIC ONLY).
 * The ST7789 panel keeps showing its last frame after the chip hangs, so
 * whatever stage number is on screen when it freezes is the last step
 * reached.  Big white digits on a red plate, top-left.
 */

void hm_lcd_mark(int stage)
{
    struct fb_area_s area;
    char buf[8];

    if (g_lcd.fb == NULL || g_lcd.fd < 0)
      {
        return;
      }

    snprintf(buf, sizeof(buf), "%d", stage);
    fill_rect(0, 0, 64, 44, 0xF800u);
    draw_latin(buf, 4, 4, 0xFFFFu, 5);

    area.x = 0;
    area.y = 0;
    area.w = 64;
    area.h = 44;
    ioctl(g_lcd.fd, FBIO_UPDATE, (unsigned long)((uintptr_t)&area));
}

static void *display_thread(void *arg)
{
    (void)arg;

    memset(&g_lcd, 0, sizeof(g_lcd));

    g_lcd.fd = open(LCD_DEV, O_RDWR);
    if (g_lcd.fd < 0) {
        syslog(LOG_ERR, "[%s] cannot open %s\n", TAG, LCD_DEV);
        return NULL;
    }

    struct fb_videoinfo_s vinfo;
    struct fb_planeinfo_s pinfo;
    if (ioctl(g_lcd.fd, FBIOGET_VIDEOINFO,
              (unsigned long)((uintptr_t)&vinfo)) < 0 ||
        ioctl(g_lcd.fd, FBIOGET_PLANEINFO,
              (unsigned long)((uintptr_t)&pinfo)) < 0) {
        syslog(LOG_ERR, "[%s] FB ioctls failed\n", TAG);
        close(g_lcd.fd);
        return NULL;
    }

    g_lcd.w = vinfo.xres;
    g_lcd.h = vinfo.yres;
    g_lcd.stride = pinfo.stride;
    g_lcd.bpp = pinfo.bpp;
    g_lcd.fb = (uint16_t *)pinfo.fbmem;

    syslog(LOG_INFO, "[%s] %dx%d bpp=%d stride=%d\n",
           TAG, g_lcd.w, g_lcd.h, g_lcd.bpp, g_lcd.stride);

    if (g_lcd.bpp != 16) {
        syslog(LOG_ERR, "[%s] only 16bpp supported, got %d\n",
               TAG, g_lcd.bpp);
        close(g_lcd.fd);
        return NULL;
    }

    bool first = true;
    bool prev_blink = false;
    int tick = 0;

    /* Let the Wi-Fi state machine finish bring-up before the first render:
     * drawing (PSRAM framebuffer + FBIO_UPDATE) concurrently with early
     * driver init has been observed to hard-crash the board. */
    sleep(2);

    while (true) {
        bool wifi = network_is_connected();
        bool llm = read_llm_ok();
        bool led = read_led();
        bool blink = (tick % BLINK_PERIOD) == BLINK_PHASE;

        if (g_lcd_hold) {
            tick++;
            sleep(TICK_INTERVAL_S);
            continue;
        }
        if (first || wifi != g_lcd.wifi || llm != g_lcd.llm ||
            led != g_lcd.led || blink != prev_blink) {
            g_lcd.wifi = wifi;
            g_lcd.llm = llm;
            g_lcd.led = led;
            render(blink);
            prev_blink = blink;
            first = false;
        }
        tick++;
        sleep(TICK_INTERVAL_S);
    }
    return NULL; /* not reached */
}

int hm_lcd_release(void)
{
    g_lcd_hold = 0;
    return 0;
}

/* ── public interface ─────────────────────────────────────────── */

int hm_lcd_display_start(void)
{
    return agent_task_create(display_thread, "hm_lcddisp", 6144, NULL, 60);
}

/* ── HomeMind voice flow indicator（voice 流程期间接管屏幕）────── */
int hm_lcd_show_text(const char *s)
{
    struct fb_area_s area;

    if (g_lcd.fd <= 0)
        return -1;
    g_lcd_hold = 1;
    fill_rect(0, 0, g_lcd.w - 1, g_lcd.h - 1, C_BG);
    draw_latin_centered(s, g_lcd.w / 2, 96, C_HI, 2);
    area.x = 0;
    area.y = 0;
    area.w = g_lcd.w;
    area.h = g_lcd.h;
    ioctl(g_lcd.fd, FBIO_UPDATE, (unsigned long)((uintptr_t)&area));
    return 0;
}
