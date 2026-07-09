/**
 * io_expander.h: single shared owner of the PCF8574 I/O expander.
 *
 * WHY THIS MODULE EXISTS (a pass-two bug fix worth remembering). The
 * display needs the expander for the LCD/touch reset lines, and the input
 * code needs it for the encoder button. The obvious approach, each module
 * creating its own PCF8574 object, is a trap: the xreef library's begin()
 * writes an initial-state byte to the chip computed only from the pins
 * THAT instance was told about (PCF8574.cpp, "resetInitial = writeModeUp
 * | readMode"). A second instance that only knows about the button pin
 * would therefore drive the LCD reset and power lines low the moment its
 * begin() ran, resetting the panel the display module just brought up.
 *
 * So: exactly one PCF8574 object lives here. Every module that touches an
 * expander pin goes through get(). begin() is called once, from
 * display::begin(), after Wire is up and all pin modes are declared.
 */

#pragma once

#include "PCF8574.h"

namespace io_expander {

/* Declare all pin modes and initialize the chip. Call exactly once, with
 * Wire already begun on the correct pins. Returns false if the chip does
 * not answer on the I2C bus. */
bool begin();

/* The one true expander instance. Safe to use from multiple tasks: the
 * ESP32 Arduino core's TwoWire serializes bus access internally with a
 * per-object lock, and this library does one transaction per call. */
PCF8574 &get();

} // namespace io_expander
