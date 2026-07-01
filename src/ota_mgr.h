// ota_mgr.h
#pragma once
#include "config.h"
#include <ESP8266WebServer.h>

namespace ota_mgr {

void mount_http_update(ESP8266WebServer& server, const cfg::Config& c);
void begin_arduino_ota(const cfg::Config& c);
void loop();

} // namespace ota_mgr
