// web_ui.h — minimal web server for px-wifi-light-esp8266.
#pragma once
#include "config.h"

namespace web_ui {

void begin(cfg::Config* cfg);
void loop();

} // namespace web_ui
