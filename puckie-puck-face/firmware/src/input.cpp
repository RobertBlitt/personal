/**
 * input.cpp: encoder quadrature decoding and button gesture detection.
 *
 * ENCODER THEORY, two-minute version. A rotary encoder has two switches,
 * A and B, ninety degrees out of phase. Turning the shaft produces square
 * waves on both; which one leads tells you the direction. This encoder
 * gives one complete cycle per detent, so the code counts a step on each
 * rising edge of A and reads B at that instant: B different from A means
 * one direction, B equal to A means the other. This matches the vendor
 * demo's decoding, so the directions agree with Elecrow's labeling.
 *
 * THE BUTTON is not a GPIO: it hangs off the PCF8574 I2C expander, so
 * "reading the button" is an I2C transaction. That is why it gets polled
 * at a lazy 5 ms rather than interrupt-driven.
 */

#include "input.h"

#include <Arduino.h>
#include "PCF8574.h"

#include "pins.h"

namespace input {

namespace {

/* A second handle to the same expander chip the display module set up.
 * The xreef library talks straight through the shared Wire bus, and both
 * users only touch their own pins, so coexistence is safe. */
PCF8574 expander(I2C_ADDR_PCF8574);

QueueHandle_t eventQueue;

constexpr uint32_t kDebounceMs = 50;
constexpr uint32_t kDoubleClickMs = 300;
constexpr uint32_t kLongPressMs = 1500;

void send(Event e) {
    /* Drop events if the queue is full rather than block; a UI thirty
     * events behind should catch up, not stall the input task. */
    xQueueSend(eventQueue, &e, 0);
}

void inputTask(void *) {
    int lastA = digitalRead(PIN_ENCODER_A);

    /* Button state machine variables. */
    bool wasPressed = false;
    uint32_t pressStartedMs = 0;
    uint32_t lastReleaseMs = 0;
    int clickCount = 0;
    bool longPressFired = false;
    uint32_t lastButtonPollMs = 0;

    for (;;) {
        /* ---- Encoder: check every pass (2 ms) ---------------------------- */
        const int a = digitalRead(PIN_ENCODER_A);
        if (a != lastA && a == HIGH) { /* rising edge of A = one detent */
            if (digitalRead(PIN_ENCODER_B) != a) {
                send(Event::RotateCcw);
            } else {
                send(Event::RotateCw);
            }
        }
        lastA = a;

        /* ---- Button: check every 5 ms (it costs an I2C transaction) ------ */
        const uint32_t now = millis();
        if (now - lastButtonPollMs >= 5) {
            lastButtonPollMs = now;
            /* Low means pressed (the pin idles high via pullup). */
            const bool pressed = expander.digitalRead(EXP_ENCODER_SW, true) == LOW;

            if (pressed && !wasPressed) {
                /* Press started. */
                if (now - lastReleaseMs > kDebounceMs) {
                    pressStartedMs = now;
                    longPressFired = false;
                }
            } else if (pressed && !longPressFired &&
                       now - pressStartedMs >= kLongPressMs) {
                /* Held long enough: fire once, swallow the release. */
                send(Event::LongPress);
                longPressFired = true;
                clickCount = 0;
            } else if (!pressed && wasPressed) {
                /* Released. */
                if (!longPressFired && now - pressStartedMs > kDebounceMs) {
                    clickCount++;
                    lastReleaseMs = now;
                }
            }

            /* Click arbitration: after the double-click window closes,
             * decide what the accumulated clicks meant. */
            if (clickCount > 0 && !pressed &&
                now - lastReleaseMs > kDoubleClickMs) {
                send(clickCount >= 2 ? Event::DoubleClick : Event::Click);
                clickCount = 0;
            }

            wasPressed = pressed;
        }

        vTaskDelay(pdMS_TO_TICKS(2));
    }
}

} // namespace

void begin() {
    pinMode(PIN_ENCODER_A, INPUT);
    pinMode(PIN_ENCODER_B, INPUT);

    /* begin() here re-attaches to the already-initialized chip; pin modes
     * were configured by the display module. */
    expander.pinMode(EXP_ENCODER_SW, INPUT_PULLUP);
    expander.begin();

    eventQueue = xQueueCreate(32, sizeof(Event));
    /* Same core as the radio task; both are light. Core 1 stays dedicated
     * to LVGL rendering. */
    xTaskCreatePinnedToCore(inputTask, "input", 3072, nullptr, 2, nullptr, 0);
}

bool poll(Event &out) {
    return xQueueReceive(eventQueue, &out, 0) == pdTRUE;
}

} // namespace input
