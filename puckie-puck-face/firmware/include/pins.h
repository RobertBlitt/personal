/**
 * pins.h: every GPIO and I2C address on the CrowPanel 2.1" rotary display.
 *
 * PROVENANCE: all values below are copied from Elecrow's own working demo
 * (vendor repo CrowPanel-2.1inch-HMI, example/RotaryScreen_2_1.ino) rather
 * than guessed from schematics. If the display does not light up, diff
 * against that file first.
 *
 * The board wires the 480x480 ST7701S panel to the ESP32-S3's parallel RGB
 * peripheral. That eats most of the GPIO budget: 16 data lines plus 4 sync
 * lines plus a 3-wire software SPI used only once at boot to send the
 * ST7701S its init registers. Slow peripherals (touch, I/O expander) share
 * the I2C bus, and the expander soaks up the miscellaneous reset lines the
 * S3 has no pins left for.
 */

#pragma once

/* ---- I2C bus (shared by CST816 touch and PCF8574 expander) ------------- */
#define PIN_I2C_SDA 38
#define PIN_I2C_SCL 39

#define I2C_ADDR_TOUCH 0x15    /* CST816 capacitive touch controller */
#define I2C_ADDR_PCF8574 0x21  /* PCF8574 8-bit I/O expander */

/* ---- PCF8574 expander bits (P0..P7) ------------------------------------ */
/* The expander pins are named P0..P7 by the xreef PCF8574 library.        */
#define EXP_TOUCH_RST P0   /* touch controller reset, active low pulse */
#define EXP_TOUCH_INT P2   /* touch interrupt line, driven high in demo */
#define EXP_LCD_POWER P3   /* panel power rail control */
#define EXP_LCD_RESET P4   /* ST7701S reset, active low pulse */
#define EXP_ENCODER_SW P5  /* rotary encoder push button, low = pressed */

/* ---- Rotary encoder quadrature inputs (direct GPIO) --------------------- */
#define PIN_ENCODER_A 42
#define PIN_ENCODER_B 4

/* ---- Backlight and onboard LED ------------------------------------------ */
#define PIN_BACKLIGHT 6    /* PWM dimmable via LEDC */
#define PIN_STATUS_LED 43  /* small onboard LED, handy as a heartbeat */

/* ---- ST7701S init SPI (used only during panel bring-up) ----------------- */
#define PIN_LCD_CS 16
#define PIN_LCD_SCK 2
#define PIN_LCD_SDA 1

/* ---- Parallel RGB bus ---------------------------------------------------- */
#define PIN_LCD_DE 40
#define PIN_LCD_VSYNC 7
#define PIN_LCD_HSYNC 15
#define PIN_LCD_PCLK 41

/* RGB565: 5 red, 6 green, 5 blue data lines. */
#define PIN_LCD_R0 46
#define PIN_LCD_R1 3
#define PIN_LCD_R2 8
#define PIN_LCD_R3 18
#define PIN_LCD_R4 17

#define PIN_LCD_G0 14
#define PIN_LCD_G1 13
#define PIN_LCD_G2 12
#define PIN_LCD_G3 11
#define PIN_LCD_G4 10
#define PIN_LCD_G5 9

#define PIN_LCD_B0 5
#define PIN_LCD_B1 45
#define PIN_LCD_B2 48
#define PIN_LCD_B3 47
#define PIN_LCD_B4 21
