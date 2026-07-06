/**
 * civ.h: encoder/decoder for the Xiegu X6200's CI-V dialect.
 *
 * CI-V is ICOM's decades-old CAT protocol. Frames look like:
 *
 *     FE FE <dest> <src> <cmd> [sub] [data...] FD
 *
 * The X6200 answers at address 0xA4 and calls the controller 0x00, per the
 * Radioddity "CI-V implementation of Xiegu X6200" document (firmware
 * V1.0.6), which is the source for every opcode used in this project.
 *
 * This module is deliberately pure: no sockets, no globals, no Arduino
 * calls except types. That keeps it testable and identical in spirit to
 * simulator/x6200_sim.py, whose unit tests are the executable spec. If a
 * byte pattern here ever disagrees with the simulator tests, the tests win.
 */

#pragma once

#include <stdint.h>
#include <stddef.h>

namespace civ {

/* Protocol constants from the Radioddity document. The default addresses
 * match the X6200; the active profile can override them at runtime. */
constexpr uint8_t PREAMBLE = 0xFE;
constexpr uint8_t TERMINATOR = 0xFD;
constexpr uint8_t ADDR_RADIO = 0xA4;
constexpr uint8_t ADDR_CONTROLLER = 0x00;
constexpr uint8_t CODE_OK = 0xFB; /* "FB", command accepted */
constexpr uint8_t CODE_NG = 0xFA; /* "NG", command rejected */

/* Mode numbers from Table 3 of the document. */
enum class Mode : uint8_t {
    LSB = 0x00,
    USB = 0x01,
    AM = 0x02,
    CW = 0x03,
    NFM = 0x05,
    CWR = 0x07,
};

/* Human-readable name for a mode byte ("USB", "CW", ...). Unknown bytes
 * return "?" rather than crashing the UI. */
const char *modeName(uint8_t mode);

void setAddresses(uint8_t radioAddress, uint8_t controllerAddress);
uint8_t radioAddress();
uint8_t controllerAddress();

/* A frame under construction or freshly parsed. CI-V frames are tiny; the
 * longest documented X6200 frame is well under 20 bytes. */
struct Frame {
    uint8_t data[24];
    size_t len = 0;
};

/* ---- BCD helpers ---------------------------------------------------------
 * CI-V packs two decimal digits per byte. Frequencies are 5 bytes with the
 * least significant digits first. The document's worked example:
 * 21,002,360 Hz <-> 60 23 00 21 00. */

/* Encode Hz into 5 little-endian BCD bytes at out[0..4]. */
void freqToBcd(uint32_t hz, uint8_t out[5]);

/* Decode 5 BCD bytes to Hz. Returns false on a non-decimal nibble. */
bool bcdToFreq(const uint8_t in[5], uint32_t &hzOut);

/* Levels and meters are 2-byte BCD in 0..255: 255 <-> 02 55. */
void levelToBcd(uint8_t value, uint8_t out[2]);
bool bcdToLevel(const uint8_t in[2], uint8_t &valueOut);

/* ---- Frame builders (controller to radio: FE FE A4 00 ... FD) ---------- */

Frame makeReadFrequency();                 /* cmd 0x03 */
Frame makeSetFrequency(uint32_t hz);       /* cmd 0x25 sub 0x00 */
Frame makeReadMode();                      /* cmd 0x26 sub 0x00 */
Frame makeSetMode(uint8_t mode, bool dataMode, uint8_t filter); /* 0x26 */
Frame makeReadSMeter();                    /* cmd 0x15 sub 0x02 */
Frame makeReadMeter(uint8_t meter);        /* cmd 0x15: RF/SWR/voltage */
Frame makeReadAttenuator();                /* cmd 0x11 */
Frame makeSetAttenuator(bool enabled);
Frame makeReadFunction(uint8_t function);  /* cmd 0x16: preamp/AGC/etc. */
Frame makeSetFunction(uint8_t function, uint8_t value);
Frame makeReadTuner();                     /* cmd 0x1C sub 0x01 */
Frame makeSetTuner(uint8_t command);       /* 0=off, 1=on, 2=start tune */
Frame makeSetBand(uint8_t bandRegister);   /* cmd 0x1A sub 0x01 */
Frame makeReadRadioId();                   /* cmd 0x19 sub 0x00 */

/* ---- Incremental frame extraction ----------------------------------------
 * TCP hands us an arbitrary byte stream; this state machine finds complete
 * FE FE ... FD frames in it. Feed bytes one at a time; when a full frame
 * addressed to the CONTROLLER is complete, feed() returns true and the
 * frame (preamble and terminator stripped, so: dest src cmd [sub] [data])
 * is in `body`. Frames addressed elsewhere are dropped silently, matching
 * how devices on a shared CI-V bus ignore traffic that is not theirs. */
class FrameParser {
public:
    bool feed(uint8_t byte);
    const uint8_t *body() const { return buf_; }
    size_t bodyLen() const { return len_; }

private:
    uint8_t buf_[24];
    size_t len_ = 0;
    int preambles_ = 0;
    bool inFrame_ = false;
};

/* ---- Reply classification -------------------------------------------------
 * Given a parsed body (dest src cmd ...), pull out what the poller needs. */

struct Reply {
    enum class Kind {
        Ok,        /* FB acknowledgement */
        Ng,        /* FA rejection */
        Frequency, /* cmd 0x03 or 0x25: freqHz is valid */
        ModeInfo,  /* cmd 0x26: mode, dataMode, filter are valid */
        SMeter,    /* cmd 0x15 0x02: level is valid */
        Meter,     /* cmd 0x15 other sub-command: sub and level are valid */
        Attenuator,/* cmd 0x11: value is valid */
        Function,  /* cmd 0x16: sub and value are valid */
        Tuner,     /* cmd 0x1C 0x01: value is valid */
        Other,     /* something we did not ask about */
    };
    Kind kind = Kind::Other;
    uint32_t freqHz = 0;
    uint8_t mode = 0;
    bool dataMode = false;
    uint8_t filter = 1;
    uint8_t level = 0;
    uint8_t sub = 0;
    uint8_t value = 0;
};

/* Parse a radio-to-controller body into a Reply. Returns false when the
 * body is malformed or not addressed to us. */
bool parseReply(const uint8_t *body, size_t len, Reply &out);

} // namespace civ
