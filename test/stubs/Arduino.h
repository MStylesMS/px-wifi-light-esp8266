#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>

using String = std::string;
using byte = unsigned char;

struct EspClass {
    uint32_t freeHeap = 0;
    uint32_t getFreeHeap() const { return freeHeap; }
    void restart() {}
};

extern EspClass ESP;

unsigned long millis();

inline void delay(unsigned long) {}
inline void yield() {}
inline void pinMode(uint8_t, uint8_t) {}
inline int  digitalRead(uint8_t) { return 1; }

static const uint8_t INPUT_PULLUP = 2;
static const uint8_t LOW          = 0;
