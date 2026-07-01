// config.cpp
#include "config.h"
#include "log.h"

#include <ESP8266WiFi.h>
#include <LittleFS.h>

namespace cfg {

static const char* TAG      = "config";
static const char* PATH     = "/config.json";
static const char* PATH_BAD = "/config.bad.json";
static const uint8_t FLASH_BTN_PIN = 0;  // GPIO0 / FLASH button

String mac_suffix() {
    uint8_t mac[6];
    WiFi.macAddress(mac);
    char buf[5];
    snprintf(buf, sizeof(buf), "%02X%02X", mac[4], mac[5]);
    return String(buf);
}

static String subst_xxxx(const String& s) {
    String r = s;
    if (r.indexOf("XXXX") >= 0) r.replace("XXXX", mac_suffix());
    return r;
}

void load_defaults(Config& c) {
    c.prop_name             = "px-light-XXXX";
    c.wifi_primary.ssid     = "";
    c.wifi_primary.password = "";
    c.wifi_backup.ssid      = "";
    c.wifi_backup.password  = "";
    c.ap_password           = "Paradox1";

    c.mqtt_host                   = "";
    c.mqtt_port                   = 1883;
    c.mqtt_username               = "";
    c.mqtt_password               = "";
    c.mqtt_base_topic             = "paradox/lights/px-light-XXXX";
    c.mqtt_announce_topic         = "paradox/props";
    c.mqtt_heartbeat_interval_ms  = 10000;
}

static void substitute_placeholders(Config& c) {
    c.prop_name        = subst_xxxx(c.prop_name);
    c.mqtt_base_topic  = subst_xxxx(c.mqtt_base_topic);
}

static bool read_file_to_string(const char* path, String& out) {
    File f = LittleFS.open(path, "r");
    if (!f) return false;
    out.reserve(f.size() + 1);
    while (f.available()) out += (char)f.read();
    f.close();
    return true;
}

static bool write_string_to_file(const char* path, const String& s) {
    File f = LittleFS.open(path, "w");
    if (!f) return false;
    size_t n = f.write((const uint8_t*)s.c_str(), s.length());
    f.close();
    return n == s.length();
}

bool load(Config& c, bool& was_invalid) {
    was_invalid = false;
    load_defaults(c);

    if (!LittleFS.exists(PATH)) {
        pxlog::info(TAG, "no /config.json; using built-in defaults");
        substitute_placeholders(c);
        return true;
    }

    String body;
    if (!read_file_to_string(PATH, body)) {
        pxlog::err(TAG, "failed to read %s", PATH);
        was_invalid = true;
        substitute_placeholders(c);
        return true;
    }

    JsonDocument doc;
    DeserializationError de = deserializeJson(doc, body);
    if (de) {
        pxlog::err(TAG, "json parse error: %s; resetting to defaults", de.c_str());
        LittleFS.remove(PATH_BAD);
        LittleFS.rename(PATH, PATH_BAD);
        was_invalid = true;
        load_defaults(c);
        substitute_placeholders(c);
        save(c);
        return true;
    }

    String err;
    if (!from_json(c, doc, &err)) {
        pxlog::err(TAG, "schema error: %s; resetting to defaults", err.c_str());
        LittleFS.remove(PATH_BAD);
        LittleFS.rename(PATH, PATH_BAD);
        was_invalid = true;
        load_defaults(c);
        substitute_placeholders(c);
        save(c);
        return true;
    }

    substitute_placeholders(c);
    return true;
}

bool save(const Config& c) {
    JsonDocument doc;
    if (!to_json(c, doc)) return false;
    String body;
    serializeJsonPretty(doc, body);
    if (!write_string_to_file(PATH, body)) {
        pxlog::err(TAG, "failed to write %s", PATH);
        return false;
    }
    pxlog::info(TAG, "saved /config.json (%u bytes)", (unsigned)body.length());
    return true;
}

bool wipe() {
    bool ok = true;
    if (LittleFS.exists(PATH))     ok = LittleFS.remove(PATH)     && ok;
    if (LittleFS.exists(PATH_BAD)) ok = LittleFS.remove(PATH_BAD) && ok;
    return ok;
}

bool factory_reset_requested(uint32_t hold_ms) {
    pinMode(FLASH_BTN_PIN, INPUT_PULLUP);
    if (digitalRead(FLASH_BTN_PIN) != LOW) return false;
    pxlog::info(TAG, "FLASH held; checking %ums for factory reset", (unsigned)hold_ms);
    uint32_t t0 = millis();
    while (millis() - t0 < hold_ms) {
        if (digitalRead(FLASH_BTN_PIN) != LOW) return false;
        delay(20);
        yield();
    }
    pxlog::warn(TAG, "factory reset triggered");
    return true;
}

bool to_json(const Config& c, JsonDocument& out) {
    out.clear();
    out["device"]["prop_name"] = c.prop_name;

    JsonObject wp = out["wifi"]["primary"].to<JsonObject>();
    wp["ssid"]     = c.wifi_primary.ssid;
    wp["password"] = c.wifi_primary.password;

    JsonObject wb = out["wifi"]["backup"].to<JsonObject>();
    wb["ssid"]     = c.wifi_backup.ssid;
    wb["password"] = c.wifi_backup.password;

    out["wifi"]["ap_password"] = c.ap_password;

    JsonObject mqtt = out["mqtt"].to<JsonObject>();
    mqtt["host"]                   = c.mqtt_host;
    mqtt["port"]                   = c.mqtt_port;
    mqtt["username"]               = c.mqtt_username;
    mqtt["password"]               = c.mqtt_password;
    mqtt["base_topic"]             = c.mqtt_base_topic;
    mqtt["announce_topic"]         = c.mqtt_announce_topic;
    mqtt["heartbeat_interval_ms"]  = c.mqtt_heartbeat_interval_ms;

    return true;
}

bool from_json(Config& c, const JsonDocument& in, String* err_out) {
    if (in["device"]["prop_name"].is<const char*>())
        c.prop_name = in["device"]["prop_name"].as<String>();

    if (in["wifi"]["primary"]["ssid"].is<const char*>())
        c.wifi_primary.ssid = in["wifi"]["primary"]["ssid"].as<String>();
    if (in["wifi"]["primary"]["password"].is<const char*>())
        c.wifi_primary.password = in["wifi"]["primary"]["password"].as<String>();

    if (in["wifi"]["backup"]["ssid"].is<const char*>())
        c.wifi_backup.ssid = in["wifi"]["backup"]["ssid"].as<String>();
    if (in["wifi"]["backup"]["password"].is<const char*>())
        c.wifi_backup.password = in["wifi"]["backup"]["password"].as<String>();

    if (in["wifi"]["ap_password"].is<const char*>())
        c.ap_password = in["wifi"]["ap_password"].as<String>();

    if (in["mqtt"]["host"].is<const char*>())
        c.mqtt_host = in["mqtt"]["host"].as<String>();
    if (in["mqtt"]["port"].is<uint16_t>())
        c.mqtt_port = in["mqtt"]["port"].as<uint16_t>();
    if (in["mqtt"]["username"].is<const char*>())
        c.mqtt_username = in["mqtt"]["username"].as<String>();
    if (in["mqtt"]["password"].is<const char*>())
        c.mqtt_password = in["mqtt"]["password"].as<String>();
    if (in["mqtt"]["base_topic"].is<const char*>())
        c.mqtt_base_topic = in["mqtt"]["base_topic"].as<String>();
    if (in["mqtt"]["announce_topic"].is<const char*>())
        c.mqtt_announce_topic = in["mqtt"]["announce_topic"].as<String>();
    if (in["mqtt"]["heartbeat_interval_ms"].is<uint32_t>())
        c.mqtt_heartbeat_interval_ms = in["mqtt"]["heartbeat_interval_ms"].as<uint32_t>();

    return true;
}

bool reboot_required(const Config& a, const Config& b) {
    return a.wifi_primary.ssid     != b.wifi_primary.ssid     ||
           a.wifi_primary.password != b.wifi_primary.password ||
           a.wifi_backup.ssid      != b.wifi_backup.ssid      ||
           a.wifi_backup.password  != b.wifi_backup.password  ||
           a.ap_password           != b.ap_password           ||
           a.mqtt_host             != b.mqtt_host             ||
           a.mqtt_port             != b.mqtt_port;
}

} // namespace cfg
