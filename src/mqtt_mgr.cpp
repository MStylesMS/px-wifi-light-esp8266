// mqtt_mgr.cpp
#include "mqtt_mgr.h"
#include "log.h"
#include "wifi_mgr.h"
#include "state.h"

#include <ESP8266WiFi.h>
#include <PubSubClient.h>

namespace mqtt_mgr {

static const char* TAG = "mqtt";

static cfg::Config*  s_cfg  = nullptr;
static MessageCb     s_cb   = nullptr;
static void*         s_user = nullptr;

static WiFiClient    s_net;
static PubSubClient  s_client(s_net);
static String        s_subscribed_commands;

static uint32_t s_last_connect_attempt = 0;
static uint32_t s_last_heartbeat       = 0;
static bool     s_announced            = false;
static bool     s_commands_subscribed  = false;

// Deferred inbound message dispatch (PubSubClient is not re-entrant).
static char     s_pending_payload[MQTT_MAX_PACKET_SIZE];
static size_t   s_pending_len          = 0;
static bool     s_pending_message      = false;

static uint32_t s_reconnect_count      = 0;
static uint32_t s_publish_fail_count   = 0;
static uint32_t s_last_inbound_cmd_ms  = 0;

static void write_uptime_ts(char* out, size_t n) {
    snprintf(out, n, "uptime+%lus", (unsigned long)(millis() / 1000UL));
}

static void update_mqtt_state(bool connected) {
    appstate::set_mqtt_connected(connected);
    appstate::set_mqtt_subscribed(connected && s_commands_subscribed);
    appstate::set_mqtt_stats(s_reconnect_count, s_publish_fail_count, s_last_inbound_cmd_ms);
}

static bool publish_json(const char* topic, const JsonDocument& doc, bool retain = false) {
    if (!s_client.connected()) return false;
    size_t body_len = measureJson(doc);
    if (body_len >= MQTT_MAX_PACKET_SIZE) {
        pxlog::warn(TAG, "publish skipped: %s payload too large (%u bytes)",
                    topic, (unsigned)body_len);
        return false;
    }
    char body[MQTT_MAX_PACKET_SIZE];
    size_t written = serializeJson(doc, body, sizeof(body));
    bool ok = s_client.publish(topic, (const uint8_t*)body, written, retain);
    if (!ok) {
        ++s_publish_fail_count;
        pxlog::warn(TAG, "publish failed: %s (%u bytes)", topic, (unsigned)written);
    }
    update_mqtt_state(true);
    return ok;
}

static void on_msg(char* /*topic*/, uint8_t* payload, unsigned int len) {
    if ((size_t)len >= sizeof(s_pending_payload)) {
        pxlog::warn(TAG, "incoming MQTT payload too large (%u bytes)", (unsigned)len);
        return;
    }
    if (s_pending_message) {
        pxlog::warn(TAG, "incoming MQTT message dropped while dispatch pending");
        return;
    }
    s_pending_len = (size_t)len;
    if (s_pending_len) memcpy(s_pending_payload, payload, s_pending_len);
    s_pending_message = true;
}

static void dispatch_pending_message() {
    if (!s_pending_message) return;
    s_pending_message = false;
    s_last_inbound_cmd_ms = millis();
    update_mqtt_state(true);
    if (s_cb) s_cb("", (const uint8_t*)s_pending_payload, s_pending_len, s_user);
}

static bool ensure_subscribed() {
    if (!s_client.connected() || !s_cfg) return false;
    if (s_commands_subscribed) return true;

    s_subscribed_commands = s_cfg->mqtt_base_topic + "/commands";
    if (s_client.subscribe(s_subscribed_commands.c_str(), 1)) {
        pxlog::info(TAG, "sub %s", s_subscribed_commands.c_str());
        s_commands_subscribed = true;
        update_mqtt_state(true);
        return true;
    }

    pxlog::warn(TAG, "sub FAILED %s", s_subscribed_commands.c_str());
    update_mqtt_state(true);
    return false;
}

void begin(cfg::Config* c, MessageCb cb, void* user) {
    s_cfg = c; s_cb = cb; s_user = user;
    s_client.setBufferSize(MQTT_MAX_PACKET_SIZE);
    s_client.setKeepAlive(MQTT_KEEPALIVE);
    s_client.setCallback(on_msg);
    update_mqtt_state(false);
}

bool connected() { return s_client.connected(); }

bool publish_state() {
    if (!s_cfg || !s_client.connected()) return false;
    JsonDocument doc;
    appstate::build_state(*s_cfg, doc);
    String topic = s_cfg->mqtt_base_topic + "/state";
    return publish_json(topic.c_str(), doc, /*retain=*/true);
}

bool publish_scenes() {
    if (!s_cfg || !s_client.connected()) return false;
    struct UiScene { const char* id; const char* label; const char* swatch; };
    static const UiScene k_ui_scenes[] = {
        { "white",       "White",        "#F4F4F4" },
        { "brightWhite", "Bright White", "#FFFFFF" },
        { "softWhite",   "Soft White",   "#FFE8E0" },
        { "moonlight",   "Moonlight",    "#B0B0C8" },
        { "coolWhite",   "Cool White",   "#A0C8FF" },
        { "nightLight",  "Night Light",  "#FF8000" },
        { "red",         "Red",          "#FF0000" },
        { "orange",      "Orange",       "#FF6E00" },
        { "yellow",      "Yellow",       "#FFDC00" },
        { "green",       "Green",        "#00FF5A" },
        { "cyan",        "Cyan",         "#00DCFF" },
        { "blue",        "Blue",         "#0046FF" },
        { "magenta",     "Magenta",      "#FF00C8" },
        { "purple",      "Purple",       "#AA3CFF" },
        { "pink",        "Pink",         "#FF4080" },
        { "uv",          "UV",           "#2A0038" },
        { "off",         "Off",          "#000000" },
    };
    JsonDocument doc;
    JsonArray arr = doc["scenes"].to<JsonArray>();
    for (const auto& sc : k_ui_scenes) {
        JsonObject obj = arr.add<JsonObject>();
        obj["id"]     = sc.id;
        obj["label"]  = sc.label;
        obj["swatch"] = sc.swatch;
    }
    String topic = s_cfg->mqtt_base_topic + "/scenes";
    return publish_json(topic.c_str(), doc, /*retain=*/true);
}

bool publish_announce() {
    if (!s_cfg || !s_client.connected()) return false;
    if (!s_cfg->mqtt_announce_topic.length()) return false;
    JsonDocument doc;
    appstate::build_announce(*s_cfg, doc);
    return publish_json(s_cfg->mqtt_announce_topic.c_str(), doc);
}

bool publish_event(const char* type, const char* event, const char* message,
                   JsonVariantConst data) {
    if (!s_cfg || !s_client.connected()) return false;
    JsonDocument doc;
    char ts[40]; write_uptime_ts(ts, sizeof(ts));
    doc["timestamp"] = ts;
    doc["type"]      = type ? type : "device";
    doc["event"]     = event;
    if (message) doc["message"] = message;
    if (!data.isNull()) doc["data"] = data;
    String topic = s_cfg->mqtt_base_topic + "/events";
    return publish_json(topic.c_str(), doc);
}

bool publish_warning(const char* warning, const char* message, JsonVariantConst data) {
    if (!s_cfg || !s_client.connected()) return false;
    JsonDocument doc;
    char ts[40]; write_uptime_ts(ts, sizeof(ts));
    doc["timestamp"] = ts;
    doc["warning"]   = warning;
    if (message) doc["message"] = message;
    if (!data.isNull()) doc["data"] = data;
    String topic = s_cfg->mqtt_base_topic + "/warnings";
    return publish_json(topic.c_str(), doc);
}

static bool try_connect() {
    if (!wifi_mgr::sta_connected()) return false;
    if (s_cfg->mqtt_host.length() == 0) return false;
    if (s_client.connected()) return true;

    s_client.setServer(s_cfg->mqtt_host.c_str(), s_cfg->mqtt_port);

    String client_id = String("px-light-") + cfg::mac_suffix() + "-" + String(millis());

    // Last-Will: offline tombstone, retained.
    String will_topic = s_cfg->mqtt_base_topic + "/state";
    JsonDocument will;
    char ts[40]; write_uptime_ts(ts, sizeof(ts));
    will["timestamp"]   = ts;
    will["application"] = "px-wifi-light-esp8266";
    will["instance"]    = s_cfg->prop_name;
    will["status"]      = "offline";
    char will_body[256];
    serializeJson(will, will_body, sizeof(will_body));

    bool ok;
    if (s_cfg->mqtt_username.length()) {
        ok = s_client.connect(client_id.c_str(),
                              s_cfg->mqtt_username.c_str(),
                              s_cfg->mqtt_password.c_str(),
                              will_topic.c_str(), 1, true,
                              will_body);
    } else {
        ok = s_client.connect(client_id.c_str(),
                              will_topic.c_str(), 1, true,
                              will_body);
    }

    if (!ok) {
        pxlog::warn(TAG, "connect to %s:%u failed rc=%d",
                    s_cfg->mqtt_host.c_str(), (unsigned)s_cfg->mqtt_port, s_client.state());
        update_mqtt_state(false);
        return false;
    }

    pxlog::info(TAG, "connected to %s:%u as %s",
                s_cfg->mqtt_host.c_str(), (unsigned)s_cfg->mqtt_port, client_id.c_str());

    s_commands_subscribed = false;
    s_announced           = false;
    ++s_reconnect_count;
    update_mqtt_state(true);
    ensure_subscribed();
    return true;
}

void loop() {
    if (!s_cfg) return;

    uint32_t now = millis();

    if (!s_client.connected()) {
        update_mqtt_state(false);
        // Retry with 5-second back-off.
        if (now - s_last_connect_attempt >= 5000UL) {
            s_last_connect_attempt = now;
            try_connect();
        }
        return;
    }

    s_client.loop();
    dispatch_pending_message();
    ensure_subscribed();

    // Announce once per connection.
    if (!s_announced) {
        s_announced = true;
        publish_announce();
        publish_scenes();
        publish_state();
    }

    // Periodic heartbeat — only advance timer after a successful publish.
    if (now - s_last_heartbeat >= s_cfg->mqtt_heartbeat_interval_ms) {
        if (publish_state()) {
            s_last_heartbeat = now;
        }
    }
}

} // namespace mqtt_mgr
