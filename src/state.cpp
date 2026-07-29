// state.cpp — builds the canonical /state and announce JSON payloads.
#include "state.h"
#include "wifi_mgr.h"
#include "light_ctrl.h"

namespace appstate {

static uint32_t s_min_free_heap_bytes   = UINT32_MAX;
static bool     s_mqtt_connected        = false;
static bool     s_mqtt_subscribed       = false;
static uint32_t s_mqtt_reconnect_count  = 0;
static uint32_t s_mqtt_publish_fail_count = 0;
static uint32_t s_mqtt_last_inbound_cmd_ms = 0;

static void write_uptime_ts(char* out, size_t n) {
    snprintf(out, n, "uptime+%lus", (unsigned long)(millis() / 1000UL));
}

void reset_heap_watermark() { s_min_free_heap_bytes = UINT32_MAX; }

void set_mqtt_connected(bool v) { s_mqtt_connected = v; }

void set_mqtt_subscribed(bool v) { s_mqtt_subscribed = v; }

void set_mqtt_stats(uint32_t reconnect_count, uint32_t publish_fail_count,
                    uint32_t last_inbound_cmd_ms) {
    s_mqtt_reconnect_count      = reconnect_count;
    s_mqtt_publish_fail_count   = publish_fail_count;
    s_mqtt_last_inbound_cmd_ms  = last_inbound_cmd_ms;
}

void build_state(const cfg::Config& c, JsonDocument& out) {
    out.clear();
    char ts[40];
    write_uptime_ts(ts, sizeof(ts));
    out["timestamp"]   = ts;
    out["application"] = "px-wifi-light-esp8266";
    out["fw_version"]  = FW_VERSION;
    out["instance"]    = c.prop_name;
    out["uptime_s"]    = (unsigned long)(millis() / 1000UL);

    uint32_t free_heap_bytes = ESP.getFreeHeap();
    if (s_min_free_heap_bytes == UINT32_MAX || free_heap_bytes < s_min_free_heap_bytes) {
        s_min_free_heap_bytes = free_heap_bytes;
    }
    out["free_heap"] = free_heap_bytes;

    JsonObject health = out["health"].to<JsonObject>();
    health["free_heap_bytes"]     = free_heap_bytes;
    health["min_free_heap_bytes"] = s_min_free_heap_bytes;
    health["max_block_bytes"]     = (unsigned)ESP.getMaxFreeBlockSize();

    JsonObject mqtt = out["mqtt"].to<JsonObject>();
    mqtt["connected"]           = s_mqtt_connected;
    mqtt["subscribed_commands"] = s_mqtt_subscribed;
    mqtt["reconnect_count"]     = s_mqtt_reconnect_count;
    mqtt["publish_fail_count"]  = s_mqtt_publish_fail_count;
    mqtt["last_inbound_cmd_ms"] = s_mqtt_last_inbound_cmd_ms;

    // Light state
    const light_ctrl::State& ls = light_ctrl::state();
    out["on"]         = ls.on;
    out["white"]      = ls.white;
    out["r"]          = ls.r;
    out["g"]          = ls.g;
    out["b"]          = ls.b;
    out["brightness"] = ls.brightness;
    out["uv"]         = light_ctrl::uv_level();
    out["fading"]     = light_ctrl::fading();
    out["default_fade_time_s"] = c.default_fade_time_s;
    if (ls.scene.length())
        out["scene"] = ls.scene;
    else
        out["scene"] = nullptr;

    // WiFi
    JsonObject wifi = out["wifi"].to<JsonObject>();
    wifi["sta_connected"] = wifi_mgr::sta_connected();
    wifi["ap_ip"]         = wifi_mgr::ap_ip();
    wifi["ap_ssid"]       = wifi_mgr::ap_ssid();
    wifi["ap_clients"]    = wifi_mgr::ap_clients();
    if (wifi_mgr::sta_connected()) {
        wifi["sta_ip"]   = wifi_mgr::sta_ip();
        wifi["sta_ssid"] = wifi_mgr::sta_ssid();
        wifi["rssi"]     = wifi_mgr::sta_rssi();
    } else {
        wifi["sta_ip"]   = nullptr;
        wifi["sta_ssid"] = nullptr;
        wifi["rssi"]     = nullptr;
    }
    wifi["mac"]  = wifi_mgr::mac_address();
    wifi["mdns"] = wifi_mgr::mdns_fqdn();
}

void build_announce(const cfg::Config& c, JsonDocument& out) {
    out.clear();
    char ts[40];
    write_uptime_ts(ts, sizeof(ts));
    out["timestamp"]   = ts;
    out["application"] = "px-wifi-light-esp8266";
    out["fw_version"]  = FW_VERSION;
    out["instance"]    = c.prop_name;
    out["base_topic"]  = c.mqtt_base_topic;
    out["ip"]          = wifi_mgr::sta_connected() ? wifi_mgr::sta_ip() : wifi_mgr::ap_ip();
    out["mac"]         = wifi_mgr::mac_address();
    out["mdns"]        = wifi_mgr::mdns_fqdn();
}

} // namespace appstate
