// light_ctrl.h — 4-channel light output controller.
//
// Channels (LoLin NodeMCU V3, see docs/pin-mapping.md):
//   D1 / GPIO5  — White  (digital on/off, active-HIGH)
//   D4 / GPIO2  — UV     (software PWM 0-255, fully independent channel)
//   D5 / GPIO14 — Green  (software PWM 0-255)
//   D6 / GPIO12 — Red    (software PWM 0-255)
//   D7 / GPIO13 — Blue   (software PWM 0-255)
#pragma once
#include <Arduino.h>

namespace light_ctrl {

struct State {
    bool    on;
    bool    white;
    uint8_t r, g, b;
    uint8_t brightness;   // 0-100 overall scaler
    String  scene;        // last named scene, or ""
};

// Must be called in setup() before any other function.
void begin();

// High-level control — each call immediately updates hardware.
void set_on(bool on);
void set_white(bool white);
void set_rgb(uint8_t r, uint8_t g, uint8_t b);
void set_brightness(uint8_t pct);      // 0-100
void apply_scene(const String& name);  // named scene from PxB table

// Brief flash for identification (non-blocking; finishes on next tick).
void identify();

// Must be called in loop() to service the identify timer.
void tick();

// UV channel — fully independent of on/off/brightness/scenes.
void set_uv(uint8_t level);   // 0 = off, 255 = full; not affected by brightness
uint8_t uv_level();           // current UV level

// Read the current logical state.
const State& state();

} // namespace light_ctrl
