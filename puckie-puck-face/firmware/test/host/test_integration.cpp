// Cross-language integration test: the firmware's real CI-V codec
// (firmware/src/civ.cpp, compiled for the host) talking to the real
// Python simulator (simulator/x6200_sim.py) over an actual TCP socket.
//
// This exercises the exact conversation the knob's radio task has with
// the radio: identify, read state, tune, change mode, read the meter.
// If the two halves of the project ever drift apart on a byte, this
// fails before any hardware is involved.
//
// Usage: test_integration <port>   (the simulator must already be
// listening on 127.0.0.1:<port>; run_tests.sh handles that)

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include "civ.h"

static int failures = 0;
#define CHECK(cond)                                                 \
    do {                                                            \
        if (!(cond)) {                                              \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            failures++;                                             \
        }                                                           \
    } while (0)

// Send one frame, read until a complete reply frame arrives (mirrors the
// firmware's transact() but with POSIX sockets instead of WiFiClient).
static bool transact(int fd, const civ::Frame &tx, civ::Reply &reply) {
    if (write(fd, tx.data, tx.len) != (ssize_t)tx.len) {
        return false;
    }
    civ::FrameParser parser;
    for (;;) {
        uint8_t buf[64];
        ssize_t n = read(fd, buf, sizeof(buf)); // 2s SO_RCVTIMEO set below
        if (n <= 0) {
            return false;
        }
        for (ssize_t i = 0; i < n; i++) {
            if (parser.feed(buf[i])) {
                return civ::parseReply(parser.body(), parser.bodyLen(), reply);
            }
        }
    }
}

int main(int argc, char **argv) {
    const int port = argc > 1 ? atoi(argv[1]) : 7373;

    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    CHECK(fd >= 0);

    struct timeval tv = {2, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        printf("FAIL: cannot connect to simulator on port %d\n", port);
        return 1;
    }

    civ::Reply r;

    // 1. Identify, same handshake the firmware uses after connecting.
    CHECK(transact(fd, civ::makeReadRadioId(), r));

    // 2. Read the frequency the simulator was started with (run_tests.sh
    //    starts it at 14,074,000 Hz).
    CHECK(transact(fd, civ::makeReadFrequency(), r));
    CHECK(r.kind == civ::Reply::Kind::Frequency);
    CHECK(r.freqHz == 14074000);

    // 3. Tune to the CI-V document's worked-example frequency and verify
    //    the readback, exercising BCD encode on our side and decode+encode
    //    on the simulator's side.
    CHECK(transact(fd, civ::makeSetFrequency(21002360), r));
    CHECK(r.kind == civ::Reply::Kind::Ok);
    CHECK(transact(fd, civ::makeReadFrequency(), r));
    CHECK(r.kind == civ::Reply::Kind::Frequency);
    CHECK(r.freqHz == 21002360);

    // 4. Mode round trip: set CW filter 1, read it back.
    CHECK(transact(fd, civ::makeSetMode((uint8_t)civ::Mode::CW, false, 1), r));
    CHECK(r.kind == civ::Reply::Kind::Ok);
    CHECK(transact(fd, civ::makeReadMode(), r));
    CHECK(r.kind == civ::Reply::Kind::ModeInfo);
    CHECK(r.mode == (uint8_t)civ::Mode::CW);
    CHECK(!r.dataMode);
    CHECK(r.filter == 1);

    // 5. Data mode flag: USB-D as an FT8 setup would use.
    CHECK(transact(fd, civ::makeSetMode((uint8_t)civ::Mode::USB, true, 2), r));
    CHECK(transact(fd, civ::makeReadMode(), r));
    CHECK(r.kind == civ::Reply::Kind::ModeInfo && r.dataMode && r.filter == 2);

    // 6. S-meter: value is animated, so only the range is checked.
    CHECK(transact(fd, civ::makeReadSMeter(), r));
    CHECK(r.kind == civ::Reply::Kind::SMeter);
    // level is uint8_t, so 0..255 by construction; reaching here means the
    // 2-byte BCD decoded cleanly.

    // 7. Band jump via the stacking register (40m), then confirm the
    //    frequency landed inside the band.
    CHECK(transact(fd, civ::makeSetBand(0x04), r));
    CHECK(r.kind == civ::Reply::Kind::Ok);
    CHECK(transact(fd, civ::makeReadFrequency(), r));
    CHECK(r.freqHz >= 7000000 && r.freqHz <= 7300000);

    // 8. Rapid-fire poll burst, approximating the firmware's polling loop
    //    cadence compressed in time: 30 cycles of freq/mode/meter.
    for (int i = 0; i < 30 && failures == 0; i++) {
        CHECK(transact(fd, civ::makeReadFrequency(), r));
        CHECK(transact(fd, civ::makeReadMode(), r));
        CHECK(transact(fd, civ::makeReadSMeter(), r));
    }

    close(fd);

    if (failures == 0) {
        printf("integration test vs live simulator: ALL PASS\n");
        return 0;
    }
    printf("integration test: %d FAILURES\n", failures);
    return 1;
}
