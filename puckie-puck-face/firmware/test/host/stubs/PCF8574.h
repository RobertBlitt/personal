// Stub of the xreef PCF8574 library surface used by input.cpp/display.cpp.
#pragma once
#include "Arduino.h"

#define P0 0
#define P1 1
#define P2 2
#define P3 3
#define P4 4
#define P5 5
#define P6 6
#define P7 7

class PCF8574 {
public:
    explicit PCF8574(uint8_t) {}
    void pinMode(uint8_t, uint8_t) {}
    bool begin() { return true; }
    uint8_t digitalRead(uint8_t, bool = false) { return 1; }
    void digitalWrite(uint8_t, uint8_t) {}
};
