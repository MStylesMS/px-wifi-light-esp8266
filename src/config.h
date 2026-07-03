// config.h — Persistent device configuration for px-wifi-light-esp8266.
#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

// ----- Compile-time pin assignments (LoLin NodeMCU V3 / ESP12F) -----
// Do NOT change without a hardware revision — see docs/pin-mapping.md.
namespace pins {
    inline constexpr uint8_t WHITE     = 5;   // D1 / GPIO5  — digital on/off
    inline constexpr uint8_t GREEN     = 14;  // D5 / GPIO14 — PWM
    inline constexpr uint8_t RED       = 12;  // D6 / GPIO12 — PWM
    inline constexpr uint8_t BLUE      = 13;  // D7 / GPIO13 — PWM
    inline constexpr uint8_t UV        = 2;   // D4 / GPIO2  — PWM 0-255
    inline constexpr uint8_t FLASH_BTN = 0;   // GPIO0 / D3  — factory reset
} // namespace pins

namespace cfg {

struct WifiCreds {
    String ssid;
    String password;
};

struct Config {
    // device
    String    prop_name;

    // wifi
    WifiCreds wifi_primary;
    WifiCreds wifi_backup;
    String    ap_password;

    // mqtt
    String   mqtt_host;
    uint16_t mqtt_port;
    String   mqtt_username;
    String   mqtt_password;
    String   mqtt_base_topic;
    String   mqtt_announce_topic;
    uint32_t mqtt_heartbeat_interval_ms;

    // light
    // Fade duration (seconds) used for on/off/setBrightness/setColor/fade
    // commands that omit "fadeTime". An explicit "fadeTime" (including 0)
    // in a command always overrides this. Settable via /api/config or the
    // MQTT "setDefaultFadeTime" command (see commands.cpp).
    float    default_fade_time_s;
};

// Returns the last 4 hex chars of the MAC address (available after WiFi init).
String mac_suffix();

// Populate c with built-in defaults, substituting XXXX with mac_suffix().
void load_defaults(Config& c);

// Read /config.json from LittleFS into c.  Falls back to defaults on any error.
// Sets was_invalid=true if the file was missing or malformed.
bool load(Config& c, bool& was_invalid);

// Persist c to /config.json.  Returns true on success.
bool save(const Config& c);

// Delete /config.json (factory reset).
bool wipe();

// Check FLASH button (GPIO0) held low for hold_ms at boot.
bool factory_reset_requested(uint32_t hold_ms = 3000);

bool to_json(const Config& c, JsonDocument& out);
bool from_json(Config& c, const JsonDocument& in, String* err_out);
bool reboot_required(const Config& a, const Config& b);

} // namespace cfg
