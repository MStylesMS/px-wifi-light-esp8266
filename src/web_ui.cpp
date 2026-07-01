// web_ui.cpp — minimal web server + JSON status/state/light APIs.
#include "web_ui.h"
#include "log.h"
#include "wifi_mgr.h"
#include "ota_mgr.h"
#include "state.h"
#include "commands.h"

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

// GET /api/status — brief connectivity snapshot (used by the status page header).
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
    String body; serializeJson(doc, body);
    s_server.send(200, "application/json", body);
}

// GET /api/state — full device state (same schema as MQTT /state topic).
static void handle_get_state() {
    JsonDocument doc;
    appstate::build_state(*s_cfg, doc);
    String body; serializeJson(doc, body);
    s_server.send(200, "application/json", body);
}

// POST /api/light — accepts a Paradox light command JSON body.
static void handle_post_light() {
    if (!s_server.hasArg("plain")) {
        s_server.send(400, "application/json", "{\"ok\":false,\"error\":\"empty body\"}");
        return;
    }
    String body = s_server.arg("plain");
    JsonDocument doc;
    DeserializationError de = deserializeJson(doc, body);
    if (de) {
        String err = String("{\"ok\":false,\"error\":\"") + de.c_str() + "\"}";
        s_server.send(400, "application/json", err);
        return;
    }
    bool ok = commands::handle(doc);
    s_server.send(ok ? 200 : 400,
                  "application/json",
                  ok ? "{\"ok\":true}" : "{\"ok\":false,\"error\":\"unknown or invalid command\"}");
}

// POST /api/config — partial config update.
static void handle_post_config() {
    if (!s_server.hasArg("plain")) {
        s_server.send(400, "application/json", "{\"ok\":false,\"error\":\"empty body\"}");
        return;
    }
    String body = s_server.arg("plain");
    JsonDocument incoming;
    DeserializationError de = deserializeJson(incoming, body);
    if (de) {
        String err = String("{\"ok\":false,\"error\":\"") + de.c_str() + "\"}";
        s_server.send(400, "application/json", err);
        return;
    }
    cfg::Config trial = *s_cfg;
    String err;
    if (!cfg::from_json(trial, incoming, &err)) {
        s_server.send(400, "application/json",
                      String("{\"ok\":false,\"error\":\"") + err + "\"}");
        return;
    }
    bool reboot = cfg::reboot_required(*s_cfg, trial);
    if (!cfg::save(trial)) {
        s_server.send(500, "application/json", "{\"ok\":false,\"error\":\"save failed\"}");
        return;
    }
    *s_cfg = trial;
    if (reboot) {
        s_reboot_pending = true;
        s_reboot_at = millis() + 1500;
    }
    JsonDocument resp;
    resp["ok"] = true;
    resp["reboot_required"] = reboot;
    String rbody; serializeJson(resp, rbody);
    s_server.send(200, "application/json", rbody);
    pxlog::info(TAG, "config saved (reboot_required=%d)", reboot ? 1 : 0);
}

// GET /api/config
static void handle_get_config() {
    JsonDocument doc;
    cfg::to_json(*s_cfg, doc);
    String body; serializeJson(doc, body);
    s_server.send(200, "application/json", body);
}

// POST /api/restart
static void handle_post_restart() {
    s_server.send(200, "application/json", "{\"ok\":true}");
    s_reboot_pending = true;
    s_reboot_at = millis() + 500;
    pxlog::warn(TAG, "reboot scheduled by web request");
}

// POST /api/reset — factory reset
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
    s_server.on("/api/state",   HTTP_GET,  handle_get_state);
    s_server.on("/api/light",   HTTP_POST, handle_post_light);
    s_server.on("/api/config",  HTTP_GET,  handle_get_config);
    s_server.on("/api/config",  HTTP_POST, handle_post_config);
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
