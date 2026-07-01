// mqtt_mgr.h — MQTT connectivity, subscriptions, and state publishing.
#pragma once
#include "config.h"
#include <Arduino.h>
#include <ArduinoJson.h>

namespace mqtt_mgr {

typedef void (*MessageCb)(const char* topic, const uint8_t* payload, size_t len, void* user);

void begin(cfg::Config* c, MessageCb cb, void* user);
void loop();

bool connected();

bool publish_state();
bool publish_announce();
bool publish_event(const char* type, const char* event, const char* message,
                   JsonVariantConst data);
bool publish_warning(const char* warning, const char* message,
                     JsonVariantConst data);

} // namespace mqtt_mgr
