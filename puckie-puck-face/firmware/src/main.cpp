/**
 * main.cpp: Puckie Puck Face, a wireless control head for the Xiegu X6200.
 *
 * Runs on the ELECROW CrowPanel 2.1" HMI rotary display (ESP32-S3). See
 * the project README for the big picture and docs/architecture.md for why
 * the pieces are shaped the way they are.
 *
 * THE SHAPE OF THE PROGRAM
 * ------------------------
 * Four modules, three background tasks, one rule.
 *
 *   display  panel + touch bring-up, LVGL registration   (no task)
 *   input    encoder and button decoding                 (task, core 0)
 *   net      WiFi, NTP, and hamqsl feed                  (task, core 0)
 *   radio    CI-V over TCP to the radio or simulator     (task, core 0)
 *   ui       LVGL screens, consumes everything above    (loop, core 1)
 *
 * The rule: only the Arduino loop() thread touches LVGL. Background tasks
 * publish data through mutex-guarded snapshots and an event queue, and the
 * UI pulls from those. Break the rule and you get the classic LVGL
 * lottery: works for hours, then crashes in a render call.
 */

#include <Arduino.h>
#include <lvgl.h>

#include "config.h"
#include "display.h"
#include "input.h"
#include "net.h"
#include "radio_client.h"
#include "ui.h"

void setup() {
    Serial.begin(115200);
    Serial.println();
    Serial.println("Puckie Puck Face starting");
    Serial.printf("  radio endpoint: %s:%d\n", RADIO_HOST, RADIO_PORT);

    /* Order matters: the display module owns first contact with the I2C
     * expander, and input's button polling relies on that setup. */
    display::begin();
    input::begin();
    net::begin();    /* starts WiFi; the feeds arrive when they arrive */
    radio::begin();  /* starts trying to reach the radio/simulator */

    ui::begin();

    Serial.println("setup complete, UI running");
}

void loop() {
    /* lv_timer_handler renders pending UI work and returns how long it
     * can sleep; capping the delay keeps input latency low. */
    ui::tick();
    const uint32_t wait = lv_timer_handler();
    delay(wait > 10 ? 10 : wait);
}
