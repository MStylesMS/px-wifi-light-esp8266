// state.h — Canonical state / announce JSON builder.
#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include "config.h"

namespace appstate {

void build_state(const cfg::Config& c, JsonDocument& out);
void build_announce(const cfg::Config& c, JsonDocument& out);

void reset_heap_watermark();
void set_mqtt_connected(bool v);
void set_mqtt_subscribed(bool v);
void set_mqtt_stats(uint32_t reconnect_count, uint32_t publish_fail_count,
                    uint32_t last_inbound_cmd_ms);

} // namespace appstate
