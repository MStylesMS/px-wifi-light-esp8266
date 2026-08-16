// commands.cpp -- MQTT / HTTP light command dispatch.
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

// After a faded off, restore pre-off brightness into the state field once
// the fade completes (visual ramp went to 0).
static int16_t s_restore_brightness_after_fade = -1;

void begin(cfg::Config* c) { s_cfg = c; }

void tick() {
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

static void fill_light_data(JsonDocument& data) {
    const light_ctrl::State& ls = light_ctrl::state();
    data["on"] = ls.on;
    data["white"] = ls.white;
    data["r"] = ls.r;
    data["g"] = ls.g;
    data["b"] = ls.b;
    data["brightness"] = ls.brightness;
    data["uv"] = light_ctrl::uv_level();
    if (ls.scene.length()) data["scene"] = ls.scene;
    else data["scene"] = nullptr;
}

static void emit_light_event(const char* event) {
    JsonDocument data;
    fill_light_data(data);
    mqtt_mgr::publish_event("light", event, nullptr, data.as<JsonVariantConst>());
}

static const char* event_name_for_cmd(const char* cmd) {
    if (strcmp(cmd, "allOn") == 0) return "all-on";
    if (strcmp(cmd, "allOff") == 0) return "all-off";
    if (strcmp(cmd, "setColor") == 0) return "set-color";
    if (strcmp(cmd, "setBrightness") == 0) return "set-brightness";
    if (strcmp(cmd, "setDefaultFadeTime") == 0) return "default-fade-time-updated";
    if (strcmp(cmd, "setColorScene") == 0 || strcmp(cmd, "scene") == 0) return "set-color-scene";
    if (strcmp(cmd, "getState") == 0 || strcmp(cmd, "getStatus") == 0) return "get-state";
    if (strcmp(cmd, "setWhite") == 0) return "set-white";
    if (strcmp(cmd, "setUV") == 0) return "set-uv";
    // on, off, fade, identify, restart -- already kebab-safe single tokens
    return cmd;
}

bool handle(const JsonDocument& doc) {
    const char* cmd = doc["command"] | "";
    if (!cmd || !cmd[0]) {
        pxlog::warn(TAG, "payload missing 'command' field");
        return false;
    }

    pxlog::info(TAG, "command: %s", cmd);
    s_restore_brightness_after_fade = -1;

    // --- on / allOn ---
    // RGB/white come from the still-held state fields (off does not clear them).
    // UV is restored from the snapshot captured at the last off/allOff.
    // If nothing is set, default to white on, RGB 0, UV 0.
    if (strcmp(cmd, "on") == 0 || strcmp(cmd, "allOn") == 0) {
        const light_ctrl::State& cur = light_ctrl::state();
        bool white = cur.white;
        uint8_t r = cur.r, g = cur.g, b = cur.b;
        uint8_t bri = cur.brightness ? cur.brightness : 100;
        uint8_t uv = 0;
        if (light_ctrl::preserved().valid)
            uv = light_ctrl::preserved().uv;
        else
            uv = light_ctrl::uv_level();

        if (!white && !r && !g && !b && uv == 0)
            white = true;  // nothing set yet -- default white on

        if (doc["brightness"].is<int>())
            bri = clamp_pct(doc["brightness"].as<int>());

        uint32_t fade_ms = fade_ms_from(doc);
        light_ctrl::fade_to(true, white, r, g, b, bri, uv, fade_ms);
        light_ctrl::set_scene_name("");
        mqtt_mgr::publish_state();
        emit_light_event(event_name_for_cmd(cmd));
        return true;
    }

    // --- off / allOff ---
    if (strcmp(cmd, "off") == 0 || strcmp(cmd, "allOff") == 0) {
        const light_ctrl::State& cur = light_ctrl::state();
        // Capture before blanking so allOn can restore W/RGB/bri/UV.
        light_ctrl::capture_preserve_from_current();
        uint32_t fade_ms = fade_ms_from(doc);
        if (fade_ms > 0) {
            s_restore_brightness_after_fade = (int16_t)(
                light_ctrl::preserved().valid ? light_ctrl::preserved().brightness : cur.brightness);
            // Keep logical white/rgb fields; ramp bri and UV to 0; on=false at end.
            light_ctrl::fade_to(false, cur.white, cur.r, cur.g, cur.b, 0, 0, fade_ms);
            light_ctrl::set_scene_name("off");
        } else {
            light_ctrl::fade_to(false, cur.white, cur.r, cur.g, cur.b,
                                cur.brightness ? cur.brightness : 100, 0, 0);
            // Immediate path: force off + uv 0 while keeping channel colour fields.
            // fade_to(0) already set on=false, uv=0, scene=off but also overwrote bri.
            // Restore bri from preserve so next on works even without fade restore path.
            if (light_ctrl::preserved().valid)
                light_ctrl::restore_brightness_field(light_ctrl::preserved().brightness);
            light_ctrl::set_scene_name("off");
        }
        mqtt_mgr::publish_state();
        emit_light_event(event_name_for_cmd(cmd));
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

        uint8_t target_brightness = doc["brightness"].is<int>()
            ? clamp_pct(doc["brightness"].as<int>()) : light_ctrl::state().brightness;
        uint8_t uv = light_ctrl::uv_level();  // setColor does not touch UV
        // Default: exclusive RGB mode (white off). Optional `white` preserves/sets
        // the white MOSFET so channel toggles can zero RGB without killing white.
        bool white = false;
        if (doc["white"].is<bool>()) white = doc["white"].as<bool>();
        bool on = white || r || g || b || uv > 0;
        uint32_t fade_ms = fade_ms_from(doc);
        light_ctrl::fade_to(on, white, r, g, b, target_brightness, uv, fade_ms);
        mqtt_mgr::publish_state();
        emit_light_event("set-color");
        return true;
    }

    // --- setBrightness ---
    if (strcmp(cmd, "setBrightness") == 0) {
        if (!doc["brightness"].is<int>()) {
            mqtt_mgr::publish_warning("LIGHT_CMD_INVALID", "missing brightness field", JsonVariantConst());
            return false;
        }
        uint8_t target = clamp_pct(doc["brightness"].as<int>());
        const light_ctrl::State& cur = light_ctrl::state();
        uint32_t fade_ms = fade_ms_from(doc);
        light_ctrl::fade_to(true, cur.white, cur.r, cur.g, cur.b, target,
                            light_ctrl::uv_level(), fade_ms);
        mqtt_mgr::publish_state();
        emit_light_event("set-brightness");
        return true;
    }

    // --- fade ---
    if (strcmp(cmd, "fade") == 0) {
        const light_ctrl::State& cur = light_ctrl::state();
        uint8_t r = cur.r, g = cur.g, b = cur.b;
        bool white = cur.white;
        uint8_t uv = light_ctrl::uv_level();

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
        if (doc["level"].is<int>()) {
            int lvl = doc["level"].as<int>();
            if (lvl < 0) lvl = 0;
            if (lvl > 255) lvl = 255;
            uv = (uint8_t)lvl;
        }

        uint8_t brightness = doc["brightness"].is<int>()
            ? clamp_pct(doc["brightness"].as<int>()) : cur.brightness;
        bool on = brightness > 0 || uv > 0 || white || r || g || b;
        uint32_t fade_ms = fade_ms_from(doc);
        light_ctrl::fade_to(on, white, r, g, b, brightness, uv, fade_ms);
        mqtt_mgr::publish_state();
        emit_light_event("fade");
        return true;
    }

    // --- setDefaultFadeTime ---
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
        if (ft > 60.0) ft = 60.0;
        s_cfg->default_fade_time_s = (float)ft;
        if (!cfg::save(*s_cfg)) {
            pxlog::warn(TAG, "setDefaultFadeTime: config save failed");
            mqtt_mgr::publish_warning("LIGHT_CONFIG_SAVE_FAILED",
                                      "failed to persist default_fade_time_s", JsonVariantConst());
        }
        pxlog::info(TAG, "default_fade_time_s=%.2f", ft);
        JsonDocument data;
        data["default_fade_time_s"] = s_cfg->default_fade_time_s;
        mqtt_mgr::publish_event("device", "default-fade-time-updated", nullptr,
                                data.as<JsonVariantConst>());
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
        // Scene "off" is all-channel off -- preserve UV/channels like allOff.
        if (strcasecmp(name, "off") == 0)
            light_ctrl::capture_preserve_from_current();
        uint32_t fade_ms = fade_ms_from(doc);
        light_ctrl::apply_scene(name, fade_ms);
        if (strcasecmp(name, "off") == 0 && fade_ms > 0) {
            const light_ctrl::Preserved& p = light_ctrl::preserved();
            if (p.valid) s_restore_brightness_after_fade = (int16_t)p.brightness;
        }
        mqtt_mgr::publish_state();
        emit_light_event("set-color-scene");
        return true;
    }

    // --- getState / getStatus ---
    if (strcmp(cmd, "getState") == 0 || strcmp(cmd, "getStatus") == 0) {
        mqtt_mgr::publish_state();
        emit_light_event("get-state");
        return true;
    }

    // --- identify ---
    if (strcmp(cmd, "identify") == 0) {
        light_ctrl::identify();
        emit_light_event("identify");
        return true;
    }

    // --- restart ---
    if (strcmp(cmd, "restart") == 0) {
        pxlog::warn(TAG, "restart requested via command");
        s_restart_pending = true;
        s_restart_at = millis() + 500;
        emit_light_event("restart");
        return true;
    }

    // --- setWhite ---
    if (strcmp(cmd, "setWhite") == 0) {
        bool w = doc["white"] | false;
        const light_ctrl::State& cur = light_ctrl::state();
        uint8_t uv = light_ctrl::uv_level();
        if (w) {
            light_ctrl::fade_to(true, true, cur.r, cur.g, cur.b, cur.brightness, uv, 0);
        } else {
            bool any_rgb = cur.r || cur.g || cur.b;
            light_ctrl::fade_to(any_rgb || uv > 0, false, cur.r, cur.g, cur.b,
                                cur.brightness, uv, 0);
        }
        mqtt_mgr::publish_state();
        emit_light_event("set-white");
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
        const light_ctrl::State& cur = light_ctrl::state();
        uint32_t fade_ms = fade_ms_from(doc);
        // Keep main channels; ramp UV. Device on if UV or main channels active.
        bool on = cur.on || lvl > 0;
        if (!cur.on && lvl > 0) on = true;
        light_ctrl::fade_to(on || cur.white || cur.r || cur.g || cur.b || lvl > 0,
                            cur.white, cur.r, cur.g, cur.b, cur.brightness,
                            (uint8_t)lvl, fade_ms);
        mqtt_mgr::publish_state();
        emit_light_event("set-uv");
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
