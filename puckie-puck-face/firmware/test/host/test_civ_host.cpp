// Host-side unit test for firmware/src/civ.cpp. Uses the same byte vectors
// as simulator/test_x6200_sim.py so both halves of the conversation agree.
#include <cassert>
#include <cstdio>
#include <initializer_list>
#include <cstring>
#include "civ.h"

using namespace civ;

static int failures = 0;
#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);     \
            failures++;                                                 \
        }                                                               \
    } while (0)

int main() {
    // Doc worked example: 21,002,360 Hz <-> 60 23 00 21 00
    uint8_t bcd[5];
    freqToBcd(21002360, bcd);
    const uint8_t expect[5] = {0x60, 0x23, 0x00, 0x21, 0x00};
    CHECK(memcmp(bcd, expect, 5) == 0);

    uint32_t hz = 0;
    CHECK(bcdToFreq(expect, hz) && hz == 21002360);

    // Roundtrip a spread of ham frequencies.
    for (uint32_t f : {1840000u, 7074000u, 14074000u, 28500123u, 50313000u}) {
        freqToBcd(f, bcd);
        CHECK(bcdToFreq(bcd, hz) && hz == f);
    }

    // Garbage BCD rejected.
    const uint8_t bad[5] = {0xAB, 0, 0, 0, 0};
    CHECK(!bcdToFreq(bad, hz));

    // Levels: 255 <-> 02 55, 42 <-> 00 42.
    uint8_t lvl[2];
    levelToBcd(255, lvl);
    CHECK(lvl[0] == 0x02 && lvl[1] == 0x55);
    levelToBcd(42, lvl);
    CHECK(lvl[0] == 0x00 && lvl[1] == 0x42);
    uint8_t v;
    for (int i = 0; i <= 255; i++) {
        levelToBcd((uint8_t)i, lvl);
        CHECK(bcdToLevel(lvl, v) && v == i);
    }

    // Frame builders produce controller-to-radio framing.
    Frame f = makeReadFrequency();
    const uint8_t rf[] = {0xFE, 0xFE, 0xA4, 0x00, 0x03, 0xFD};
    CHECK(f.len == sizeof(rf) && memcmp(f.data, rf, f.len) == 0);

    f = makeSetFrequency(21002360);
    const uint8_t sf[] = {0xFE, 0xFE, 0xA4, 0x00, 0x25, 0x00,
                          0x60, 0x23, 0x00, 0x21, 0x00, 0xFD};
    CHECK(f.len == sizeof(sf) && memcmp(f.data, sf, f.len) == 0);

    f = makeSetMode(0x03, false, 1); // CW, filter 1
    const uint8_t sm[] = {0xFE, 0xFE, 0xA4, 0x00, 0x26, 0x00, 0x03, 0x00, 0x01, 0xFD};
    CHECK(f.len == sizeof(sm) && memcmp(f.data, sm, f.len) == 0);

    // Parser: feed the doc's example reply byte by byte.
    // FE FE 00 A4 03 60 23 00 21 00 FD
    {
        FrameParser p;
        const uint8_t reply[] = {0xFE, 0xFE, 0x00, 0xA4, 0x03,
                                 0x60, 0x23, 0x00, 0x21, 0x00, 0xFD};
        bool complete = false;
        for (uint8_t b : reply) complete = p.feed(b);
        CHECK(complete);
        Reply r;
        CHECK(parseReply(p.body(), p.bodyLen(), r));
        CHECK(r.kind == Reply::Kind::Frequency && r.freqHz == 21002360);
    }

    // Parser: leading noise, then an OK ack, then an S-meter reading in
    // one stream, as TCP might deliver it.
    {
        FrameParser p;
        const uint8_t stream[] = {
            0x00, 0x42,                                        // noise
            0xFE, 0xFE, 0x00, 0xA4, 0xFB, 0xFD,                // OK
            0xFE, 0xFE, 0x00, 0xA4, 0x15, 0x02, 0x01, 0x37, 0xFD, // S=137
        };
        int frames = 0;
        Reply last;
        for (uint8_t b : stream) {
            if (p.feed(b)) {
                CHECK(parseReply(p.body(), p.bodyLen(), last));
                frames++;
            }
        }
        CHECK(frames == 2);
        CHECK(last.kind == Reply::Kind::SMeter && last.level == 137);
    }

    // Parser: a frame addressed to someone else (an echo of our own
    // command, dest = radio) must be dropped.
    {
        FrameParser p;
        const uint8_t echo[] = {0xFE, 0xFE, 0xA4, 0x00, 0x03, 0xFD};
        bool complete = false;
        for (uint8_t b : echo) complete = p.feed(b);
        CHECK(!complete);
    }

    // Mode reply parsing: FE FE 00 A4 26 00 01 01 02 FD = USB-D filter 2.
    {
        FrameParser p;
        const uint8_t reply[] = {0xFE, 0xFE, 0x00, 0xA4, 0x26, 0x00,
                                 0x01, 0x01, 0x02, 0xFD};
        bool complete = false;
        for (uint8_t b : reply) complete = p.feed(b);
        CHECK(complete);
        Reply r;
        CHECK(parseReply(p.body(), p.bodyLen(), r));
        CHECK(r.kind == Reply::Kind::ModeInfo && r.mode == 0x01 &&
              r.dataMode && r.filter == 2);
    }

    CHECK(strcmp(modeName(0x01), "USB") == 0);
    CHECK(strcmp(modeName(0x99), "?") == 0);

    if (failures == 0) {
        printf("civ host tests: ALL PASS\n");
        return 0;
    }
    printf("civ host tests: %d FAILURES\n", failures);
    return 1;
}
