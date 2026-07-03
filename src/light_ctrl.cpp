// light_ctrl.cpp
#include "light_ctrl.h"
#include "log.h"
#include "config.h"    // pins namespace

namespace light_ctrl {

static const char* TAG = "light";

static State s_state = { true, true, 0, 0, 0, 100, "" };  // boot default: white on

static bool     s_identify_active = false;
static uint32_t s_identify_until  = 0;
static State    s_pre_identify;

static uint8_t  s_uv_level = 0;

// Fade engine — mirrors the PxB DMX adapter's software fade: chained ticks
// at ~30 Hz interpolating brightness and r/g/b from the live state to a
// target. `white` is applied instantly (digital channel, can't be dimmed).
static const uint32_t FADE_TICK_MS = 33;  // ~30 Hz, matches PxB dmx.js FADE_HZ

static bool     s_fade_active           = false;
static bool     s_fade_completed_flag   = false;
static uint32_t s_fade_start_ms         = 0;
static uint32_t s_fade_duration_ms      = 0;
static uint32_t s_fade_last_tick_ms     = 0;
static uint8_t  s_fade_from_r, s_fade_from_g, s_fade_from_b, s_fade_from_brightness;
static uint8_t  s_fade_to_r,   s_fade_to_g,   s_fade_to_b,   s_fade_to_brightness;
static bool     s_fade_to_on;

// Clamp helper
static uint8_t clamp8(int v) {
    if (v < 0)   return 0;
    if (v > 255) return 255;
    return (uint8_t)v;
}

// Cancel any in-progress fade. Called at the top of every immediate setter
// so a direct command always interrupts a running fade, matching the PxB
// DMX adapter's `_cancelFade()` pattern.
static void cancel_fade() {
    s_fade_active = false;
}

// Apply the logical state to hardware.
//
// MOSFETs are IRLB8721 (N-channel, enhancement mode).
// Gate HIGH → MOSFET ON → LED on. Active-HIGH drive is correct.
//
// Boot/failsafe note: GPIO2 (D4, UV channel) is held HIGH by the ESP8266
// bootstrap circuit (required for normal boot mode). This means the UV strip
// fires briefly during the boot ROM phase — unavoidable but typically
// imperceptible. begin() drives it to 0 immediately on startup.
// White (D1/GPIO5) has no bootstrap constraint and starts low.
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

// UV channel is driven at its stored level at all times, independent of
// the main on/off/brightness state.
static void apply_uv_hw() {
    analogWrite(pins::UV, s_uv_level);
}

void begin() {
    // Use 8-bit PWM range (matches r/g/b byte values directly).
    analogWriteRange(255);
    analogWriteFreq(1000);  // 1 kHz — safe for most MOSFETs/transistors

    pinMode(pins::WHITE, OUTPUT);
    pinMode(pins::RED,   OUTPUT);
    pinMode(pins::GREEN, OUTPUT);
    pinMode(pins::BLUE,  OUTPUT);
    pinMode(pins::UV,    OUTPUT);

    // Start with everything off.
    apply_hw();
    apply_uv_hw();
    pxlog::info(TAG, "begin: all channels off");
}

void set_on(bool on) {
    cancel_fade();
    s_state.on = on;
    apply_hw();
    pxlog::info(TAG, "on=%d", (int)on);
}

void set_white(bool white) {
    cancel_fade();
    s_state.white = white;
    if (!s_state.on) s_state.on = white;  // turning white on also turns device on
    apply_hw();
}

void set_rgb(uint8_t r, uint8_t g, uint8_t b) {
    cancel_fade();
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
    cancel_fade();
    if (pct > 100) pct = 100;
    s_state.brightness = pct;
    apply_hw();
    pxlog::info(TAG, "brightness=%u%%", (unsigned)pct);
}

void fade_to(bool on, bool white, uint8_t r, uint8_t g, uint8_t b, uint8_t brightness, uint32_t duration_ms) {
    // White is a digital channel — apply instantly, it cannot be smoothly faded.
    s_state.white = white;
    s_state.scene = "";

    if (duration_ms == 0) {
        cancel_fade();
        s_state.on         = on;
        s_state.r          = r;
        s_state.g          = g;
        s_state.b          = b;
        s_state.brightness = brightness;
        apply_hw();
        pxlog::info(TAG, "fade: duration=0, applied immediately");
        return;
    }

    // Start from the current *live* values — if a fade was already in
    // progress, s_state reflects wherever that transition currently is,
    // so the new fade picks up from there rather than finishing the old one.
    // When currently off, the true visual brightness is 0 regardless of the
    // preserved brightness field (apply_hw() zeroes everything while off).
    s_fade_from_r          = s_state.r;
    s_fade_from_g          = s_state.g;
    s_fade_from_b          = s_state.b;
    s_fade_from_brightness = s_state.on ? s_state.brightness : 0;
    s_fade_to_r            = r;
    s_fade_to_g            = g;
    s_fade_to_b            = b;
    s_fade_to_brightness   = brightness;
    s_fade_to_on           = on;

    // Stay "on" for the duration of the ramp so dimming is visible; the
    // final tick clears it if the target brightness is 0.
    s_state.on = true;

    s_fade_active        = true;
    s_fade_start_ms      = millis();
    s_fade_duration_ms   = duration_ms;
    s_fade_last_tick_ms  = 0;  // force an immediate first tick
    pxlog::info(TAG, "fade: start duration_ms=%lu", (unsigned long)duration_ms);
}

bool fading() { return s_fade_active; }

bool take_fade_completed() {
    bool v = s_fade_completed_flag;
    s_fade_completed_flag = false;
    return v;
}

void restore_brightness_field(uint8_t pct) {
    if (pct > 100) pct = 100;
    s_state.brightness = pct;
}

// Advance the active fade by one tick, if enough time has passed. Cheap to
// call every loop() iteration — gated on FADE_TICK_MS internally.
static void service_fade() {
    if (!s_fade_active) return;

    uint32_t now = millis();
    if (now - s_fade_last_tick_ms < FADE_TICK_MS) return;
    s_fade_last_tick_ms = now;

    float t = (float)(now - s_fade_start_ms) / (float)s_fade_duration_ms;
    if (t >= 1.0f) t = 1.0f;

    s_state.r = clamp8((int)(s_fade_from_r + (s_fade_to_r - s_fade_from_r) * t + 0.5f));
    s_state.g = clamp8((int)(s_fade_from_g + (s_fade_to_g - s_fade_from_g) * t + 0.5f));
    s_state.b = clamp8((int)(s_fade_from_b + (s_fade_to_b - s_fade_from_b) * t + 0.5f));
    int bri = (int)(s_fade_from_brightness + (s_fade_to_brightness - s_fade_from_brightness) * t + 0.5f);
    if (bri < 0)   bri = 0;
    if (bri > 100) bri = 100;
    s_state.brightness = (uint8_t)bri;
    apply_hw();

    if (t >= 1.0f) {
        s_fade_active = false;
        s_state.on = s_fade_to_on && (s_state.brightness > 0);
        apply_hw();
        s_fade_completed_flag = true;
        pxlog::info(TAG, "fade: complete");
    }
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
    { "white",       true,  true,    0,   0, 255, 100 },
    { "normal",      true,  true,    0,   0,   0, 100 },
    { "brightWhite", true,  true,  255, 255, 255, 100 },
    { "softWhite",   true,  true,  128,   0,   0, 100 }, 
    { "warmWhite",   true,  true,  255,   0,   0, 100 }, 
    { "dim",         true,  false, 255, 255, 255, 100 }, 
    { "coolWhite",   true,  true,    0,   0, 255, 100 }, 
    { "nightLight",  true,  false,  96,  32,  16, 100 },
    { "reading",     true,  false, 255, 255, 128, 100 },
    { "relax",       true,  false, 255, 128,   0, 100 },
    { "party",       true,  false, 255,   0, 255, 100 },
    { "romantic",    true,  false, 255,   0,   0, 100 },
    { "sunset",      true,  false, 255,   0,   0, 100 },
    { "sunrise",     true,  false,   0,   0,   0,   0 },
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

void apply_scene(const String& name, uint32_t duration_ms) {
    for (size_t i = 0; i < k_scene_count; ++i) {
        if (name.equalsIgnoreCase(k_scenes[i].name)) {
            const SceneEntry& sc = k_scenes[i];
            if (duration_ms > 0) {
                fade_to(sc.on, sc.white, sc.r, sc.g, sc.b, sc.brightness, duration_ms);
            } else {
                cancel_fade();
                s_state.on         = sc.on;
                s_state.white      = sc.white;
                s_state.r          = sc.r;
                s_state.g          = sc.g;
                s_state.b          = sc.b;
                s_state.brightness = sc.brightness;
                apply_hw();
            }
            // fade_to() clears s_state.scene (it treats the target as a
            // plain colour); restore the scene name so state reporting
            // still reflects it, faded or not.
            s_state.scene = name;
            pxlog::info(TAG, "scene=%s duration_ms=%lu", name.c_str(), (unsigned long)duration_ms);
            return;
        }
    }
    pxlog::warn(TAG, "unknown scene: %s", name.c_str());
}

void identify() {
    if (s_identify_active) return;
    cancel_fade();
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
    service_fade();

    if (s_identify_active && (int32_t)(millis() - s_identify_until) >= 0) {
        s_identify_active = false;
        s_state = s_pre_identify;
        apply_hw();
        pxlog::info(TAG, "identify ended");
    }
}

void set_uv(uint8_t level) {
    s_uv_level = level;
    apply_uv_hw();
    pxlog::info(TAG, "uv=%u", (unsigned)level);
}

uint8_t uv_level() { return s_uv_level; }

const State& state() { return s_state; }

} // namespace light_ctrl
