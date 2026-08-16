// light_ctrl.h -- 5-channel light output controller (W + RGB + UV).
//
// Channels (LoLin NodeMCU V3, see docs/pin-mapping.md):
//   D1 / GPIO5  -- White  (digital on/off, active-HIGH)
//   D2 / GPIO4  -- UV     (software PWM 0-255; faded with RGB; zeroed by off/allOff
//                         and by every scene except "uv")
//   D5 / GPIO14 -- Green  (software PWM 0-255)
//   D6 / GPIO12 -- Red    (software PWM 0-255)
//   D7 / GPIO13 -- Blue   (software PWM 0-255)
#pragma once
#include <Arduino.h>

namespace light_ctrl {

struct State {
    bool    on;
    bool    white;
    uint8_t r, g, b;
    uint8_t brightness;   // 0-100 overall scaler (RGB only)
    String  scene;        // last named scene, or ""
};

// Snapshot of channel targets preserved across off/allOff for the next on/allOn.
struct Preserved {
    bool    valid;
    bool    white;
    uint8_t r, g, b;
    uint8_t brightness;
    uint8_t uv;
};

void begin();

void set_on(bool on);
void set_white(bool white);
void set_rgb(uint8_t r, uint8_t g, uint8_t b);
void set_brightness(uint8_t pct);

// Apply a named scene. If duration_ms > 0, r/g/b, brightness, and UV fade at ~30 Hz.
// White applies instantly. Non-uv scenes force UV=0; "uv" sets UV=255.
void apply_scene(const String& name, uint32_t duration_ms = 0);

void identify();
void tick();

// Fade live output to target. Brightness, r/g/b, UV interpolate at ~30 Hz.
// white applies instantly. on stays true during ramp; cleared at end if target off.
void fade_to(bool on, bool white, uint8_t r, uint8_t g, uint8_t b,
             uint8_t brightness, uint8_t uv, uint32_t duration_ms);

bool fading();
bool take_fade_completed();
void restore_brightness_field(uint8_t pct);

void set_uv(uint8_t level);
uint8_t uv_level();

void capture_preserve_from_current();
const Preserved& preserved();
void restore_preserved_to_targets(bool& out_white, uint8_t& out_r, uint8_t& out_g,
                                  uint8_t& out_b, uint8_t& out_bri, uint8_t& out_uv);
void set_scene_name(const String& name);
const State& state();

} // namespace light_ctrl
