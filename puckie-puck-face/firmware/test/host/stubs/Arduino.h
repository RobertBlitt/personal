// Minimal Arduino/ESP32 stub for host syntax-checking of firmware modules.
// Only declares what the Puckie Puck Face sources actually touch. This is a
// build-check artifact, not part of the firmware.
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>
#include <string>

// ---- basic Arduino API ----
inline uint32_t millis() { return 0; }
inline void delay(uint32_t) {}
inline void delayMicroseconds(uint32_t) {}

#define HIGH 1
#define LOW 0
#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2

inline void pinMode(int, int) {}
inline int digitalRead(int) { return 0; }
inline void digitalWrite(int, int) {}

inline void ledcSetup(int, int, int) {}
inline void ledcAttachPin(int, int) {}
inline void ledcWrite(int, int) {}

// strlcpy is BSD; glibc lacks it.
inline size_t strlcpy(char *dst, const char *src, size_t size) {
    size_t len = strlen(src);
    if (size) {
        size_t n = len < size - 1 ? len : size - 1;
        memcpy(dst, src, n);
        dst[n] = '\0';
    }
    return len;
}

// ---- String (std::string-backed, minimal surface) ----
class String {
public:
    String() {}
    String(const char *s) : s_(s ? s : "") {}
    String(const std::string &s) : s_(s) {}
    String(int v) : s_(std::to_string(v)) {}
    String(unsigned v) : s_(std::to_string(v)) {}
    unsigned length() const { return (unsigned)s_.size(); }
    const char *c_str() const { return s_.c_str(); }
    int indexOf(const String &needle, int from = 0) const {
        auto p = s_.find(needle.s_, from);
        return p == std::string::npos ? -1 : (int)p;
    }
    int indexOf(const char *needle, int from = 0) const { return indexOf(String(needle), from); }
    String substring(int a, int b) const { return String(s_.substr(a, b - a)); }
    void trim() {
        while (!s_.empty() && isspace((unsigned char)s_.front())) s_.erase(s_.begin());
        while (!s_.empty() && isspace((unsigned char)s_.back())) s_.pop_back();
    }
    String operator+(const String &o) const { return String(s_ + o.s_); }
    String operator+(const char *o) const { return String(s_ + o); }
    bool operator==(const char *o) const { return s_ == o; }
private:
    std::string s_;
};
inline String operator+(const char *a, const String &b) { return String(a) + b; }

// ---- Serial ----
struct SerialStub {
    void begin(unsigned long) {}
    void println(const char * = "") {}
    void println(const String &) {}
    void print(const char *) {}
    template <typename... Args> void printf(const char *, Args...) {}
};
extern SerialStub Serial;

// ---- FreeRTOS surface ----
typedef void *TaskHandle_t;
typedef void *QueueHandle_t;
typedef void *SemaphoreHandle_t;
typedef int BaseType_t;
#define pdTRUE 1
#define pdFALSE 0
#define portMAX_DELAY 0xffffffffUL
inline uint32_t pdMS_TO_TICKS(uint32_t ms) { return ms; }
inline void vTaskDelay(uint32_t) {}
inline BaseType_t xTaskCreatePinnedToCore(void (*)(void *), const char *,
                                          uint32_t, void *, int, TaskHandle_t *, int) {
    return pdTRUE;
}
inline QueueHandle_t xQueueCreate(int, size_t) { return nullptr; }
inline BaseType_t xQueueSend(QueueHandle_t, const void *, uint32_t) { return pdTRUE; }
inline BaseType_t xQueueReceive(QueueHandle_t, void *, uint32_t) { return pdFALSE; }
inline SemaphoreHandle_t xSemaphoreCreateMutex() { return nullptr; }
inline BaseType_t xSemaphoreTake(SemaphoreHandle_t, uint32_t) { return pdTRUE; }
inline BaseType_t xSemaphoreGive(SemaphoreHandle_t) { return pdTRUE; }

// ---- heap_caps ----
#define MALLOC_CAP_SPIRAM 0
inline void *heap_caps_malloc(size_t n, int) { return malloc(n); }

// ---- SNTP/timezone helper from esp32-hal ----
inline void configTzTime(const char *, const char *, const char * = nullptr,
                         const char * = nullptr) {}
