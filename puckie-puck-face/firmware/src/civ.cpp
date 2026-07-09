/**
 * civ.cpp: implementation of the CI-V codec declared in civ.h.
 *
 * Read this side by side with simulator/x6200_sim.py. The Python file is
 * the radio's half of the conversation; this file is the knob's half.
 */

#include "civ.h"

namespace civ {

const char *modeName(uint8_t mode) {
    switch (static_cast<Mode>(mode)) {
        case Mode::LSB: return "LSB";
        case Mode::USB: return "USB";
        case Mode::AM: return "AM";
        case Mode::CW: return "CW";
        case Mode::NFM: return "NFM";
        case Mode::CWR: return "CWR";
        default: return "?";
    }
}

/* ---- BCD helpers -------------------------------------------------------- */

void freqToBcd(uint32_t hz, uint8_t out[5]) {
    /* Peel two decimal digits per byte, least significant pair first.
     * 21,002,360 -> byte0 (6,0)=0x60, byte1 (2,3)=0x23, byte2 0x00,
     * byte3 (2,1)=0x21, byte4 0x00. */
    for (int i = 0; i < 5; i++) {
        uint8_t units = hz % 10;
        uint8_t tens = (hz / 10) % 10;
        out[i] = static_cast<uint8_t>((tens << 4) | units);
        hz /= 100;
    }
}

bool bcdToFreq(const uint8_t in[5], uint32_t &hzOut) {
    uint32_t hz = 0;
    /* Walk from the most significant byte (index 4) downward. */
    for (int i = 4; i >= 0; i--) {
        uint8_t tens = (in[i] >> 4) & 0x0F;
        uint8_t units = in[i] & 0x0F;
        if (tens > 9 || units > 9) {
            return false; /* not a decimal digit: corrupted frame */
        }
        hz = hz * 100 + tens * 10 + units;
    }
    hzOut = hz;
    return true;
}

void levelToBcd(uint8_t value, uint8_t out[2]) {
    /* 0..255 as decimal digits across two bytes: 255 -> 02 55. */
    uint8_t hundreds = value / 100;
    uint8_t tens = (value % 100) / 10;
    uint8_t units = value % 10;
    out[0] = hundreds;
    out[1] = static_cast<uint8_t>((tens << 4) | units);
}

bool bcdToLevel(const uint8_t in[2], uint8_t &valueOut) {
    uint8_t hundreds = in[0] & 0x0F;
    uint8_t tens = (in[1] >> 4) & 0x0F;
    uint8_t units = in[1] & 0x0F;
    if (hundreds > 2 || tens > 9 || units > 9) {
        return false;
    }
    uint16_t v = hundreds * 100 + tens * 10 + units;
    if (v > 255) {
        return false;
    }
    valueOut = static_cast<uint8_t>(v);
    return true;
}

/* ---- Frame builders ------------------------------------------------------ */

/* Start every outbound frame the same way: FE FE A4 00. */
static Frame frameStart() {
    Frame f;
    f.data[0] = PREAMBLE;
    f.data[1] = PREAMBLE;
    f.data[2] = ADDR_RADIO;      /* destination: the radio */
    f.data[3] = ADDR_CONTROLLER; /* source: us */
    f.len = 4;
    return f;
}

static void push(Frame &f, uint8_t b) {
    if (f.len < sizeof(f.data)) {
        f.data[f.len++] = b;
    }
}

static void finish(Frame &f) {
    push(f, TERMINATOR);
}

Frame makeReadFrequency() {
    Frame f = frameStart();
    push(f, 0x03); /* "Get active VFO frequency" */
    finish(f);
    return f;
}

Frame makeSetFrequency(uint32_t hz) {
    Frame f = frameStart();
    push(f, 0x25); /* selected/unselected VFO frequency */
    push(f, 0x00); /* sub 0x00: the currently selected VFO */
    uint8_t bcd[5];
    freqToBcd(hz, bcd);
    for (int i = 0; i < 5; i++) {
        push(f, bcd[i]);
    }
    finish(f);
    return f;
}

Frame makeReadMode() {
    Frame f = frameStart();
    push(f, 0x26); /* mode / data flag / filter */
    push(f, 0x00); /* selected VFO */
    finish(f);
    return f;
}

Frame makeSetMode(uint8_t mode, bool dataMode, uint8_t filter) {
    Frame f = frameStart();
    push(f, 0x26);
    push(f, 0x00);
    push(f, mode);
    push(f, dataMode ? 0x01 : 0x00);
    push(f, filter); /* 1..3; note the doc says this applies to both VFOs */
    finish(f);
    return f;
}

Frame makeReadSMeter() {
    Frame f = frameStart();
    push(f, 0x15);
    push(f, 0x02); /* S-meter, 0-255 over 2-byte BCD */
    finish(f);
    return f;
}

Frame makeSetBand(uint8_t bandRegister) {
    Frame f = frameStart();
    push(f, 0x1A);
    push(f, 0x01);
    push(f, bandRegister); /* 0x01=160m .. 0x0B=6m per Table 4 */
    push(f, 0x00);         /* D2: "irrelevant (any number)" per the doc */
    finish(f);
    return f;
}

Frame makeReadRadioId() {
    Frame f = frameStart();
    push(f, 0x19);
    push(f, 0x00); /* returns the CI-V address, 0xA4 */
    finish(f);
    return f;
}

/* ---- Incremental frame parser -------------------------------------------- */

bool FrameParser::feed(uint8_t byte) {
    if (!inFrame_) {
        /* Hunting for the FE FE preamble. Anything else is line noise. */
        if (byte == PREAMBLE) {
            preambles_++;
            if (preambles_ >= 2) {
                inFrame_ = true;
                len_ = 0;
            }
        } else {
            preambles_ = 0;
        }
        return false;
    }

    if (byte == TERMINATOR) {
        /* Frame complete. Reset the hunt state either way; whether the
         * frame is useful is the caller's judgement. */
        inFrame_ = false;
        preambles_ = 0;
        /* Need at least dest + src + cmd, and it must be addressed to us.
         * (On a shared CI-V bus we would also see our own transmissions
         * echoed back; those have dest == ADDR_RADIO and are dropped.) */
        return len_ >= 3 && buf_[0] == ADDR_CONTROLLER;
    }

    if (byte == PREAMBLE && len_ == 0) {
        /* Tolerate extra preamble bytes (FE FE FE ...), which some CI-V
         * devices emit for bus arbitration. */
        return false;
    }

    if (len_ < sizeof(buf_)) {
        buf_[len_++] = byte;
    } else {
        /* Oversized frame: garbage. Abandon it and resync. */
        inFrame_ = false;
        preambles_ = 0;
        len_ = 0;
    }
    return false;
}

/* ---- Reply classification -------------------------------------------------- */

bool parseReply(const uint8_t *body, size_t len, Reply &out) {
    /* body layout: [0]=dest [1]=src [2]=cmd [3...]=sub/data */
    if (len < 3 || body[0] != ADDR_CONTROLLER || body[1] != ADDR_RADIO) {
        return false;
    }
    const uint8_t cmd = body[2];
    const uint8_t *payload = body + 3;
    const size_t payloadLen = len - 3;

    switch (cmd) {
        case CODE_OK:
            out.kind = Reply::Kind::Ok;
            return true;

        case CODE_NG:
            out.kind = Reply::Kind::Ng;
            return true;

        case 0x03: /* frequency: 5 BCD bytes follow the command */
            if (payloadLen >= 5 && bcdToFreq(payload, out.freqHz)) {
                out.kind = Reply::Kind::Frequency;
                return true;
            }
            return false;

        case 0x25: /* frequency with a sub-command byte before the BCD */
            if (payloadLen >= 6 && bcdToFreq(payload + 1, out.freqHz)) {
                out.kind = Reply::Kind::Frequency;
                return true;
            }
            return false;

        case 0x26: /* sub, mode, data flag, filter */
            if (payloadLen >= 4) {
                out.mode = payload[1];
                out.dataMode = payload[2] != 0;
                out.filter = payload[3];
                out.kind = Reply::Kind::ModeInfo;
                return true;
            }
            return false;

        case 0x15: /* sub 0x02 is the S-meter; 2 BCD bytes follow */
            if (payloadLen >= 3 && payload[0] == 0x02 &&
                bcdToLevel(payload + 1, out.level)) {
                out.kind = Reply::Kind::SMeter;
                return true;
            }
            return false;

        default:
            out.kind = Reply::Kind::Other;
            return true;
    }
}

} // namespace civ
