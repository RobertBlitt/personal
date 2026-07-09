/**
 * lv_conf.h for Puckie Puck Face (LVGL 8.3.6).
 *
 * LVGL reads this file because platformio.ini defines LV_CONF_INCLUDE_SIMPLE
 * and puts include/ on the compiler's search path. Any option NOT set here
 * silently falls back to the library default in lv_conf_internal.h, so this
 * file only lists the settings that matter for this board.
 *
 * The values mirror the lv_conf.h that Elecrow ships in their vendor repo
 * for this exact panel, minus the demo widgets we do not use.
 */

#if 1 /* Set to 0 to disable this file and fall back to pure defaults. */

#ifndef LV_CONF_H
#define LV_CONF_H

/* The ST7701 panel takes 16-bit RGB565 pixels. */
#define LV_COLOR_DEPTH 16

/* The ESP32-S3 RGB peripheral wants bytes in native order, no swap.
 * (Boards that push pixels over SPI usually need swap=1; this one is a
 * parallel RGB bus, so no.) */
#define LV_COLOR_16_SWAP 0

/* Let LVGL allocate from the normal heap instead of a fixed static pool.
 * The S3 has plenty of internal RAM for LVGL's bookkeeping; the big pixel
 * buffers are allocated separately from PSRAM in display.cpp. */
#define LV_MEM_CUSTOM 1
#define LV_MEM_CUSTOM_INCLUDE <stdlib.h>
#define LV_MEM_CUSTOM_ALLOC   malloc
#define LV_MEM_CUSTOM_FREE    free
#define LV_MEM_CUSTOM_REALLOC realloc

/* LVGL needs a millisecond clock for animations and timers. Arduino's
 * millis() is exactly that. */
#define LV_TICK_CUSTOM 1
#define LV_TICK_CUSTOM_INCLUDE "Arduino.h"
#define LV_TICK_CUSTOM_SYS_TIME_EXPR (millis())

/* Fonts. Each enabled size costs flash, so only the sizes the UI actually
 * uses are switched on. 48 is the big frequency readout. */
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_24 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_MONTSERRAT_32 1
#define LV_FONT_MONTSERRAT_40 1
#define LV_FONT_MONTSERRAT_48 1

#define LV_FONT_DEFAULT &lv_font_montserrat_16

#endif /* LV_CONF_H */

#endif /* End of "content enable" */
