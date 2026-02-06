#include "config_store.h"

static String gen_token(size_t bytes_len = 16)
{
  // 16 bytes => 32 hex chars
  String out;
  out.reserve(bytes_len * 2);
  for (size_t i = 0; i < bytes_len; i++)
  {
    uint8_t b = (uint8_t)(esp_random() & 0xFF);
    const char* hex = "0123456789abcdef";
    out += hex[(b >> 4) & 0xF];
    out += hex[b & 0xF];
  }
  return out;
}


static Preferences prefs;

void config_begin() {
  prefs.begin("flexit", false);
}

static String chip_suffix4() {
  uint64_t mac = ESP.getEfuseMac();
  uint16_t s = (uint16_t)(mac & 0xFFFF);
  char buf[5];
  snprintf(buf, sizeof(buf), "%04X", s);
  return String(buf);
}

String config_chip_suffix4() { return chip_suffix4(); }

String DeviceConfig::ap_ssid() const {
  return String("VentReader-Setup-") + chip_suffix4();
}

DeviceConfig config_load() {
  DeviceConfig c;

  c.wifi_ssid = prefs.getString("ssid", "");
  c.wifi_pass = prefs.getString("wpass", "");

  c.api_token = prefs.getString("token", "");
  c.token_generated = prefs.getBool("tgen", false);
  if (!c.token_generated || c.api_token.length() < 16 || c.api_token == "CHANGE_ME")
  {
    c.api_token = gen_token(16);
    c.token_generated = true;
    // persist immediately
    prefs.putString("token", c.api_token);
    prefs.putBool("tgen", true);
  }
  c.admin_user = prefs.getString("auser", "admin");
  c.admin_pass = prefs.getString("apass", "ventreader");
  c.admin_pass_changed = prefs.getBool("apchg", false);

  // wizard / onboarding
  c.setup_completed = prefs.getBool("setup", false);

  // model selection (Modbus map)
  c.model = prefs.getString("model", "S3");

  c.modbus_enabled = prefs.getBool("modbus", false);
  c.homey_enabled  = prefs.getBool("homey", true);

  c.poll_interval_ms = prefs.getULong("pollms", 10UL * 60UL * 1000UL);

  return c;
}

void config_save(const DeviceConfig& c) {
  prefs.putString("ssid", c.wifi_ssid);
  prefs.putString("wpass", c.wifi_pass);

  prefs.putString("token", c.api_token);
  prefs.putString("auser", c.admin_user);
  prefs.putString("apass", c.admin_pass);
  prefs.putBool("apchg", c.admin_pass_changed);

  prefs.putBool("setup", c.setup_completed);
  prefs.putString("model", c.model);

  prefs.putBool("setup", c.setup_completed);
  prefs.putString("model", c.model);

  prefs.putBool("modbus", c.modbus_enabled);
  prefs.putBool("homey", c.homey_enabled);

  prefs.putULong("pollms", c.poll_interval_ms);
}

void config_factory_reset() {
  // wipe all keys in this namespace
  prefs.clear();
}
