// commands.h — MQTT light command dispatch.
//
// Accepts JSON payloads in the Paradox command envelope:
//   { "command": "<name>", ... params ... }
//
// Can be called from both the MQTT message callback and the HTTP API handler.
#pragma once
#include "config.h"
#include <ArduinoJson.h>

namespace commands {

void begin(cfg::Config* c);
void tick();

// Parse and execute one command payload (JSON document).
// Returns true if the command was recognised and applied.
bool handle(const JsonDocument& doc);

// Convenience overload from raw bytes (MQTT callback).
bool handle_payload(const uint8_t* payload, size_t len);

} // namespace commands
