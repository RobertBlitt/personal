/**
 * display.cpp: the fiddly hardware bring-up, straight from the vendor demo.
 *
 * The dance below (expander pin setup, power rail, reset pulses with those
 * exact delays, then the ST7701S init blob) is copied from Elecrow's
 * RotaryScreen_2_1.ino example because it is known to work on this panel.
 * Deviate at your own risk; the ST7701S is unforgiving about init order.
 *
 * How pixels get to the glass, in one paragraph: the ST7701S is configured
 * once over a 3-wire software SPI (the "SWSPI" pins), after which it is
 * fed like a dumb RGB monitor: the ESP32-S3's LCD peripheral streams
 * every frame continuously over the 16-bit parallel bus using DMA from
 * PSRAM. LVGL renders into two full-screen buffers in PSRAM and the flush
 * callback blits the dirty region into the panel's live framebuffer.
 */

#include "display.h"

#include <Arduino.h>
#include <Wire.h>
#include <lvgl.h>
#include <Arduino_GFX_Library.h>
#include <Adafruit_CST8XX.h>

#include "io_expander.h"
#include "pins.h"
#include "config.h"

namespace display {

namespace {

constexpr uint16_t kScreenWidth = 480;
constexpr uint16_t kScreenHeight = 480;

Adafruit_CST8XX touch;
bool touchOk = false;

/* The RGB bus wiring. Pin numbers and constructor order are the vendor's;
 * see pins.h for provenance. */
Arduino_ESP32RGBPanel *bus = new Arduino_ESP32RGBPanel(
    PIN_LCD_CS, PIN_LCD_SCK, PIN_LCD_SDA,
    PIN_LCD_DE, PIN_LCD_VSYNC, PIN_LCD_HSYNC, PIN_LCD_PCLK,
    PIN_LCD_R0, PIN_LCD_R1, PIN_LCD_R2, PIN_LCD_R3, PIN_LCD_R4,
    PIN_LCD_G0, PIN_LCD_G1, PIN_LCD_G2, PIN_LCD_G3, PIN_LCD_G4, PIN_LCD_G5,
    PIN_LCD_B0, PIN_LCD_B1, PIN_LCD_B2, PIN_LCD_B3, PIN_LCD_B4);

/* st7701_type5_init_operations is the init register blob for this exact
 * glass, shipped inside the Arduino_GFX library. The porch timings are the
 * vendor's numbers. */
Arduino_ST7701_RGBPanel *gfx = new Arduino_ST7701_RGBPanel(
    bus, GFX_NOT_DEFINED /* RST: handled via the expander instead */,
    0 /* rotation */, false /* IPS flag as vendor sets it */,
    kScreenWidth, kScreenHeight,
    st7701_type5_init_operations, sizeof(st7701_type5_init_operations),
    true /* BGR order */,
    10 /* hsync front porch */, 4 /* hsync pulse */, 20 /* hsync back porch */,
    10 /* vsync front porch */, 4 /* vsync pulse */, 20 /* vsync back porch */);

lv_disp_draw_buf_t drawBuf;

/* LVGL calls this when it has finished rendering a region ("area") into
 * one of the draw buffers and wants it on the glass. */
void flushCb(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *pixels) {
    const uint32_t w = area->x2 - area->x1 + 1;
    const uint32_t h = area->y2 - area->y1 + 1;
#if (LV_COLOR_16_SWAP != 0)
    gfx->draw16bitBeRGBBitmap(area->x1, area->y1, (uint16_t *)&pixels->full, w, h);
#else
    gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t *)&pixels->full, w, h);
#endif
    lv_disp_flush_ready(disp);
}

/* LVGL polls this to learn where fingers are. The CST816 reports one
 * touch point, which is all a knob UI needs. */
void touchReadCb(lv_indev_drv_t *, lv_indev_data_t *data) {
    if (touchOk && touch.touched()) {
        CST_TS_Point p = touch.getPoint(0);
        data->point.x = p.x;
        /* The vendor demo nudges Y to correct a small panel offset. */
        data->point.y = p.y - 20;
        data->state = LV_INDEV_STATE_PR;
    } else {
        data->state = LV_INDEV_STATE_REL;
    }
}

