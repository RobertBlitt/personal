/**
 * net.cpp: WiFi bring-up, NTP, and the hamqsl + POTA fetchers.
 *
 * A NOTE ON THE XML "PARSER". The N0NBH feed is small and its structure
 * has been stable for many years, so instead of dragging in an XML
 * library this file does honest string searching for the handful of tags
 * it needs. If the feed ever changes shape the worst case is empty
 * strings on the dashboard, not a crash.
 *
 * A NOTE ON TLS. Both feeds are fetched over HTTPS with certificate
 * verification disabled (setInsecure). That is a deliberate tradeoff for
 * a hobby dashboard: pinning CA certificates on a microcontroller means
 * shipping updates when the sites rotate certs. Nothing secret travels on
 * these connections; treat the data as public and unauthenticated.
 */

#include "net.h"

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>

namespace net {

const char *const kBandNames[4] = {
    "80m-40m", "30m-20m", "17m-15m", "12m-10m",
};

namespace {

SemaphoreHandle_t dataMutex;
SolarData solarData;   /* guarded by dataMutex */
PotaData potaData;     /* guarded by dataMutex */
volatile bool ntpDone = false;

/* ---- Small helpers -------------------------------------------------------- */

/* Copy the text between <tag> and </tag> into out. Returns false when the
 * tag is missing. Sizes are enforced, truncating politely. */
bool extractTag(const String &xml, const char *tag, char *out, size_t outSize) {
    String open = String("<") + tag + ">";
    String close = String("</") + tag + ">";
    int start = xml.indexOf(open);
    if (start < 0) return false;
    start += open.length();
    int end = xml.indexOf(close, start);
    if (end < 0) return false;
    String value = xml.substring(start, end);
    value.trim();
    strlcpy(out, value.c_str(), outSize);
    return true;
}

/* Pull one band condition rating out of the hamqsl XML. The tags look like:
 *   <band name="80m-40m" time="day">Good</band>                         */
bool extractBand(const String &xml, const char *bandName, const char *dayNight,
                 char *out, size_t outSize) {
    String needle = String("name=\"") + bandName + "\" time=\"" + dayNight + "\">";
    int start = xml.indexOf(needle);
    if (start < 0) return false;
    start += needle.length();
    int end = xml.indexOf("<", start);
    if (end < 0) return false;
    String value = xml.substring(start, end);
    value.trim();
    strlcpy(out, value.c_str(), outSize);
    return true;
}

/* One HTTPS GET, body into a String. Returns an empty string on failure.
 * Strings are fine here: these bodies are tens of KB and land in PSRAM
 * because the Arduino core routes large allocations there. */
String httpGet(const char *url, uint32_t timeoutMs = 15000) {
    WiFiClientSecure tls;
    tls.setInsecure(); /* see the TLS note at the top of this file */
    HTTPClient http;
    http.setTimeout(timeoutMs);
    http.setConnectTimeout(timeoutMs);
    /* Some feeds care about having a UA; be a polite, identifiable bot. */
    http.setUserAgent("PuckiePuckFace/0.1 (ESP32 ham dashboard)");
    if (!http.begin(tls, url)) {
        return String();
    }
    const int code = http.GET();
    String body;
    if (code == HTTP_CODE_OK) {
        body = http.getString();
    } else {
        Serial.printf("[net] GET %s -> %d\n", url, code);
    }
    http.end();
    return body;
}

/* ---- Feed refreshers -------------------------------------------------------- */

void refreshSolar() {
    String xml = httpGet(HAMQSL_URL);
    if (xml.length() == 0) return;

    SolarData fresh;
    extractTag(xml, "solarflux", fresh.solarFlux, sizeof(fresh.solarFlux));
    extractTag(xml, "aindex", fresh.aIndex, sizeof(fresh.aIndex));
    extractTag(xml, "kindex", fresh.kIndex, sizeof(fresh.kIndex));
    extractTag(xml, "sunspots", fresh.sunspots, sizeof(fresh.sunspots));
    extractTag(xml, "updated", fresh.updated, sizeof(fresh.updated));
    for (int i = 0; i < 4; i++) {
        extractBand(xml, kBandNames[i], "day", fresh.bandDay[i], sizeof(fresh.bandDay[i]));
        extractBand(xml, kBandNames[i], "night", fresh.bandNight[i], sizeof(fresh.bandNight[i]));
    }
    fresh.valid = fresh.solarFlux[0] != '\0';

    xSemaphoreTake(dataMutex, portMAX_DELAY);
    solarData = fresh;
    xSemaphoreGive(dataMutex);
    Serial.printf("[net] solar data refreshed (SFI %s)\n", fresh.solarFlux);
}

void refreshPota() {
    String json = httpGet(POTA_URL);
    if (json.length() == 0) return;

    /* The full spot list is large. A filter document tells ArduinoJson to
     * keep only the fields we display and discard the rest during parsing,
     * which keeps the memory footprint small and predictable. */
    StaticJsonDocument<192> filter;
    filter[0]["activator"] = true;
    filter[0]["frequency"] = true;
    filter[0]["reference"] = true;
    filter[0]["mode"] = true;
    filter[0]["name"] = true;

    DynamicJsonDocument doc(32 * 1024);
    /* Parse from the raw C string: unambiguous input type for ArduinoJson,
     * which copies what the filter keeps into `doc`. */
    DeserializationError err =
        deserializeJson(doc, json.c_str(), DeserializationOption::Filter(filter));
    if (err) {
        Serial.printf("[net] POTA JSON parse failed: %s\n", err.c_str());
        return;
    }

    PotaData fresh;
    for (JsonObject spot : doc.as<JsonArray>()) {
        if (fresh.count >= POTA_MAX_SPOTS) break;
        PotaSpot &s = fresh.spots[fresh.count];
        strlcpy(s.activator, spot["activator"] | "", sizeof(s.activator));
        strlcpy(s.reference, spot["reference"] | "", sizeof(s.reference));
        strlcpy(s.parkName, spot["name"] | "", sizeof(s.parkName));
        strlcpy(s.modeStr, spot["mode"] | "", sizeof(s.modeStr));
        /* The API reports frequency in kHz, usually as a string
         * ("14285.0") but occasionally tooling shifts types, so accept a
         * bare number too. A `| "0"` default alone would silently turn a
         * numeric value into 0 Hz. Converted to Hz for consistency with
         * everything else in this project. */
        JsonVariantConst freqField = spot["frequency"];
        double khz = 0.0;
        if (freqField.is<const char *>()) {
            khz = atof(freqField.as<const char *>());
        } else {
            khz = freqField.as<double>();
        }
        s.freqHz = static_cast<uint32_t>(khz * 1000.0);
        fresh.count++;
    }
    fresh.valid = fresh.count > 0;

    xSemaphoreTake(dataMutex, portMAX_DELAY);
    potaData = fresh;
    xSemaphoreGive(dataMutex);
    Serial.printf("[net] POTA refreshed, %d spots\n", fresh.count);
}

/* ---- The task ----------------------------------------------------------------- */

void netTask(void *) {
    /* Wait for WiFi before doing anything time-or-network flavored. */
    while (WiFi.status() != WL_CONNECTED) {
        vTaskDelay(pdMS_TO_TICKS(250));
    }
    Serial.printf("[net] WiFi up, IP %s\n", WiFi.localIP().toString().c_str());

    /* NTP: configTzTime sets the POSIX TZ string and starts SNTP in the
     * background. We then watch for the year to become plausible, which
     * is the standard "has NTP landed yet" trick on ESP32. */
    configTzTime(TIME_ZONE, NTP_SERVER);
    for (int i = 0; i < 100 && !ntpDone; i++) {
        time_t now = time(nullptr);
        struct tm tmNow;
        localtime_r(&now, &tmNow);
        if (tmNow.tm_year + 1900 >= 2020) {
            ntpDone = true;
            Serial.println("[net] NTP time acquired");
        } else {
            vTaskDelay(pdMS_TO_TICKS(200));
        }
    }

    uint32_t lastSolarMs = 0;
    uint32_t lastPotaMs = 0;
    bool firstRun = true;

    for (;;) {
        if (WiFi.status() == WL_CONNECTED) {
            const uint32_t now = millis();
            if (firstRun || now - lastSolarMs >= HAMQSL_REFRESH_MS) {
                refreshSolar();
                lastSolarMs = now;
            }
            if (firstRun || now - lastPotaMs >= POTA_REFRESH_MS) {
                refreshPota();
                lastPotaMs = now;
            }
            firstRun = false;
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

} // namespace

void begin() {
    dataMutex = xSemaphoreCreateMutex();

    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    WiFi.setAutoReconnect(true);

    /* 8 KB stack: TLS handshakes are stack-hungry. Core 0 with the other
     * background work. */
    xTaskCreatePinnedToCore(netTask, "net", 8192, nullptr, 1, nullptr, 0);
}

bool wifiUp() {
    return WiFi.status() == WL_CONNECTED;
}

bool timeSynced() {
    return ntpDone;
}

SolarData solar() {
    xSemaphoreTake(dataMutex, portMAX_DELAY);
    SolarData copy = solarData;
    xSemaphoreGive(dataMutex);
    return copy;
}

PotaData pota() {
    xSemaphoreTake(dataMutex, portMAX_DELAY);
    PotaData copy = potaData;
    xSemaphoreGive(dataMutex);
    return copy;
}

Status status() {
    Status s;
    s.wifiUp = WiFi.status() == WL_CONNECTED;
    strlcpy(s.ssid, WIFI_SSID, sizeof(s.ssid));
    if (s.wifiUp) {
        strlcpy(s.ip, WiFi.localIP().toString().c_str(), sizeof(s.ip));
        s.rssi = WiFi.RSSI();
    } else {
        strlcpy(s.ip, "not connected", sizeof(s.ip));
    }
    return s;
}

} // namespace net
