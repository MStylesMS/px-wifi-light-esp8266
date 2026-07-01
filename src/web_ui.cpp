// web_ui.cpp — minimal web server + JSON status API.
#include "web_ui.h"
#include "log.h"
#include "wifi_mgr.h"
#include "ota_mgr.h"

#include <ArduinoJson.h>
#include <ESP8266WebServer.h>
#include <LittleFS.h>

namespace web_ui {

static const char* TAG = "web";
static ESP8266WebServer s_server(80);
static cfg::Config* s_cfg = nullptr;
static bool s_reboot_pending = false;
static uint32_t s_reboot_at = 0;

static bool stream_static(const char* path, const char* mime) {
    File f = LittleFS.open(path, "r");
    if (!f) return false;
    s_server.streamFile(f, mime);
    f.close();
    return true;
}

static void handle_root() {
    if (!stream_static("/index.html", "text/html")) {
        s_server.send(500, "text/plain",
                      "index.html missing — run `pio run -t uploadfs`.");
    }
}

static void handle_static_css() {
    if (!stream_static("/style.css", "text/css"))
        s_server.send(404, "text/plain", "not found");
}

static void handle_static_js() {
    if (!stream_static("/app.js", "application/javascript"))
        s_server.send(404, "text/plain", "not found");
}

// GET /api/status — returns JSON with network info.
static void handle_get_status() {
    JsonDocument doc;
    doc["prop_name"]     = s_cfg->prop_name;
    doc["fw_version"]    = FW_VERSION;
    doc["uptime_s"]      = (unsigned long)(millis() / 1000UL);
    doc["free_heap"]     = (unsigned)ESP.getFreeHeap();
    doc["ap_ip"]         = wifi_mgr::ap_ip();
    doc["ap_ssid"]       = wifi_mgr::ap_ssid();
    doc["ap_clients"]    = wifi_mgr::ap_clients();
    doc["sta_connected"] = wifi_mgr::sta_connected();
    doc["sta_ip"]        = wifi_mgr::sta_connected() ? wifi_mgr::sta_ip()   : String("");
    doc["sta_ssid"]      = wifi_mgr::sta_connected() ? wifi_mgr::sta_ssid() : String("");
    doc["sta_rssi"]      = wifi_mgr::sta_connected() ? wifi_mgr::sta_rssi() : 0;
    doc["mac"]           = wifi_mgr::mac_address();
    doc["mdns"]          = wifi_mgr::mdns_fqdn();

    String body;
    serializeJson(doc, body);
    s_server.send(200, "application/json", body);
}

// POST /api/restart
static void handle_post_restart() {
    s_server.send(200, "application/json", "{\"ok\":true}");
    s_reboot_pending = true;
    s_reboot_at = millis() + 500;
    pxlog::warn(TAG, "reboot scheduled by web request");
}

// POST /api/reset  — factory reset
static void handle_post_reset() {
    cfg::wipe();
    s_server.send(200, "application/json", "{\"ok\":true,\"reboot\":true}");
    s_reboot_pending = true;
    s_reboot_at = millis() + 500;
    pxlog::warn(TAG, "factory reset by web request");
}

void begin(cfg::Config* cfg) {
    s_cfg = cfg;

    s_server.on("/",            HTTP_GET,  handle_root);
    s_server.on("/style.css",   HTTP_GET,  handle_static_css);
    s_server.on("/app.js",      HTTP_GET,  handle_static_js);
    s_server.on("/api/status",  HTTP_GET,  handle_get_status);
    s_server.on("/api/restart", HTTP_POST, handle_post_restart);
    s_server.on("/api/reset",   HTTP_POST, handle_post_reset);

    ota_mgr::mount_http_update(s_server, *cfg);

    s_server.begin();
    pxlog::info(TAG, "HTTP server started on port 80");
}

void loop() {
    s_server.handleClient();
    if (s_reboot_pending && millis() >= s_reboot_at) {
        pxlog::warn(TAG, "rebooting now");
        ESP.restart();
    }
}

} // namespace web_ui