void resetPanelViaExpander() {
    /* One shared expander instance for the whole firmware; see
     * io_expander.h for why per-module instances would break the panel. */
    if (io_expander::begin()) {
        Serial.println("[display] PCF8574 expander OK");
    } else {
        Serial.println("[display] PCF8574 expander NOT FOUND, check I2C");
    }
    PCF8574 &expander = io_expander::get();

    /* Power the panel rail, then pulse both reset lines low. The delays
     * are the vendor's; the ST7701S datasheet wants >10 ms post-reset. */
    expander.digitalWrite(EXP_LCD_POWER, HIGH);
    delay(100);

    expander.digitalWrite(EXP_LCD_RESET, HIGH);
    delay(100);
    expander.digitalWrite(EXP_LCD_RESET, LOW);
    delay(120);
    expander.digitalWrite(EXP_LCD_RESET, HIGH);
    delay(120);

    expander.digitalWrite(EXP_TOUCH_RST, HIGH);
    delay(100);
    expander.digitalWrite(EXP_TOUCH_RST, LOW);
    delay(120);
    expander.digitalWrite(EXP_TOUCH_RST, HIGH);
    delay(120);
    expander.digitalWrite(EXP_TOUCH_INT, HIGH);
    delay(120);
}

} // namespace

void setBacklight(uint8_t level) {
    /* LEDC channel 0, 5 kHz, 8-bit resolution: flicker-free dimming. */
    static bool configured = false;
    if (!configured) {
        ledcSetup(0, 5000, 8);
        ledcAttachPin(PIN_BACKLIGHT, 0);
        configured = true;
    }
    ledcWrite(0, level);
}

void begin() {
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);

    resetPanelViaExpander();

    gfx->begin();
    gfx->fillScreen(BLACK);

    touchOk = touch.begin(&Wire, I2C_ADDR_TOUCH);
    Serial.println(touchOk ? "[display] CST816 touch OK"
                           : "[display] CST816 touch not found");

    lv_init();

    /* Two full-screen buffers in PSRAM. Full-screen double buffering costs
     * about 922 KB of the 8 MB PSRAM and gives tear-free redraws; internal
     * SRAM could never hold this. */
    const size_t bufBytes = sizeof(lv_color_t) * kScreenWidth * kScreenHeight;
    lv_color_t *buf1 = (lv_color_t *)heap_caps_malloc(bufBytes, MALLOC_CAP_SPIRAM);
    lv_color_t *buf2 = (lv_color_t *)heap_caps_malloc(bufBytes, MALLOC_CAP_SPIRAM);
    if (!buf1 || !buf2) {
        Serial.println("[display] FATAL: PSRAM draw buffer allocation failed");
        for (;;) delay(1000);
    }
    lv_disp_draw_buf_init(&drawBuf, buf1, buf2, kScreenWidth * kScreenHeight);

    static lv_disp_drv_t dispDrv;
    lv_disp_drv_init(&dispDrv);
    dispDrv.hor_res = kScreenWidth;
    dispDrv.ver_res = kScreenHeight;
    dispDrv.flush_cb = flushCb;
    dispDrv.draw_buf = &drawBuf;
    lv_disp_drv_register(&dispDrv);

    static lv_indev_drv_t indevDrv;
    lv_indev_drv_init(&indevDrv);
    indevDrv.type = LV_INDEV_TYPE_POINTER;
    indevDrv.read_cb = touchReadCb;
    lv_indev_drv_register(&indevDrv);

    setBacklight(BACKLIGHT_DEFAULT);
    /* Vendor demo drops the panel power expander line low after init;
     * mirror that behaviour. */
    io_expander::get().digitalWrite(EXP_LCD_POWER, LOW);

    Serial.println("[display] up");
}

} // namespace display
