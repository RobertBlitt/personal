#include "io_expander.h"

#include "pins.h"

namespace io_expander {

namespace {
PCF8574 chip(I2C_ADDR_PCF8574);
} // namespace

bool begin() {
    /* Declare every pin this project uses BEFORE begin(), because begin()
     * writes the chip's initial state from these declarations. A pin left
     * undeclared here gets driven low at init, which for the LCD reset
     * line means a blank screen. */
    chip.pinMode(EXP_TOUCH_RST, OUTPUT);
    chip.pinMode(EXP_TOUCH_INT, OUTPUT);
    chip.pinMode(EXP_LCD_POWER, OUTPUT);
    chip.pinMode(EXP_LCD_RESET, OUTPUT);
    chip.pinMode(EXP_ENCODER_SW, INPUT_PULLUP);
    return chip.begin();
}

PCF8574 &get() {
    return chip;
}

} // namespace io_expander
