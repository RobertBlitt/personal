#pragma once
#include "Arduino.h"
#include "WiFi.h"

#define HTTP_CODE_OK 200

class HTTPClient {
public:
    bool begin(WiFiClient &, const char *) { return false; }
    void setTimeout(uint32_t) {}
    void setConnectTimeout(uint32_t) {}
    void setUserAgent(const char *) {}
    int GET() { return -1; }
    String getString() { return String(); }
    void end() {}
};
