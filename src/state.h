// state.h — Canonical state / announce JSON builder.
#pragma once
#include <ArduinoJson.h>
#include "config.h"

namespace appstate {

void build_state(const cfg::Config& c, JsonDocument& out);
void build_announce(const cfg::Config& c, JsonDocument& out);

} // namespace appstate
