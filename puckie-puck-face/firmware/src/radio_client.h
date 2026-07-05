/**
 * radio_client.h: the "talks to the radio" half of the firmware.
 *
 * ARCHITECTURE NOTE, read this before touching anything.
 *
 * This module is the transport seam the whole project pivots on. The UI
 * never opens sockets and never builds CI-V frames; it only does two
 * things:
 *
 *   1. reads the latest RadioSnapshot (frequency, mode, S-meter, link up?)
 *   2. calls intent functions like tuneBy() and requestMode()
 *
 * Everything network-ish happens in one FreeRTOS task that this module
 * owns. Today that task speaks CI-V over a TCP socket to the Python
 * simulator. Swapping the other end for a Raspberry Pi bridge or a rooted
 * X6200 changes nothing here. Swapping the transport itself (say, a UART
 * link to a Bluetooth co-processor board) means rewriting only this .cpp,
 * not the UI, because the intent functions are the contract.
 *
 * THREAD SAFETY: RadioSnapshot is copied out under a mutex, so the UI task
 * always sees a consistent set of values. Intents go through a FreeRTOS
 * queue. Nothing else is shared.
 */

#pragma once

#include <stdint.h>

namespace radio {

/* What the UI knows about the radio at any instant. Plain data, no
 * pointers, safe to copy around. */
struct Snapshot {
    bool linkUp = false;      /* TCP connected and radio answering */
    uint32_t freqHz = 0;      /* selected VFO frequency */
    uint8_t mode = 0x01;      /* CI-V mode byte, see profile::modeName() */
    bool dataMode = false;    /* USB-D / LSB-D flag */
    uint8_t filter = 2;       /* filter slot 1..3 */
    uint8_t sMeter = 0;       /* 0..255 per the CI-V doc */
    uint8_t rfMeter = 0;      /* TX RF output meter, 0..255 */
    uint8_t swrMeter = 0;     /* SWR meter, 0..255 */
    uint8_t voltageMeter = 0; /* supply-voltage meter, 0..255 */
    bool preamp = false;
    bool attenuator = false;
    uint8_t agc = 3;          /* 0 off, 1 fast, 2 slow, 3 auto */
    bool tuner = false;
    uint32_t staleMs = 0;     /* ms since the last successful poll */
};

/* Start the radio task. Call once from setup() after WiFi is going. */
void begin();

/* Copy out the latest state. Cheap; call it every UI frame if you like. */
Snapshot snapshot();

/* ---- Intents (things the human wants the radio to do) ------------------- */

/* Nudge the frequency by a signed amount in Hz (encoder detents call this).
 * Multiple calls between poll cycles coalesce into one set command, so
 * spinning the knob fast sends a handful of frames, not hundreds. */
void tuneBy(int32_t deltaHz);

/* Jump straight to a frequency in Hz (used by the POTA screen's
 * "tune to this spot" action). */
void tuneTo(uint32_t hz);

/* Ask for a specific mode (CI-V mode byte plus the data flag). */
void requestMode(uint8_t mode, bool dataMode);
void requestModeFilter(uint8_t mode, bool dataMode, uint8_t filter);

/* Step to the next/previous documented mode, for a quick mode toggle. */
void cycleMode(bool forward);

void setPreamp(bool enabled);
void setAttenuator(bool enabled);
void setAgc(uint8_t mode);
void setTuner(bool enabled);
void startTune();

} // namespace radio
