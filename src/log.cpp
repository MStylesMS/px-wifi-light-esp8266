// log.cpp
#include "log.h"
#include <stdarg.h>
#include <stdio.h>

namespace pxlog {

static const size_t LINE_MAX = 160;
static const size_t RING_SZ  = 16;

struct Line { char buf[LINE_MAX]; };
static Line   s_ring[RING_SZ];
static size_t s_head  = 0;
static size_t s_count = 0;

static void push_line(const char* level, const char* tag, const char* msg) {
    Line& l = s_ring[s_head];
    snprintf(l.buf, LINE_MAX, "%lu %s [%s] %s",
             (unsigned long)millis(), level, tag, msg);
    s_head = (s_head + 1) % RING_SZ;
    if (s_count < RING_SZ) s_count++;
}

static void emit(const char* level, const char* tag, const char* fmt, va_list ap) {
    char msg[LINE_MAX];
    vsnprintf(msg, sizeof(msg), fmt, ap);
    Serial.printf("%lu %s [%s] %s\r\n",
                  (unsigned long)millis(), level, tag, msg);
    push_line(level, tag, msg);
}

void begin(uint32_t baud) {
    Serial.begin(baud);
    delay(50);
    Serial.printf("\r\n--- px-wifi-light-esp8266 boot ---\r\n");
}

void info(const char* tag, const char* fmt, ...) {
    va_list ap; va_start(ap, fmt); emit("INFO", tag, fmt, ap); va_end(ap);
}
void warn(const char* tag, const char* fmt, ...) {
    va_list ap; va_start(ap, fmt); emit("WARN", tag, fmt, ap); va_end(ap);
}
void err(const char* tag, const char* fmt, ...) {
    va_list ap; va_start(ap, fmt); emit("ERR ", tag, fmt, ap); va_end(ap);
}

void each_line(LineCb cb, void* user) {
    if (s_count == 0) return;
    size_t start = (s_head + RING_SZ - s_count) % RING_SZ;
    for (size_t i = 0; i < s_count; ++i) {
        size_t idx = (start + i) % RING_SZ;
        if (!cb(s_ring[idx].buf, user)) return;
    }
}

} // namespace pxlog
