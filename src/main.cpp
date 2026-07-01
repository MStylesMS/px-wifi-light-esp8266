// main.cpp — px-wifi-light-esp8266 entry point.
#include <Arduino.h>
#include <LittleFS.h>

#include "log.h"
#include "config.h"
#include "wifi_mgr.h"
#include "web_ui.h"
#include "ota_mgr.h"

static const char* TAG = "main";
static cfg::Config s_cfg;
static uint32_t s_last_hb_ms = 0;

void setup() {
    pxlog::begin(115200);
    pxlog::info(TAG, "boot fw=%s", FW_VERSION);

    if (!LittleFS.begin()) {
        pxlog::err(TAG, "LittleFS mount failed; formatting...");
        LittleFS.format();
        LittleFS.begin();
    }

    // Factory reset: hold FLASH button during boot.
    if (cfg::factory_reset_requested(3000)) {
        cfg::wipe();
        pxlog::warn(TAG, "factory reset performed");
    }

    bool was_invalid = false;
    cfg::load(s_cfg, was_invalid);
    pxlog::info(TAG, "instance: %s", s_cfg.prop_name.c_str());

    wifi_mgr::begin(s_cfg);
    web_ui::begin(&s_cfg);
    ota_mgr::begin_arduino_ota(s_cfg);

    pxlog::info(TAG, "setup complete — AP: http://%s/  mdns: http://%s/",
                wifi_mgr::ap_ip().c_str(),
                wifi_mgr::mdns_fqdn().c_str());
}

void loop() {
    wifi_mgr::loop();
    web_ui::loop();
    ota_mgr::loop();

    // Serial heartbeat every 30 s
    uint32_t now = millis();
    if (now - s_last_hb_ms >= 30000UL) {
        s_last_hb_ms = now;
        pxlog::info(TAG,
                    "hb uptime_s=%lu free_heap=%u sta=%s ip=%s rssi=%d ap_clients=%d",
                    (unsigned long)(now / 1000UL),
                    (unsigned)ESP.getFreeHeap(),
                    wifi_mgr::sta_connected() ? "up" : "down",
                    wifi_mgr::sta_connected() ? wifi_mgr::sta_ip().c_str() : "-",
                    wifi_mgr::sta_connected() ? wifi_mgr::sta_rssi() : 0,
                    wifi_mgr::ap_clients());
    }
}
