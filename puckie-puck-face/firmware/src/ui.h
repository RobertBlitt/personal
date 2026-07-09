/**
 * ui.h: the four screens and the glue that routes input events to them.
 *
 * Screens, in double-click order:
 *   0 Radio: big frequency, mode, S-meter arc, link status. Rotating the
 *     knob tunes; a click cycles the tuning step; long press is reserved
 *     for the future pairing gesture.
 *   1 Clock: UTC and local time, date, grid square.
 *   2 Bands: N0NBH solar numbers and band condition ratings.
 *   3 POTA: recent activator spots; rotate to highlight one, click to
 *     tune the radio to it (the fun party trick).
 *
 * All functions here must be called from the LVGL thread (loop()).
 */

#pragma once

namespace ui {

/* Build all screens and show the radio screen. Call after display::begin(). */
void begin();

/* Drain input events and refresh dynamic labels. Call every loop() pass;
 * it rate-limits its own refresh work internally. */
void tick();

} // namespace ui
