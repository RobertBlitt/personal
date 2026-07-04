/**
 * config.h: the only file you should need to edit to get on the air.
 *
 * Everything user-specific lives here: WiFi credentials, where the radio
 * (or the simulator standing in for it) lives on the network, your grid
 * square, and your timezone.
 */

#pragma once

/* ---- WiFi ---------------------------------------------------------------- */
/* 2.4 GHz networks only; the ESP32-S3 has no 5 GHz radio. */
#define WIFI_SSID "your-network-name"
#define WIFI_PASSWORD "your-network-password"

/* ---- Radio endpoint ------------------------------------------------------- */
/* Where CI-V frames go. Today: the machine running simulator/x6200_sim.py.
 * Later: a Raspberry Pi bridging TCP to the real radio's USB CAT port, or
 * a rooted X6200 itself. The firmware cannot tell the difference, which is
 * the whole point of the design. */
#define RADIO_HOST "192.168.1.100"
#define RADIO_PORT 7373

/* How often the radio poller refreshes state, in milliseconds. Each cycle
 * reads one of: frequency, mode, S-meter (staggered round-robin), so the
 * S-meter updates every 3rd cycle. 150 ms keeps the meter lively without
 * hammering the link. */
#define RADIO_POLL_INTERVAL_MS 150

/* ---- Station data ---------------------------------------------------------- */
/* Maidenhead grid square shown on the clock screen. BL01 covers Honolulu;
 * six characters if you know them, four is fine. */
#define STATION_GRID "BL01xh"

/* POSIX TZ string for local time. "HST10" is Hawaii Standard Time, which
 * is UTC minus 10 with no daylight saving. Mainland examples:
 *   Pacific:  "PST8PDT,M3.2.0,M11.1.0"
 *   Eastern:  "EST5EDT,M3.2.0,M11.1.0"                                   */
#define TIME_ZONE "HST10"
#define NTP_SERVER "pool.ntp.org"

/* ---- Data feeds -------------------------------------------------------------- */
/* N0NBH solar and band conditions XML (the widget seen on QRZ pages). */
#define HAMQSL_URL "https://www.hamqsl.com/solarxml.php"
/* Parks On The Air active spots. */
#define POTA_URL "https://api.pota.app/spot/activator"
/* Refresh cadence for both feeds. hamqsl asks bots to poll no more than
 * once every 30 minutes; POTA spots turn over quickly. Milliseconds. */
#define HAMQSL_REFRESH_MS (30UL * 60UL * 1000UL)
#define POTA_REFRESH_MS (2UL * 60UL * 1000UL)
/* How many POTA spots to keep and show. */
#define POTA_MAX_SPOTS 8

/* ---- Tuning behaviour ------------------------------------------------------------ */
/* Frequency steps the encoder button cycles through, in Hz. */
#define TUNE_STEPS { 10, 100, 1000, 10000 }
#define TUNE_STEP_COUNT 4
#define TUNE_STEP_DEFAULT_INDEX 2 /* start at 1 kHz */

/* ---- Display ---------------------------------------------------------------------- */
#define BACKLIGHT_DEFAULT 204 /* 0-255 PWM duty, matches vendor demo's 80% */
