/**
 * radio_profile.h: radio-specific facts kept out of UI and transport code.
 *
 * The UI should talk in human intents: tune, change mode, show signal. The
 * active profile supplies the radio name, CI-V address, mode cycle, and
 * small dialect facts needed to turn those intents into radio bytes.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

namespace profile {

struct ModeInfo {
    uint8_t code;
    const char *name;
};

struct BandInfo {
    const char *name;
    uint32_t lowHz;
    uint32_t highHz;
    uint32_t landingHz;
    uint8_t defaultMode;
    bool dataMode;
};

struct MemoryInfo {
    const char *name;
    uint32_t freqHz;
    uint8_t mode;
    bool dataMode;
};

struct RadioProfile {
    const char *name;
    uint8_t civAddress;
    uint8_t controllerAddress;
    const ModeInfo *modes;
    size_t modeCount;
    uint8_t defaultMode;
    uint8_t defaultFilter;
    const BandInfo *bands;
    size_t bandCount;
    const MemoryInfo *memories;
    size_t memoryCount;
};

/* Current compile-time active profile. Later this can come from NVS. */
const RadioProfile &active();

const char *modeName(uint8_t mode);
uint8_t nextMode(uint8_t current, bool forward);
int bandIndexForFrequency(uint32_t hz);
const char *bandNameForFrequency(uint32_t hz);

} // namespace profile
