// state.cpp — builds the canonical /state and announce JSON payloads.
#include "state.h"
#include "wifi_mgr.h"
#include "light_ctrl.h"

namespace appstate {

static void write_uptime_ts(char* out, size_t n) {
    snprintf(out, n, "uptime+%lus", (unsigned long)(millis() / 1000UL));
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
    out["free_heap"]   = (unsigned)ESP.getFreeHeap();

    // Light state
    const light_ctrl::State& ls = light_ctrl::state();
    out["on"]         = ls.on;
    out["white"]      = ls.white;
    out["r"]          = ls.r;
    out["g"]          = ls.g;
    out["b"]          = ls.b;
    out["brightness"] = ls.brightness;
    out["uv"]         = light_ctrl::uv_level();
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
