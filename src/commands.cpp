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

void begin(cfg::Config* c) { s_cfg = c; }

void tick() {
    if (s_restart_pending && (int32_t)(millis() - s_restart_at) >= 0) {
        pxlog::warn(TAG, "restarting now");
        ESP.restart();
    }
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

    // --- on / allOn ---
    if (strcmp(cmd, "on") == 0 || strcmp(cmd, "allOn") == 0) {
        light_ctrl::set_on(true);
        if (!light_ctrl::state().white &&
            !light_ctrl::state().r &&
            !light_ctrl::state().g &&
            !light_ctrl::state().b) {
            // Nothing set yet — default to white on.
            light_ctrl::set_white(true);
        }
        mqtt_mgr::publish_state();
        return true;
    }

    // --- off / allOff ---
    if (strcmp(cmd, "off") == 0 || strcmp(cmd, "allOff") == 0) {
        light_ctrl::set_on(false);
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
        // Turn off white when setting an explicit colour.
        light_ctrl::set_white(false);
        light_ctrl::set_rgb(r, g, b);
        if (doc["brightness"].is<int>())
            light_ctrl::set_brightness((uint8_t)(doc["brightness"].as<int>()));
        mqtt_mgr::publish_state();
        return true;
    }

    // --- setBrightness ---
    if (strcmp(cmd, "setBrightness") == 0) {
        if (!doc["brightness"].is<int>()) {
            mqtt_mgr::publish_warning("LIGHT_CMD_INVALID", "missing brightness field", JsonVariantConst());
            return false;
        }
        light_ctrl::set_brightness((uint8_t)(doc["brightness"].as<int>()));
        if (!light_ctrl::state().on) light_ctrl::set_on(true);
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
        light_ctrl::apply_scene(name);
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
