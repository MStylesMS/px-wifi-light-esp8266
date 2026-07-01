// main.cpp — px-wifi-light-esp8266 entry point.
#include <Arduino.h>
#include <LittleFS.h>

#include "log.h"
#include "config.h"
#include "wifi_mgr.h"
#include "web_ui.h"
#include "ota_mgr.h"
#include "light_ctrl.h"
#include "commands.h"
#include "mqtt_mgr.h"

static const char* TAG = "main";
static cfg::Config s_cfg;
static uint32_t s_last_hb_ms = 0;

// MQTT message callback — dispatches to commands module.
static void on_mqtt(const char* topic, const uint8_t* payload, size_t len, void* /*user*/) {
    commands::handle_payload(payload, len);
}

void setup() {
    pxlog::begin(115200);
    pxlog::info(TAG, "boot fw=%s", FW_VERSION);

    if (!LittleFS.begin()) {
        pxlog::err(TAG, "LittleFS mount failed; formatting...");
        LittleFS.format();
        LittleFS.begin();
    }

    if (cfg::factory_reset_requested(3000)) {
        cfg::wipe();
        pxlog::warn(TAG, "factory reset performed");
    }

    bool was_invalid = false;
    cfg::load(s_cfg, was_invalid);
    pxlog::info(TAG, "instance: %s", s_cfg.prop_name.c_str());

    light_ctrl::begin();
    wifi_mgr::begin(s_cfg);
    web_ui::begin(&s_cfg);
    ota_mgr::begin_arduino_ota(s_cfg);

    commands::begin(&s_cfg);
    mqtt_mgr::begin(&s_cfg, on_mqtt, nullptr);

    pxlog::info(TAG, "setup complete — AP: http://%s/  mdns: http://%s/",
                wifi_mgr::ap_ip().c_str(),
                wifi_mgr::mdns_fqdn().c_str());
}

void loop() {
    wifi_mgr::loop();
    web_ui::loop();
    ota_mgr::loop();
    mqtt_mgr::loop();
    light_ctrl::tick();
    commands::tick();

    uint32_t now = millis();
    if (now - s_last_hb_ms >= 30000UL) {
        s_last_hb_ms = now;
        pxlog::info(TAG,
                    "hb uptime_s=%lu free_heap=%u sta=%s ip=%s rssi=%d mqtt=%s on=%d rgb=%u,%u,%u",
                    (unsigned long)(now / 1000UL),
                    (unsigned)ESP.getFreeHeap(),
                    wifi_mgr::sta_connected() ? "up" : "down",
                    wifi_mgr::sta_connected() ? wifi_mgr::sta_ip().c_str() : "-",
                    wifi_mgr::sta_connected() ? wifi_mgr::sta_rssi() : 0,
                    mqtt_mgr::connected() ? "up" : "down",
                    (int)light_ctrl::state().on,
                    (unsigned)light_ctrl::state().r,
                    (unsigned)light_ctrl::state().g,
                    (unsigned)light_ctrl::state().b);
    }
}

