// WiFi stub for host syntax-checking. Mirrors the slice of the ESP32
// Arduino WiFi API used by radio_client.cpp and net.cpp.
#pragma once
#include "Arduino.h"

#define WL_CONNECTED 3
enum { WIFI_STA = 1 };

struct IPAddress {
    String toString() const { return String("0.0.0.0"); }
};

struct WiFiClass {
    void mode(int) {}
    void begin(const char *, const char *) {}
    void setAutoReconnect(bool) {}
    int status() { return 0; }
    IPAddress localIP() { return IPAddress(); }
    String SSID() { return String(); }
    int32_t RSSI() { return 0; }
};
extern WiFiClass WiFi;

class WiFiClient {
public:
    virtual ~WiFiClient() {}
    bool connect(const char *, uint16_t, int = 0) { return false; }
    bool connected() { return false; }
    void stop() {}
    void setNoDelay(bool) {}
    size_t write(const uint8_t *, size_t) { return 0; }
    int available() { return 0; }
    int read() { return -1; }
};
