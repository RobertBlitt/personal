/**
 * radio_client.cpp: TCP CI-V transport plus the polling state machine.
 *
 * The task loop below does three jobs, forever:
 *
 *   1. keep the TCP connection to RADIO_HOST:RADIO_PORT alive, with a
 *      backoff so a missing radio does not melt the WiFi,
 *   2. flush any pending user intents (tune, mode change) to the radio,
 *   3. poll frequency, mode and S-meter round-robin so the UI stays fresh
 *      even when someone changes things on the radio's own front panel.
 *
 * CI-V is strictly request/reply on this link, so the code sends one frame
 * and reads until it gets a terminator or times out. Simple, and it makes
 * link health trivial to detect: a timeout means the link is sick.
 */

#include "radio_client.h"

#include <Arduino.h>
#include <WiFi.h>

#include "civ.h"
#include "config.h"

namespace radio {

namespace {

/* ---- Shared state -------------------------------------------------------- */

SemaphoreHandle_t stateMutex;
Snapshot state;              /* guarded by stateMutex */
uint32_t lastGoodPollMs = 0; /* guarded by stateMutex */

/* Pending intents, guarded by the same mutex. A full command queue would
 * be overkill: tuning coalesces into one delta, and mode changes only
 * keep the most recent request. */
int32_t pendingTuneDelta = 0;
uint32_t pendingTuneAbsolute = 0; /* 0 = none pending */
bool pendingModeValid = false;
uint8_t pendingMode = 0;
bool pendingDataMode = false;

WiFiClient client;

/* The documented mode bytes in a cycling order for cycleMode(). */
const uint8_t kModeOrder[] = {0x00, 0x01, 0x02, 0x03, 0x05, 0x07};
constexpr size_t kModeCount = sizeof(kModeOrder);

/* ---- Low-level send/receive ---------------------------------------------- */

/* Send one frame and wait for the radio's reply. Returns true and fills
 * `reply` on success. A false return means timeout or disconnection, and
 * the caller should treat the link as down. */
bool transact(const civ::Frame &tx, civ::Reply &reply, uint32_t timeoutMs = 400) {
    if (!client.connected()) {
        return false;
    }
    client.write(tx.data, tx.len);

    civ::FrameParser parser;
    const uint32_t deadline = millis() + timeoutMs;
    while (millis() < deadline) {
        while (client.available()) {
            uint8_t b = static_cast<uint8_t>(client.read());
            if (parser.feed(b)) {
                return civ::parseReply(parser.body(), parser.bodyLen(), reply);
            }
        }
        if (!client.connected()) {
            return false;
        }
        /* Yield to other tasks instead of busy-spinning the CPU. */
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    return false;
}

/* ---- Connection management ------------------------------------------------ */

bool ensureConnected() {
    if (client.connected()) {
        return true;
    }
    if (WiFi.status() != WL_CONNECTED) {
        return false;
    }
    /* connect() blocks briefly; that is fine inside our own task. */
    if (!client.connect(RADIO_HOST, RADIO_PORT, 1500)) {
        return false;
    }
    client.setNoDelay(true); /* CI-V frames are tiny; do not Nagle-buffer them */

    /* Sanity handshake: ask for the radio ID. The simulator and the real
     * radio both answer 0xA4. This also proves the thing on the far end
     * actually speaks CI-V and is not, say, someone's printer. */
    civ::Reply reply;
    if (!transact(civ::makeReadRadioId(), reply)) {
        client.stop();
        return false;
    }
    return true;
}

void markLinkDown() {
    client.stop();
    xSemaphoreTake(stateMutex, portMAX_DELAY);
    state.linkUp = false;
    xSemaphoreGive(stateMutex);
}

/* ---- Intent flushing -------------------------------------------------------- */

/* Push any queued user actions to the radio. Returns false on link error. */
bool flushIntents() {
    /* Snapshot-and-clear the pending intents under the lock, then do the
     * slow network work outside it. */
    int32_t tuneDelta;
    uint32_t tuneAbs;
    bool modeValid;
    uint8_t mode;
    bool dataMode;
    uint8_t filter;
    uint32_t currentFreq;

    xSemaphoreTake(stateMutex, portMAX_DELAY);
    tuneDelta = pendingTuneDelta;
    tuneAbs = pendingTuneAbsolute;
    modeValid = pendingModeValid;
    mode = pendingMode;
    dataMode = pendingDataMode;
    filter = state.filter;
    currentFreq = state.freqHz;
    pendingTuneDelta = 0;
    pendingTuneAbsolute = 0;
    pendingModeValid = false;
    xSemaphoreGive(stateMutex);

    if (tuneAbs != 0 || tuneDelta != 0) {
        /* An absolute jump wins; a relative delta rides on current state. */
        int64_t target = (tuneAbs != 0)
                             ? static_cast<int64_t>(tuneAbs)
                             : static_cast<int64_t>(currentFreq) + tuneDelta;
        /* Clamp to the radio's receive range so a wild knob spin cannot
         * ask for negative kilohertz. */
        if (target < 500000) target = 500000;
        if (target > 56000000) target = 56000000;

        civ::Reply reply;
        if (!transact(civ::makeSetFrequency(static_cast<uint32_t>(target)), reply)) {
            return false;
        }
        /* Reflect it locally right away so the UI does not rubber-band
         * while waiting for the next poll cycle. */
        xSemaphoreTake(stateMutex, portMAX_DELAY);
        state.freqHz = static_cast<uint32_t>(target);
        xSemaphoreGive(stateMutex);
    }

    if (modeValid) {
        civ::Reply reply;
        if (!transact(civ::makeSetMode(mode, dataMode, filter), reply)) {
            return false;
        }
        xSemaphoreTake(stateMutex, portMAX_DELAY);
        state.mode = mode;
        state.dataMode = dataMode;
        xSemaphoreGive(stateMutex);
    }

    return true;
}

/* ---- The task --------------------------------------------------------------- */

void radioTask(void *) {
    /* Round-robin poll schedule. The S-meter changes fastest but matters
     * least when it is a beat late, so an even rotation is fine. */
    int pollPhase = 0;
    uint32_t reconnectBackoffMs = 500;

    for (;;) {
        if (!ensureConnected()) {
            markLinkDown();
            vTaskDelay(pdMS_TO_TICKS(reconnectBackoffMs));
            /* Exponential backoff, capped: 0.5s, 1s, 2s, 4s, 8s, 8s... */
            reconnectBackoffMs = reconnectBackoffMs * 2;
            if (reconnectBackoffMs > 8000) {
                reconnectBackoffMs = 8000;
            }
            continue;
        }
        reconnectBackoffMs = 500; /* connected: reset the backoff */

        if (!flushIntents()) {
            markLinkDown();
            continue;
        }

        /* One poll per cycle keeps each cycle short, so user intents never
         * wait long behind polling traffic. */
        civ::Reply reply;
        bool ok = false;
        switch (pollPhase) {
            case 0:
                ok = transact(civ::makeReadFrequency(), reply) &&
                     reply.kind == civ::Reply::Kind::Frequency;
                if (ok) {
                    xSemaphoreTake(stateMutex, portMAX_DELAY);
                    /* Only accept the poll if the user is not mid-spin;
                     * otherwise the poll would briefly revert the display. */
                    if (pendingTuneDelta == 0 && pendingTuneAbsolute == 0) {
                        state.freqHz = reply.freqHz;
                    }
                    xSemaphoreGive(stateMutex);
                }
                break;
            case 1:
                ok = transact(civ::makeReadMode(), reply) &&
                     reply.kind == civ::Reply::Kind::ModeInfo;
                if (ok) {
                    xSemaphoreTake(stateMutex, portMAX_DELAY);
                    state.mode = reply.mode;
                    state.dataMode = reply.dataMode;
                    state.filter = reply.filter;
                    xSemaphoreGive(stateMutex);
                }
                break;
            case 2:
                ok = transact(civ::makeReadSMeter(), reply) &&
                     reply.kind == civ::Reply::Kind::SMeter;
                if (ok) {
                    xSemaphoreTake(stateMutex, portMAX_DELAY);
                    state.sMeter = reply.level;
                    xSemaphoreGive(stateMutex);
                }
                break;
        }
        pollPhase = (pollPhase + 1) % 3;

        if (ok) {
            xSemaphoreTake(stateMutex, portMAX_DELAY);
            state.linkUp = true;
            lastGoodPollMs = millis();
            xSemaphoreGive(stateMutex);
        } else {
            markLinkDown();
            continue;
        }

        vTaskDelay(pdMS_TO_TICKS(RADIO_POLL_INTERVAL_MS));
    }
}

} // namespace

/* ---- Public API ----------------------------------------------------------- */

void begin() {
    stateMutex = xSemaphoreCreateMutex();
    /* Core 0 keeps radio and network chatter away from core 1, where the
     * Arduino loop() runs LVGL rendering. 4 KB of stack is comfortable for
     * a task that only shuffles small buffers. */
    xTaskCreatePinnedToCore(radioTask, "radio", 4096, nullptr, 1, nullptr, 0);
}

Snapshot snapshot() {
    xSemaphoreTake(stateMutex, portMAX_DELAY);
    Snapshot copy = state;
    copy.staleMs = millis() - lastGoodPollMs;
    xSemaphoreGive(stateMutex);
    return copy;
}

void tuneBy(int32_t deltaHz) {
    xSemaphoreTake(stateMutex, portMAX_DELAY);
    pendingTuneDelta += deltaHz;
    /* Give instant visual feedback: move the local number immediately.
     * The radio catches up within one flush cycle. */
    int64_t shown = static_cast<int64_t>(state.freqHz) + deltaHz;
    if (shown < 500000) shown = 500000;
    if (shown > 56000000) shown = 56000000;
    state.freqHz = static_cast<uint32_t>(shown);
    xSemaphoreGive(stateMutex);
}

void tuneTo(uint32_t hz) {
    xSemaphoreTake(stateMutex, portMAX_DELAY);
    pendingTuneAbsolute = hz;
    pendingTuneDelta = 0;
    state.freqHz = hz;
    xSemaphoreGive(stateMutex);
}

void requestMode(uint8_t mode, bool dataMode) {
    xSemaphoreTake(stateMutex, portMAX_DELAY);
    pendingModeValid = true;
    pendingMode = mode;
    pendingDataMode = dataMode;
    xSemaphoreGive(stateMutex);
}

void cycleMode(bool forward) {
    xSemaphoreTake(stateMutex, portMAX_DELAY);
    uint8_t current = state.mode;
    xSemaphoreGive(stateMutex);

    /* Find the current mode in the cycle order, then step. */
    size_t idx = 0;
    for (size_t i = 0; i < kModeCount; i++) {
        if (kModeOrder[i] == current) {
            idx = i;
            break;
        }
    }
    idx = (idx + (forward ? 1 : kModeCount - 1)) % kModeCount;
    requestMode(kModeOrder[idx], false);
}

} // namespace radio
