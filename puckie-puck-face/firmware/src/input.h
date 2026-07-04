/**
 * input.h: the rotary encoder and its push button, turned into clean events.
 *
 * Raw quadrature edges and switch bounces are read in a dedicated FreeRTOS
 * task (the encoder needs ~2 ms polling to not miss detents; LVGL renders
 * for tens of ms at a stretch, so polling from loop() would drop steps).
 * The task pushes InputEvents into a queue; the UI drains the queue from
 * the LVGL thread. That way all LVGL calls stay on one thread, which LVGL
 * requires.
 */

#pragma once

#include <stdint.h>

namespace input {

enum class Event : uint8_t {
    RotateCw,     /* one detent clockwise */
    RotateCcw,    /* one detent counter-clockwise */
    Click,        /* short press and release */
    DoubleClick,  /* two clicks within the double-click window */
    LongPress,    /* held for 1.5 s; reserved as the future "pair" gesture */
};

/* Start the polling task. The display module must have initialized the
 * PCF8574 expander (the button lives on it) before this is called. */
void begin();

/* Non-blocking. Returns true and fills `out` if an event was waiting. */
bool poll(Event &out);

} // namespace input
