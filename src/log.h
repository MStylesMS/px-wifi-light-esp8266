// log.h — thin Serial-based logging shim with a small ring buffer.
#pragma once

#include <Arduino.h>

namespace pxlog {

void begin(uint32_t baud = 115200);

void info(const char* tag, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
void warn(const char* tag, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
void err (const char* tag, const char* fmt, ...) __attribute__((format(printf, 2, 3)));

typedef bool (*LineCb)(const char* line, void* user);
void each_line(LineCb cb, void* user);

} // namespace pxlog
