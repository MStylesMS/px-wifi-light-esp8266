// wifi_mgr.h — AP+STA always-on, dual STA via WiFiMulti.
#pragma once
#include "config.h"
#include <Arduino.h>

namespace wifi_mgr {

String network_name_from_prop(const String& prop_name);

void begin(const cfg::Config& c);
void loop();

bool   sta_connected();
String sta_ip();
String sta_ssid();
int    sta_rssi();

String ap_ip();
String ap_ssid();
int    ap_clients();

String mac_address();
String mdns_hostname();
String mdns_fqdn();

} // namespace wifi_mgr
