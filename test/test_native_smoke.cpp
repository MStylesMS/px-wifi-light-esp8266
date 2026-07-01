#include <unity.h>
#include <ArduinoJson.h>
#include "../src/config.h"

static unsigned long s_fake_millis = 5000UL;
EspClass ESP;

unsigned long millis() { return s_fake_millis; }

// --- cfg namespace stubs for host build ---
namespace cfg {

String mac_suffix() { return String("AABB"); }

} // namespace cfg

// --- tests ---

void test_config_defaults_prop_name() {
    cfg::Config c;
    cfg::load_defaults(c);
    // prop_name contains "px-light" prefix
    TEST_ASSERT_TRUE(c.prop_name.find("px-light") != std::string::npos);
}

void test_config_to_from_json_roundtrip() {
    cfg::Config orig;
    orig.prop_name = "px-light-1234";
    orig.wifi_primary.ssid     = "TestNet";
    orig.wifi_primary.password = "secret";
    orig.wifi_backup.ssid      = "";
    orig.wifi_backup.password  = "";
    orig.ap_password = "appass";

    JsonDocument doc;
    TEST_ASSERT_TRUE(cfg::to_json(orig, doc));

    cfg::Config restored;
    String err;
    TEST_ASSERT_TRUE(cfg::from_json(restored, doc, &err));

    TEST_ASSERT_EQUAL_STRING(orig.prop_name.c_str(),               restored.prop_name.c_str());
    TEST_ASSERT_EQUAL_STRING(orig.wifi_primary.ssid.c_str(),       restored.wifi_primary.ssid.c_str());
    TEST_ASSERT_EQUAL_STRING(orig.wifi_primary.password.c_str(),   restored.wifi_primary.password.c_str());
    TEST_ASSERT_EQUAL_STRING(orig.ap_password.c_str(),             restored.ap_password.c_str());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_config_defaults_prop_name);
    RUN_TEST(test_config_to_from_json_roundtrip);
    return UNITY_END();
}
