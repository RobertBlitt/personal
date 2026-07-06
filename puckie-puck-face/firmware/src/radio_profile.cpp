#include "radio_profile.h"

namespace profile {

namespace {

constexpr ModeInfo kX6200Modes[] = {
    {0x00, "LSB"},
    {0x01, "USB"},
    {0x03, "CW"},
    {0x07, "CWR"},
    {0x02, "AM"},
    {0x05, "NFM"},
};

/* Conservative US-region landing points. They are profile data so regional
 * variants and future radios can replace them without changing the UI. */
constexpr BandInfo kX6200Bands[] = {
    {"160m", 1800000, 2000000, 1900000, 0x00, false},
    {"80m", 3500000, 4000000, 3900000, 0x00, false},
    {"60m", 5330000, 5407000, 5357000, 0x01, false},
    {"40m", 7000000, 7300000, 7200000, 0x00, false},
    {"30m", 10100000, 10150000, 10136000, 0x01, true},
    {"20m", 14000000, 14350000, 14200000, 0x01, false},
    {"17m", 18068000, 18168000, 18100000, 0x01, false},
    {"15m", 21000000, 21450000, 21250000, 0x01, false},
    {"12m", 24890000, 24990000, 24940000, 0x01, false},
    {"10m", 28000000, 29700000, 28400000, 0x01, false},
    {"6m", 50000000, 54000000, 50125000, 0x01, false},
};

constexpr MemoryInfo kX6200Memories[] = {
    {"40m FT8", 7074000, 0x01, true},
    {"30m FT8", 10136000, 0x01, true},
    {"20m FT8", 14074000, 0x01, true},
    {"17m FT8", 18100000, 0x01, true},
    {"15m FT8", 21074000, 0x01, true},
    {"10m FT8", 28074000, 0x01, true},
    {"20m Call", 14300000, 0x01, false},
    {"6m Call", 50125000, 0x01, false},
};

constexpr RadioProfile kX6200 = {
    "Xiegu X6200",
    0xA4,
    0x00,
    kX6200Modes,
    sizeof(kX6200Modes) / sizeof(kX6200Modes[0]),
    0x01,
    2,
    kX6200Bands,
    sizeof(kX6200Bands) / sizeof(kX6200Bands[0]),
    kX6200Memories,
    sizeof(kX6200Memories) / sizeof(kX6200Memories[0]),
};

} // namespace

const RadioProfile &active() {
    return kX6200;
}

const char *modeName(uint8_t mode) {
    const RadioProfile &p = active();
    for (size_t i = 0; i < p.modeCount; i++) {
        if (p.modes[i].code == mode) {
            return p.modes[i].name;
        }
    }
    return "?";
}

uint8_t nextMode(uint8_t current, bool forward) {
    const RadioProfile &p = active();
    size_t idx = 0;
    for (size_t i = 0; i < p.modeCount; i++) {
        if (p.modes[i].code == current) {
            idx = i;
            break;
        }
    }
    idx = (idx + (forward ? 1 : p.modeCount - 1)) % p.modeCount;
    return p.modes[idx].code;
}

int bandIndexForFrequency(uint32_t hz) {
    const RadioProfile &p = active();
    for (size_t i = 0; i < p.bandCount; i++) {
        if (hz >= p.bands[i].lowHz && hz <= p.bands[i].highHz) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

const char *bandNameForFrequency(uint32_t hz) {
    const int index = bandIndexForFrequency(hz);
    if (index < 0) {
        return "GEN";
    }
    const RadioProfile &p = active();
    return p.bands[index].name;
}

} // namespace profile
