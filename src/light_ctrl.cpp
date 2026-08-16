// light_ctrl.cpp
#include "light_ctrl.h"
#include "log.h"
#include "config.h"    // pins namespace

namespace light_ctrl {

static const char* TAG = "light";

// Boot default: white on, UV off (logical). Hardware is zeroed in begin().
static State s_state = { true, true, 0, 0, 0, 100, "" };

static bool     s_identify_active = false;
static uint32_t s_identify_until  = 0;
static State    s_pre_identify;
static uint8_t  s_pre_identify_uv = 0;

static uint8_t  s_uv_level = 0;

static Preserved s_preserved = { false, true, 0, 0, 0, 100, 0 };

// Fade engine -- ~30 Hz interpolating brightness, r/g/b, and UV.
// white is applied instantly (digital channel, cannot be dimmed).
static const uint32_t FADE_TICK_MS = 33;

static bool     s_fade_active           = false;
static bool     s_fade_completed_flag   = false;
static uint32_t s_fade_start_ms         = 0;
static uint32_t s_fade_duration_ms      = 0;
static uint32_t s_fade_last_tick_ms     = 0;
static uint8_t  s_fade_from_r, s_fade_from_g, s_fade_from_b, s_fade_from_brightness, s_fade_from_uv;
static uint8_t  s_fade_to_r,   s_fade_to_g,   s_fade_to_b,   s_fade_to_brightness,   s_fade_to_uv;
static bool     s_fade_to_on;

static uint8_t clamp8(int v) {
    if (v < 0)   return 0;
    if (v > 255) return 255;
    return (uint8_t)v;
}

static void cancel_fade() {
    s_fade_active = false;
}

// Apply white + RGB. When off, force those outputs to zero. UV is separate.
static void apply_rgb_hw() {
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

static void apply_uv_hw() {
    analogWrite(pins::UV, s_uv_level);
}

static void apply_all_hw() {
    apply_rgb_hw();
    apply_uv_hw();
}

void begin() {
    analogWriteRange(255);
    analogWriteFreq(1000);

    pinMode(pins::WHITE, OUTPUT);
    pinMode(pins::RED,   OUTPUT);
    pinMode(pins::GREEN, OUTPUT);
    pinMode(pins::BLUE,  OUTPUT);
    pinMode(pins::UV,    OUTPUT);

    // Hardware starts dark; logical default remains white-on for first allOn.
    s_state.on = false;
    s_state.white = true;
    s_state.r = s_state.g = s_state.b = 0;
    s_state.brightness = 100;
    s_state.scene = "";
    s_uv_level = 0;
    apply_all_hw();
    pxlog::info(TAG, "begin: all channels off");
}

void set_on(bool on) {
    cancel_fade();
    s_state.on = on;
    if (!on) {
        // Master off also zeros UV output (level field stays until set_uv/off path).
        s_uv_level = 0;
        s_state.scene = "off";
    }
    apply_all_hw();
    pxlog::info(TAG, "on=%d uv=%u", (int)on, (unsigned)s_uv_level);
}

void set_white(bool white) {
    cancel_fade();
    s_state.white = white;
    if (white) s_state.on = true;
    s_state.scene = "";
    apply_rgb_hw();
}

void set_rgb(uint8_t r, uint8_t g, uint8_t b) {
    cancel_fade();
    s_state.r = r;
    s_state.g = g;
    s_state.b = b;
    s_state.white = false;
    if (r || g || b) s_state.on = true;
    s_state.scene = "";
    apply_rgb_hw();
    pxlog::info(TAG, "rgb r=%u g=%u b=%u", (unsigned)r, (unsigned)g, (unsigned)b);
}

void set_brightness(uint8_t pct) {
    cancel_fade();
    if (pct > 100) pct = 100;
    s_state.brightness = pct;
    apply_rgb_hw();
    pxlog::info(TAG, "brightness=%u%%", (unsigned)pct);
}

void fade_to(bool on, bool white, uint8_t r, uint8_t g, uint8_t b,
             uint8_t brightness, uint8_t uv, uint32_t duration_ms) {
    s_state.white = white;
    s_state.scene = "";

    if (duration_ms == 0) {
        cancel_fade();
        s_state.on         = on;
        s_state.r          = r;
        s_state.g          = g;
        s_state.b          = b;
        s_state.brightness = brightness;
        s_uv_level         = uv;
        if (!on) s_state.scene = "off";
        apply_all_hw();
        pxlog::info(TAG, "fade: duration=0, applied immediately uv=%u", (unsigned)uv);
        return;
    }

    s_fade_from_r          = s_state.r;
    s_fade_from_g          = s_state.g;
    s_fade_from_b          = s_state.b;
    s_fade_from_brightness = s_state.on ? s_state.brightness : 0;
    s_fade_from_uv         = s_uv_level;
    s_fade_to_r            = r;
    s_fade_to_g            = g;
    s_fade_to_b            = b;
    s_fade_to_brightness   = brightness;
    s_fade_to_uv           = uv;
    s_fade_to_on           = on;

    // Stay on for RGB ramp visibility. UV still ramps via s_uv_level.
    s_state.on = true;

    s_fade_active        = true;
    s_fade_start_ms      = millis();
    s_fade_duration_ms   = duration_ms;
    s_fade_last_tick_ms  = 0;
    pxlog::info(TAG, "fade: start duration_ms=%lu to_uv=%u",
                (unsigned long)duration_ms, (unsigned)uv);
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
    s_uv_level = clamp8((int)(s_fade_from_uv + (s_fade_to_uv - s_fade_from_uv) * t + 0.5f));
    apply_all_hw();

    if (t >= 1.0f) {
        s_fade_active = false;
        s_state.on = s_fade_to_on && (s_state.brightness > 0 || s_state.white ||
                                      s_state.r || s_state.g || s_state.b);
        // Honour explicit off target even if residual channel fields remain.
        if (!s_fade_to_on) {
            s_state.on = false;
            s_uv_level = s_fade_to_uv;  // normally 0
            s_state.scene = "off";
        }
        apply_all_hw();
        s_fade_completed_flag = true;
        pxlog::info(TAG, "fade: complete on=%d uv=%u", (int)s_state.on, (unsigned)s_uv_level);
    }
}

struct SceneEntry {
    const char* name;
    bool        on;
    bool        white;
    uint8_t     r, g, b;
    uint8_t     brightness;
    uint8_t     uv;
};

// Scene table (0.5.2). UV=0 unless scene is "uv".
// Removed: normal, warmWhite, reading, relax, party, romantic, sunrise, sunset.
// dim renamed to moonlight (same RGB full @ 30% bri).
static const SceneEntry k_scenes[] = {
    { "white",       true,  true,    0,   0,   0, 100,   0 },
    { "brightWhite", true,  true,  255, 255, 255, 100,   0 },
    { "softWhite",   true,  true,  255,   0,   0, 100,   0 },
    { "moonlight",   true,  false, 255, 255, 255,  30,   0 },
    { "coolWhite",   true,  true,    0,   0, 255, 100,   0 },
    { "nightLight",  true,  false, 255, 128,   0,   8,   0 },
    { "red",         true,  false, 255,   0,   0, 100,   0 },
    { "green",       true,  false,   0, 255,   0, 100,   0 },
    { "blue",        true,  false,   0,   0, 255, 100,   0 },
    { "yellow",      true,  false, 255, 255,   0, 100,   0 },
    { "orange",      true,  false, 255, 128,   0, 100,   0 },
    { "purple",      true,  false, 128,   0, 255, 100,   0 },
    { "pink",        true,  false, 255,  64, 128, 100,   0 },
    { "cyan",        true,  false,   0, 255, 255, 100,   0 },
    { "magenta",     true,  false, 255,   0, 255, 100,   0 },
    { "uv",          true,  false,   0,   0,   0, 100, 255 },
    { "off",         false, false,   0,   0,   0, 100,   0 },
};
static const size_t k_scene_count = sizeof(k_scenes) / sizeof(k_scenes[0]);

void apply_scene(const String& name, uint32_t duration_ms) {
    for (size_t i = 0; i < k_scene_count; ++i) {
        if (name.equalsIgnoreCase(k_scenes[i].name)) {
            const SceneEntry& sc = k_scenes[i];
            if (duration_ms > 0) {
                fade_to(sc.on, sc.white, sc.r, sc.g, sc.b, sc.brightness, sc.uv, duration_ms);
            } else {
                cancel_fade();
                s_state.on         = sc.on;
                s_state.white      = sc.white;
                s_state.r          = sc.r;
                s_state.g          = sc.g;
                s_state.b          = sc.b;
                s_state.brightness = sc.brightness;
                s_uv_level         = sc.uv;
                apply_all_hw();
            }
            // fade_to clears scene; restore canonical lowercase id for state.
            s_state.scene = k_scenes[i].name;
            if (!sc.on) s_state.scene = "off";
            pxlog::info(TAG, "scene=%s uv=%u duration_ms=%lu",
                        k_scenes[i].name, (unsigned)sc.uv, (unsigned long)duration_ms);
            return;
        }
    }
    pxlog::warn(TAG, "unknown scene: %s", name.c_str());
}

void identify() {
    if (s_identify_active) return;
    cancel_fade();
    s_pre_identify = s_state;
    s_pre_identify_uv = s_uv_level;
    s_identify_active = true;
    s_identify_until  = millis() + 2000;
    s_state.on     = true;
    s_state.white  = true;
    s_state.r = s_state.g = s_state.b = 255;
    s_state.brightness = 100;
    // Leave UV as-is during identify flash of main channels.
    apply_rgb_hw();
    pxlog::info(TAG, "identify started");
}

void tick() {
    service_fade();

    if (s_identify_active && (int32_t)(millis() - s_identify_until) >= 0) {
        s_identify_active = false;
        s_state = s_pre_identify;
        s_uv_level = s_pre_identify_uv;
        apply_all_hw();
        pxlog::info(TAG, "identify ended");
    }
}

void set_uv(uint8_t level) {
    cancel_fade();
    s_uv_level = level;
    s_state.scene = "";
    apply_uv_hw();
    pxlog::info(TAG, "uv=%u", (unsigned)level);
}

uint8_t uv_level() { return s_uv_level; }

void capture_preserve_from_current() {
    bool meaningful = s_state.white || s_state.r || s_state.g || s_state.b ||
                      s_uv_level || (s_state.on && s_state.brightness > 0);
    if (!meaningful) {
        s_preserved.valid = true;
        s_preserved.white = true;
        s_preserved.r = s_preserved.g = s_preserved.b = 0;
        s_preserved.brightness = 100;
        s_preserved.uv = 0;
        return;
    }
    s_preserved.valid = true;
    s_preserved.white = s_state.white;
    s_preserved.r = s_state.r;
    s_preserved.g = s_state.g;
    s_preserved.b = s_state.b;
    s_preserved.brightness = s_state.brightness ? s_state.brightness : 100;
    s_preserved.uv = s_uv_level;
}

const Preserved& preserved() { return s_preserved; }

void restore_preserved_to_targets(bool& out_white, uint8_t& out_r, uint8_t& out_g,
                                  uint8_t& out_b, uint8_t& out_bri, uint8_t& out_uv) {
    if (s_preserved.valid) {
        out_white = s_preserved.white;
        out_r = s_preserved.r;
        out_g = s_preserved.g;
        out_b = s_preserved.b;
        out_bri = s_preserved.brightness;
        out_uv = s_preserved.uv;
        return;
    }
    // Default when nothing was ever set: white on, RGB off, UV off.
    out_white = true;
    out_r = out_g = out_b = 0;
    out_bri = 100;
    out_uv = 0;
}

void set_scene_name(const String& name) {
    s_state.scene = name;
}

const State& state() { return s_state; }

} // namespace light_ctrl
