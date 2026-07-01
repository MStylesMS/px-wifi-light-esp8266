// config.h — Persistent device configuration for px-wifi-light-esp8266.
#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

namespace cfg {

struct WifiCreds {
    String ssid;
    String password;
};

struct Config {
    String    prop_name;
    WifiCreds wifi_primary;
    WifiCreds wifi_backup;
    String    ap_password;
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

} // namespace cfg
