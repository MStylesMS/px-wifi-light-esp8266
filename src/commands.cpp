// commands.cpp — MQTT / HTTP light command dispatch.
#include "commands.h"
#include "log.h"
#include "light_ctrl.h"
#include "mqtt_mgr.h"

#include <ArduinoJson.h>

namespace commands {

static const char* TAG = "cmd";
static cfg::Config* s_cfg = nullptr;

static bool s_restart_pending = false;
static uint32_t s_restart_at  = 0;

// Set when a faded "off" is issued; consumed once that fade completes to
// restore the pre-off brightness into the persisted state field (preserving
// "off keeps channel values for next on" even though the fade visually
// ramped brightness down to 0). -1 = no restore pending. Any new command
// supersedes a pending restore, same as light_ctrl's own fade cancellation.
static int16_t s_restore_brightness_after_fade = -1;

void begin(cfg::Config* c) { s_cfg = c; }

void tick() {
    // A fade started by a command has finished — restore any pending
    // preserved-brightness value, then publish the settled state.
    if (light_ctrl::take_fade_completed()) {
        if (s_restore_brightness_after_fade >= 0) {
            light_ctrl::restore_brightness_field((uint8_t)s_restore_brightness_after_fade);
            s_restore_brightness_after_fade = -1;
        }
        mqtt_mgr::publish_state();
    }

    if (s_restart_pending && (int32_t)(millis() - s_restart_at) >= 0) {
        pxlog::warn(TAG, "restarting now");
        ESP.restart();
    }
}

// Read the "fadeTime" field (seconds, float) and convert to ms.
// An explicit value (including 0) always wins. When the field is absent,
// falls back to the configured default_fade_time_s (0 = no default fade).
static uint32_t fade_ms_from(const JsonDocument& doc) {
    JsonVariantConst v = doc["fadeTime"];
    double ft = v.isNull() ? (s_cfg ? (double)s_cfg->default_fade_time_s : 0.0) : v.as<double>();
    if (ft <= 0.0) return 0;
    return (uint32_t)(ft * 1000.0 + 0.5);
}

static uint8_t clamp_pct(int v) {
    if (v < 0)   return 0;
    if (v > 100) return 100;
    return (uint8_t)v;
}

// Parse "#RRGGBB" hex colour string into r, g, b.
static bool parse_hex_color(const char* hex, uint8_t& r, uint8_t& g, uint8_t& b) {
    if (!hex) return false;
    const char* p = hex;
    if (*p == '#') ++p;
    if (strlen(p) != 6) return false;
    char buf[3] = {0,0,0};
    buf[0] = p[0]; buf[1] = p[1]; r = (uint8_t)strtol(buf, nullptr, 16);
    buf[0] = p[2]; buf[1] = p[3]; g = (uint8_t)strtol(buf, nullptr, 16);
    buf[0] = p[4]; buf[1] = p[5]; b = (uint8_t)strtol(buf, nullptr, 16);
    return true;
}

bool handle(const JsonDocument& doc) {
    const char* cmd = doc["command"] | "";
    if (!cmd || !cmd[0]) {
        pxlog::warn(TAG, "payload missing 'command' field");
        return false;
    }

    pxlog::info(TAG, "command: %s", cmd);

    // Any new command supersedes a pending post-fade brightness restore
    // from a previous faded "off" (only the off branch below re-arms it).
    s_restore_brightness_after_fade = -1;

    // --- on / allOn ---
    if (strcmp(cmd, "on") == 0 || strcmp(cmd, "allOn") == 0) {
        const light_ctrl::State& cur = light_ctrl::state();
        bool white = cur.white;
        if (!white && !cur.r && !cur.g && !cur.b) white = true;  // nothing set yet — default to white on
        uint32_t fade_ms = fade_ms_from(doc);

        if (fade_ms > 0) {
            uint8_t target_brightness = doc["brightness"].is<int>()
                ? clamp_pct(doc["brightness"].as<int>()) : 100;
            light_ctrl::fade_to(true, white, cur.r, cur.g, cur.b, target_brightness, fade_ms);
        } else {
            light_ctrl::set_on(true);
            if (white) light_ctrl::set_white(true);
        }
        mqtt_mgr::publish_state();
        return true;
    }

    // --- off / allOff ---
    if (strcmp(cmd, "off") == 0 || strcmp(cmd, "allOff") == 0) {
        uint32_t fade_ms = fade_ms_from(doc);
        if (fade_ms > 0) {
            const light_ctrl::State& cur = light_ctrl::state();
            // Remember the current brightness so it can be restored once the
            // fade-to-black finishes — "off" preserves channel values for
            // the next "on", even though the fade ramps brightness to 0.
            s_restore_brightness_after_fade = (int16_t)cur.brightness;
            light_ctrl::fade_to(false, cur.white, cur.r, cur.g, cur.b, 0, fade_ms);
        } else {
            light_ctrl::set_on(false);
        }
        mqtt_mgr::publish_state();
        return true;
    }

    // --- setColor ---
    if (strcmp(cmd, "setColor") == 0) {
        uint8_t r = 0, g = 0, b = 0;
        JsonVariantConst col = doc["color"];
        if (col.is<const char*>()) {
            if (!parse_hex_color(col.as<const char*>(), r, g, b)) {
                pxlog::warn(TAG, "setColor: invalid hex color");
                mqtt_mgr::publish_warning("LIGHT_CMD_INVALID", "invalid hex color", JsonVariantConst());
                return false;
            }
        } else if (col.is<JsonObjectConst>()) {
            r = col["r"] | 0;
            g = col["g"] | 0;
            b = col["b"] | 0;
        } else {
            pxlog::warn(TAG, "setColor: missing 'color' field");
            mqtt_mgr::publish_warning("LIGHT_CMD_INVALID", "missing color field", JsonVariantConst());
            return false;
        }

        uint32_t fade_ms = fade_ms_from(doc);
        if (fade_ms > 0) {
            uint8_t target_brightness = doc["brightness"].is<int>()
                ? clamp_pct(doc["brightness"].as<int>()) : light_ctrl::state().brightness;
            light_ctrl::fade_to(true, false, r, g, b, target_brightness, fade_ms);
        } else {
            // Turn off white when setting an explicit colour.
            light_ctrl::set_white(false);
            light_ctrl::set_rgb(r, g, b);
            if (doc["brightness"].is<int>())
                light_ctrl::set_brightness((uint8_t)(doc["brightness"].as<int>()));
        }
        mqtt_mgr::publish_state();
        return true;
    }

    // --- setBrightness ---
    if (strcmp(cmd, "setBrightness") == 0) {
        if (!doc["brightness"].is<int>()) {
            mqtt_mgr::publish_warning("LIGHT_CMD_INVALID", "missing brightness field", JsonVariantConst());
            return false;
        }
        uint8_t target = clamp_pct(doc["brightness"].as<int>());
        uint32_t fade_ms = fade_ms_from(doc);
        if (fade_ms > 0) {
            const light_ctrl::State& cur = light_ctrl::state();
            light_ctrl::fade_to(true, cur.white, cur.r, cur.g, cur.b, target, fade_ms);
        } else {
            light_ctrl::set_brightness(target);
            if (!light_ctrl::state().on) light_ctrl::set_on(true);
        }
        mqtt_mgr::publish_state();
        return true;
    }

    // --- fade --- (generic: ramp brightness and/or color to a target over fadeTime seconds)
    if (strcmp(cmd, "fade") == 0) {
        const light_ctrl::State& cur = light_ctrl::state();
        uint8_t r = cur.r, g = cur.g, b = cur.b;
        bool white = cur.white;

        JsonVariantConst col = doc["color"];
        if (!col.isNull()) {
            if (col.is<const char*>()) {
                if (!parse_hex_color(col.as<const char*>(), r, g, b)) {
                    pxlog::warn(TAG, "fade: invalid hex color");
                    mqtt_mgr::publish_warning("LIGHT_CMD_INVALID", "invalid hex color", JsonVariantConst());
                    return false;
                }
            } else if (col.is<JsonObjectConst>()) {
                r = col["r"] | 0;
                g = col["g"] | 0;
                b = col["b"] | 0;
            }
            white = false;
        }

        uint8_t brightness = doc["brightness"].is<int>() ? clamp_pct(doc["brightness"].as<int>()) : cur.brightness;
        bool on = brightness > 0;
        uint32_t fade_ms = fade_ms_from(doc);

        if (fade_ms > 0) {
            light_ctrl::fade_to(on, white, r, g, b, brightness, fade_ms);
        } else {
            light_ctrl::set_white(white);
            light_ctrl::set_rgb(r, g, b);
            light_ctrl::set_brightness(brightness);
            light_ctrl::set_on(on);
        }
        mqtt_mgr::publish_state();
        return true;
    }

    // --- setDefaultFadeTime --- (persisted; used when a command omits "fadeTime")
    if (strcmp(cmd, "setDefaultFadeTime") == 0) {
        if (!s_cfg) {
            mqtt_mgr::publish_warning("LIGHT_CMD_INVALID", "config not available", JsonVariantConst());
            return false;
        }
        JsonVariantConst v = doc["fadeTime"];
        if (v.isNull()) {
            mqtt_mgr::publish_warning("LIGHT_CMD_INVALID", "missing fadeTime field", JsonVariantConst());
            return false;
        }
        double ft = v.as<double>();
        if (ft < 0.0)  ft = 0.0;
        if (ft > 60.0) ft = 60.0;  // sane ceiling
        s_cfg->default_fade_time_s = (float)ft;
        if (!cfg::save(*s_cfg)) {
            pxlog::warn(TAG, "setDefaultFadeTime: config save failed");
            mqtt_mgr::publish_warning("LIGHT_CONFIG_SAVE_FAILED", "failed to persist default_fade_time_s", JsonVariantConst());
        }
        pxlog::info(TAG, "default_fade_time_s=%.2f", ft);
        JsonDocument data;
        data["default_fade_time_s"] = s_cfg->default_fade_time_s;
        mqtt_mgr::publish_event("device", "default-fade-time-updated", nullptr, data.as<JsonVariantConst>());
        mqtt_mgr::publish_state();
        return true;
    }

    // --- setColorScene / scene ---
    if (strcmp(cmd, "setColorScene") == 0 || strcmp(cmd, "scene") == 0) {
        const char* name = doc["scene"] | "";
        if (!name || !name[0]) {
            mqtt_mgr::publish_warning("LIGHT_CMD_INVALID", "missing scene field", JsonVariantConst());
            return false;
        }
        uint32_t fade_ms = fade_ms_from(doc);
        light_ctrl::apply_scene(name, fade_ms);
        mqtt_mgr::publish_state();
        return true;
    }

    // --- getState / getStatus ---
    if (strcmp(cmd, "getState") == 0 || strcmp(cmd, "getStatus") == 0) {
        mqtt_mgr::publish_state();
        return true;
    }

    // --- identify ---
    if (strcmp(cmd, "identify") == 0) {
        light_ctrl::identify();
        return true;
    }

    // --- restart ---
    if (strcmp(cmd, "restart") == 0) {
        pxlog::warn(TAG, "restart requested via command");
        s_restart_pending = true;
        s_restart_at = millis() + 500;
        return true;
    }

    // --- setWhite ---
    if (strcmp(cmd, "setWhite") == 0) {
        bool w = doc["white"] | false;
        light_ctrl::set_white(w);
        if (!w && !light_ctrl::state().r && !light_ctrl::state().g && !light_ctrl::state().b)
            light_ctrl::set_on(false);
        mqtt_mgr::publish_state();
        return true;
    }

    // --- setUV ---
    if (strcmp(cmd, "setUV") == 0) {
        if (!doc["level"].is<int>()) {
            mqtt_mgr::publish_warning("LIGHT_CMD_INVALID", "missing level field", JsonVariantConst());
            return false;
        }
        int lvl = doc["level"].as<int>();
        if (lvl < 0)   lvl = 0;
        if (lvl > 255) lvl = 255;
        light_ctrl::set_uv((uint8_t)lvl);
        mqtt_mgr::publish_state();
        return true;
    }

    pxlog::warn(TAG, "unknown command: %s", cmd);
    mqtt_mgr::publish_warning("LIGHT_CMD_UNKNOWN",
                              (String("unknown command: ") + cmd).c_str(),
                              JsonVariantConst());
    return false;
}

bool handle_payload(const uint8_t* payload, size_t len) {
    JsonDocument doc;
    DeserializationError de = deserializeJson(doc, payload, len);
    if (de) {
        pxlog::warn(TAG, "json parse error: %s", de.c_str());
        mqtt_mgr::publish_warning("LIGHT_CMD_INVALID", de.c_str(), JsonVariantConst());
        return false;
    }
    return handle(doc);
}

} // namespace commands
