/**
 * display.h: bring-up for the CrowPanel's 480x480 round screen plus the
 * LVGL glue (flush callback, touch input device, backlight).
 *
 * Call display::begin() once from setup(), before building any UI, then
 * call lv_timer_handler() from loop() like every LVGL app does.
 */

#pragma once

#include <stdint.h>

namespace display {

/* Initialize the I/O expander, reset the panel and touch controller,
 * start the ST7701S, allocate LVGL draw buffers in PSRAM, and register
 * the display and touch drivers with LVGL. Halts with a serial message
 * if the hardware does not respond. */
void begin();

/* Backlight brightness, 0..255 (LEDC PWM on the backlight pin). */
void setBacklight(uint8_t level);

} // namespace display
