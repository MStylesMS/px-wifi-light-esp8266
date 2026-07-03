// light_ctrl.h — 4-channel light output controller.
//
// Channels (LoLin NodeMCU V3, see docs/pin-mapping.md):
//   D1 / GPIO5  — White  (digital on/off, active-HIGH)
//   D2 / GPIO4  — UV     (software PWM 0-255, fully independent channel)
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

// High-level control — each call immediately updates hardware and cancels
// any fade in progress.
void set_on(bool on);
void set_white(bool white);
void set_rgb(uint8_t r, uint8_t g, uint8_t b);
void set_brightness(uint8_t pct);      // 0-100

// Apply a named scene from the PxB table. If duration_ms > 0, the r/g/b and
// brightness transition is faded (same engine as fade_to()); white and "on"
// still apply instantly. duration_ms == 0 (default) applies the scene
// immediately, matching the previous behaviour.
void apply_scene(const String& name, uint32_t duration_ms = 0);  // named scene from PxB table

// Brief flash for identification (non-blocking; finishes on next tick).
void identify();

// Must be called in loop() to service the identify timer and any active fade.
void tick();

// Fade from the current live output to the given target over duration_ms.
// Only brightness and r/g/b are interpolated (30 Hz ticks, matching the PxB
// DMX adapter's software fade); `white` applies instantly since it's a
// digital on/off channel and cannot be smoothly dimmed. `on` stays true for
// the duration of the fade so the ramp is visible, and is only cleared once
// the target brightness reaches 0.
//
// Calling this while a fade is already in progress cancels the old fade and
// starts the new one from the current (mid-transition) live values — it
// never runs the original transition to completion first.
//
// duration_ms == 0 applies the target immediately (no fade).
void fade_to(bool on, bool white, uint8_t r, uint8_t g, uint8_t b, uint8_t brightness, uint32_t duration_ms);

// True while a fade started by fade_to() is still in progress.
bool fading();

// Returns true exactly once, the first time it's polled after a fade
// finishes, then clears — used by commands::tick() to publish final state.
bool take_fade_completed();

// Overwrite the persisted brightness field without touching hardware, the
// on/white/rgb state, or any active fade. Used by commands::tick() to
// restore the pre-fade brightness after a faded "off" completes, preserving
// the documented "off preserves channel values for next on" contract even
// though the fade visually ramped brightness down to 0 to reach it.
void restore_brightness_field(uint8_t pct);

// UV channel — fully independent of on/off/brightness/scenes.
void set_uv(uint8_t level);   // 0 = off, 255 = full; not affected by brightness
uint8_t uv_level();           // current UV level

// Read the current logical state.
const State& state();

} // namespace light_ctrl
