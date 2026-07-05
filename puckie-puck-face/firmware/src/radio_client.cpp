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
#include "radio_profile.h"

namespace radio {

namespace {

/* ---- Shared state -------------------------------------------------------- */

SemaphoreHandle_t stateMutex;
Snapshot state;              /* guarded by stateMutex */
uint32_t lastGoodPollMs = 0; /* guarded by stateMutex */

/* Pending intents, guarded by the same mutex. A full command queue would
 * be overkill: tuning coalesces naturally (every detent updates the
 * locally shown frequency, and one dirty flag says "push it"), and mode
 * changes only keep the most recent request.
 *
 * Design note from the pass-two review: the shown frequency
 * (state.freqHz) is the single source of truth for what to send. An
 * earlier version accumulated a separate delta AND optimistically moved
 * the shown value, then added the delta again at send time, doubling
 * every encoder step. One value plus one flag cannot double-count. */
bool pendingFreqSend = false;
bool pendingModeValid = false;
uint8_t pendingMode = 0;
bool pendingDataMode = false;
uint8_t pendingFilter = 2;
bool pendingPreampValid = false;
bool pendingPreamp = false;
bool pendingAttenuatorValid = false;
bool pendingAttenuator = false;
bool pendingAgcValid = false;
uint8_t pendingAgc = 3;
bool pendingTunerValid = false;
uint8_t pendingTunerCommand = 0;

WiFiClient client;

/* ---- Low-level send/receive ---------------------------------------------- */

/* Send one frame and wait for the radio's reply. Returns true and fills
 * `reply` on success. A false return means timeout or disconnection, and
 * the caller should treat the link as down. */
bool transact(const civ::Frame &tx, civ::Reply &reply, uint32_t timeoutMs = 1000) {
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
    bool freqSend;
    uint32_t freqToSend;
    bool modeValid;
    uint8_t mode;
    bool dataMode;
    uint8_t filter;
    bool preampValid;
    bool preamp;
    bool attenuatorValid;
    bool attenuator;
    bool agcValid;
    uint8_t agc;
    bool tunerValid;
    uint8_t tunerCommand;

    xSemaphoreTake(stateMutex, portMAX_DELAY);
    freqSend = pendingFreqSend;
    freqToSend = state.freqHz; /* already clamped by tuneBy/tuneTo */
    modeValid = pendingModeValid;
    mode = pendingMode;
    dataMode = pendingDataMode;
    filter = pendingFilter;
    preampValid = pendingPreampValid;
    preamp = pendingPreamp;
    attenuatorValid = pendingAttenuatorValid;
    attenuator = pendingAttenuator;
    agcValid = pendingAgcValid;
    agc = pendingAgc;
    tunerValid = pendingTunerValid;
    tunerCommand = pendingTunerCommand;
    pendingFreqSend = false;
    pendingModeValid = false;
    pendingPreampValid = false;
    pendingAttenuatorValid = false;
    pendingAgcValid = false;
    pendingTunerValid = false;
    xSemaphoreGive(stateMutex);

    if (freqSend) {
        civ::Reply reply;
        if (!transact(civ::makeSetFrequency(freqToSend), reply)) {
            /* Link error: re-arm the flag so the frequency is pushed once
             * the connection comes back. Any detents that landed while we
             * were sending set the flag again anyway. */
            xSemaphoreTake(stateMutex, portMAX_DELAY);
            pendingFreqSend = true;
            xSemaphoreGive(stateMutex);
            return false;
        }
    }

    if (modeValid) {
        civ::Reply reply;
        if (!transact(civ::makeSetMode(mode, dataMode, filter), reply)) {
            xSemaphoreTake(stateMutex, portMAX_DELAY);
            pendingModeValid = true;
            pendingMode = mode;
            pendingDataMode = dataMode;
            xSemaphoreGive(stateMutex);
            return false;
        }
        xSemaphoreTake(stateMutex, portMAX_DELAY);
        state.mode = mode;
        state.dataMode = dataMode;
        xSemaphoreGive(stateMutex);
    }

    if (preampValid) {
        civ::Reply reply;
        if (!transact(civ::makeSetFunction(0x02, preamp ? 1 : 0), reply)) {
            return false;
        }
    }
    if (attenuatorValid) {
        civ::Reply reply;
        if (!transact(civ::makeSetAttenuator(attenuator), reply)) {
            return false;
        }
    }
    if (agcValid) {
        civ::Reply reply;
        if (!transact(civ::makeSetFunction(0x12, agc), reply)) {
            return false;
        }
    }
    if (tunerValid) {
        civ::Reply reply;
        if (!transact(civ::makeSetTuner(tunerCommand), reply)) {
            return false;
        }
    }

    return true;
}

/* ---- The task --------------------------------------------------------------- */

void radioTask(void *) {
    /* Round-robin poll schedule. The S-meter changes fastest but matters
     * least when it is a beat late, so an even rotation is fine. */
    int pollPhase = 0;
    int controlPollPhase = 0;
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
                    if (!pendingFreqSend) {
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

        /* One lower-rate control/meter query per cycle. These values each
         * refresh about every 3.5 seconds without slowing the core VFO poll. */
        civ::Reply controlReply;
        bool controlOk = false;
        switch (controlPollPhase) {
            case 0:
                controlOk = transact(civ::makeReadAttenuator(), controlReply) &&
                            controlReply.kind == civ::Reply::Kind::Attenuator;
                if (controlOk) {
                    xSemaphoreTake(stateMutex, portMAX_DELAY);
                    state.attenuator = controlReply.value != 0;
                    xSemaphoreGive(stateMutex);
                }
                break;
            case 1:
                controlOk = transact(civ::makeReadFunction(0x02), controlReply) &&
                            controlReply.kind == civ::Reply::Kind::Function;
                if (controlOk) {
                    xSemaphoreTake(stateMutex, portMAX_DELAY);
                    state.preamp = controlReply.value != 0;
                    xSemaphoreGive(stateMutex);
                }
                break;
            case 2:
                controlOk = transact(civ::makeReadFunction(0x12), controlReply) &&
                            controlReply.kind == civ::Reply::Kind::Function;
                if (controlOk) {
                    xSemaphoreTake(stateMutex, portMAX_DELAY);
                    state.agc = controlReply.value;
                    xSemaphoreGive(stateMutex);
                }
                break;
            case 3:
                controlOk = transact(civ::makeReadTuner(), controlReply) &&
                            controlReply.kind == civ::Reply::Kind::Tuner;
                if (controlOk) {
                    xSemaphoreTake(stateMutex, portMAX_DELAY);
                    state.tuner = controlReply.value != 0;
                    xSemaphoreGive(stateMutex);
                }
                break;
            case 4:
                controlOk = transact(civ::makeReadMeter(0x11), controlReply) &&
                            controlReply.kind == civ::Reply::Kind::Meter;
                if (controlOk) {
                    xSemaphoreTake(stateMutex, portMAX_DELAY);
                    state.rfMeter = controlReply.level;
                    xSemaphoreGive(stateMutex);
                }
                break;
            case 5:
                controlOk = transact(civ::makeReadMeter(0x12), controlReply) &&
                            controlReply.kind == civ::Reply::Kind::Meter;
                if (controlOk) {
                    xSemaphoreTake(stateMutex, portMAX_DELAY);
                    state.swrMeter = controlReply.level;
                    xSemaphoreGive(stateMutex);
                }
                break;
            case 6:
                controlOk = transact(civ::makeReadMeter(0x15), controlReply) &&
                            controlReply.kind == civ::Reply::Kind::Meter;
                if (controlOk) {
                    xSemaphoreTake(stateMutex, portMAX_DELAY);
                    state.voltageMeter = controlReply.level;
                    xSemaphoreGive(stateMutex);
                }
                break;
        }
        controlPollPhase = (controlPollPhase + 1) % 7;

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
    const profile::RadioProfile &p = profile::active();
    civ::setAddresses(p.civAddress, p.controllerAddress);

    stateMutex = xSemaphoreCreateMutex();
    state.mode = p.defaultMode;
    state.filter = p.defaultFilter;
    pendingFilter = p.defaultFilter;
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
    /* Move the local number immediately for instant dial feedback, clamp
     * to the receive range, and flag it for sending. The radio catches up
     * within one flush cycle. */
    int64_t shown = static_cast<int64_t>(state.freqHz) + deltaHz;
    if (shown < 500000) shown = 500000;
    if (shown > 56000000) shown = 56000000;
    state.freqHz = static_cast<uint32_t>(shown);
    pendingFreqSend = true;
    xSemaphoreGive(stateMutex);
}

void tuneTo(uint32_t hz) {
    xSemaphoreTake(stateMutex, portMAX_DELAY);
    if (hz < 500000) hz = 500000;
    if (hz > 56000000) hz = 56000000;
    state.freqHz = hz;
    pendingFreqSend = true;
    xSemaphoreGive(stateMutex);
}

void requestMode(uint8_t mode, bool dataMode) {
    Snapshot s = snapshot();
    requestModeFilter(mode, dataMode, s.filter);
}

void requestModeFilter(uint8_t mode, bool dataMode, uint8_t filter) {
    xSemaphoreTake(stateMutex, portMAX_DELAY);
    pendingModeValid = true;
    pendingMode = mode;
    pendingDataMode = dataMode;
    pendingFilter = filter < 1 ? 1 : (filter > 3 ? 3 : filter);
    xSemaphoreGive(stateMutex);
}

void cycleMode(bool forward) {
    xSemaphoreTake(stateMutex, portMAX_DELAY);
    uint8_t current = state.mode;
    xSemaphoreGive(stateMutex);

    requestMode(profile::nextMode(current, forward), false);
}

void setPreamp(bool enabled) {
    xSemaphoreTake(stateMutex, portMAX_DELAY);
    state.preamp = enabled;
    pendingPreamp = enabled;
    pendingPreampValid = true;
    xSemaphoreGive(stateMutex);
}

void setAttenuator(bool enabled) {
    xSemaphoreTake(stateMutex, portMAX_DELAY);
    state.attenuator = enabled;
    pendingAttenuator = enabled;
    pendingAttenuatorValid = true;
    xSemaphoreGive(stateMutex);
}

void setAgc(uint8_t mode) {
    if (mode > 3) mode = 3;
    xSemaphoreTake(stateMutex, portMAX_DELAY);
    state.agc = mode;
    pendingAgc = mode;
    pendingAgcValid = true;
    xSemaphoreGive(stateMutex);
}

void setTuner(bool enabled) {
    xSemaphoreTake(stateMutex, portMAX_DELAY);
    state.tuner = enabled;
    pendingTunerCommand = enabled ? 1 : 0;
    pendingTunerValid = true;
    xSemaphoreGive(stateMutex);
}

void startTune() {
    xSemaphoreTake(stateMutex, portMAX_DELAY);
    state.tuner = true;
    pendingTunerCommand = 2;
    pendingTunerValid = true;
    xSemaphoreGive(stateMutex);
}

} // namespace radio
