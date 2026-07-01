// light_ctrl.cpp
#include "light_ctrl.h"
#include "log.h"
#include "config.h"    // pins namespace

namespace light_ctrl {

static const char* TAG = "light";

static State s_state = { false, false, 0, 0, 0, 100, "" };

static bool     s_identify_active = false;
static uint32_t s_identify_until  = 0;
static State    s_pre_identify;

// Clamp helper
static uint8_t clamp8(int v) {
    if (v < 0)   return 0;
    if (v > 255) return 255;
    return (uint8_t)v;
}

// Apply the logical state to hardware.
//
// MOSFETs are IRLB8721 (N-channel, enhancement mode).
// Gate HIGH → MOSFET ON → LED on. Active-HIGH drive is correct.
//
// Boot/failsafe note: GPIO2 (White) is held HIGH by the ESP8266 bootstrap
// circuit (required for normal boot mode). With an N-channel MOSFET this
// means the white channel is ON during the boot ROM phase — intentional.
// If the firmware ever fails to start, white stays on as the hardware-level
// default. Once this function is called from begin(), the firmware takes
// control and can drive it to whatever state it needs.
static void apply_hw() {
    if (!s_state.on) {
        digitalWrite(pins::WHITE, LOW);
        analogWrite(pins::RED,   0);
        analogWrite(pins::GREEN, 0);
        analogWrite(pins::BLUE,  0);
        return;
    }

    float scale = s_state.brightness / 100.0f;
    digitalWrite(pins::WHITE, s_state.white ? HIGH : LOW);
    analogWrite(pins::RED,   clamp8((int)(s_state.r * scale)));
    analogWrite(pins::GREEN, clamp8((int)(s_state.g * scale)));
    analogWrite(pins::BLUE,  clamp8((int)(s_state.b * scale)));
}

void begin() {
    // Use 8-bit PWM range (matches r/g/b byte values directly).
    analogWriteRange(255);
    analogWriteFreq(1000);  // 1 kHz — safe for most MOSFETs/transistors

    pinMode(pins::WHITE, OUTPUT);
    pinMode(pins::RED,   OUTPUT);
    pinMode(pins::GREEN, OUTPUT);
    pinMode(pins::BLUE,  OUTPUT);

    // Start with everything off.
    apply_hw();
    pxlog::info(TAG, "begin: all channels off");
}

void set_on(bool on) {
    s_state.on = on;
    apply_hw();
    pxlog::info(TAG, "on=%d", (int)on);
}

void set_white(bool white) {
    s_state.white = white;
    if (!s_state.on) s_state.on = white;  // turning white on also turns device on
    apply_hw();
}

void set_rgb(uint8_t r, uint8_t g, uint8_t b) {
    s_state.r = r;
    s_state.g = g;
    s_state.b = b;
    s_state.white = false;  // explicit colour overrides white channel
    // Any non-zero RGB implicitly turns the device on.
    if (r || g || b) s_state.on = true;
    s_state.scene = "";
    apply_hw();
    pxlog::info(TAG, "rgb r=%u g=%u b=%u", (unsigned)r, (unsigned)g, (unsigned)b);
}

void set_brightness(uint8_t pct) {
    if (pct > 100) pct = 100;
    s_state.brightness = pct;
    apply_hw();
    pxlog::info(TAG, "brightness=%u%%", (unsigned)pct);
}

struct SceneEntry {
    const char* name;
    bool        on;
    bool        white;
    uint8_t     r, g, b;
    uint8_t     brightness;
};

// Scene table aligned with PxB scene names (docs/api.md).
static const SceneEntry k_scenes[] = {
    { "off",         false, false,   0,   0,   0, 100 },
    { "white",       true,  true,    0,   0,   0, 100 },
    { "normal",      true,  true,    0,   0,   0, 100 },
    { "brightWhite", true,  true,    0,   0,   0, 100 },
    { "softWhite",   true,  true,    0,   0,   0,  50 },
    { "warmWhite",   true,  true,   32,   8,   0, 100 },  // white + warm RGB tint
    { "dim",         true,  true,    0,   0,   0,  30 },
    { "coolWhite",   true,  false,  80,  80, 255, 100 },  // cool-blue RGB only
    { "red",         true,  false, 255,   0,   0, 100 },
    { "green",       true,  false,   0, 255,   0, 100 },
    { "blue",        true,  false,   0,   0, 255, 100 },
    { "yellow",      true,  false, 255, 255,   0, 100 },
    { "orange",      true,  false, 255, 128,   0, 100 },
    { "purple",      true,  false, 128,   0, 255, 100 },
    { "pink",        true,  false, 255,  64, 128, 100 },
    { "cyan",        true,  false,   0, 255, 255, 100 },
    { "magenta",     true,  false, 255,   0, 255, 100 },
};
static const size_t k_scene_count = sizeof(k_scenes) / sizeof(k_scenes[0]);

void apply_scene(const String& name) {
    for (size_t i = 0; i < k_scene_count; ++i) {
        if (name.equalsIgnoreCase(k_scenes[i].name)) {
            const SceneEntry& sc = k_scenes[i];
            s_state.on         = sc.on;
            s_state.white      = sc.white;
            s_state.r          = sc.r;
            s_state.g          = sc.g;
            s_state.b          = sc.b;
            s_state.brightness = sc.brightness;
            s_state.scene      = name;
            apply_hw();
            pxlog::info(TAG, "scene=%s", name.c_str());
            return;
        }
    }
    pxlog::warn(TAG, "unknown scene: %s", name.c_str());
}

void identify() {
    if (s_identify_active) return;
    s_pre_identify = s_state;
    s_identify_active = true;
    s_identify_until  = millis() + 2000;
    // Flash white at full brightness for 2 s.
    s_state.on     = true;
    s_state.white  = true;
    s_state.r = s_state.g = s_state.b = 255;
    s_state.brightness = 100;
    apply_hw();
    pxlog::info(TAG, "identify started");
}

void tick() {
    if (s_identify_active && (int32_t)(millis() - s_identify_until) >= 0) {
        s_identify_active = false;
        s_state = s_pre_identify;
        apply_hw();
        pxlog::info(TAG, "identify ended");
    }
}

const State& state() { return s_state; }

} // namespace light_ctrl
