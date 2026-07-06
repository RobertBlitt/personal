/**
 * net.h: WiFi, clock sync, and the dashboard data feed.
 *
 * Like radio_client, everything slow happens in a background task and the
 * UI only ever copies out plain structs under a mutex. HTTP fetches can
 * take seconds; LVGL must never wait on them.
 */

#pragma once

#include <stdint.h>

#include "config.h"

namespace net {

/* Solar and band data from N0NBH's hamqsl.com XML feed. */
struct SolarData {
    bool valid = false;
    char solarFlux[8] = "";   /* SFI, e.g. "156" */
    char aIndex[8] = "";
    char kIndex[8] = "";
    char sunspots[8] = "";
    char updated[40] = "";
    /* Band condition ratings, day and night, as short strings
     * ("Good", "Fair", "Poor") per the feed's four band groups. */
    char bandDay[4][8];
    char bandNight[4][8];
};

/* The four band-group labels hamqsl reports, in display order. */
extern const char *const kBandNames[4];

struct Status {
    bool wifiUp = false;
    char ssid[33] = "";
    char ip[16] = "";
    int32_t rssi = 0;
};

/* Kick off WiFi association and start the background fetch task. */
void begin();

/* True once associated with the AP. */
bool wifiUp();

/* True once NTP has produced a plausible wall-clock time. */
bool timeSynced();

/* Copy out the latest feed data (thread safe). */
SolarData solar();

/* Current WiFi details for the status screen. */
Status status();

} // namespace net
