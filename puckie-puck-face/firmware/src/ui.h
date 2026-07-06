/**
 * ui.h: screen-mode navigation and the glue that routes input events.
 *
 * Double-click moves between top-level modes; single-click moves between
 * screens inside the current mode:
 *   Radio: Radio, Quick Memories, Band Select
 *   Time: Grayline, Clock
 *   Conditions: Band Conditions
 *   Controls: mode, filter, RF front end, tuner and meters
 *   System: Status
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
