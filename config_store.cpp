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

static bool is_supported_model(const String& model)
{
  return model == "S3" ||
         model == "S4" ||
         model == "S2_EXP" ||
         model == "S7_EXP" ||
         model == "CL3_EXP" ||
         model == "CL4_EXP";
}

static String normalize_lang(const String& in)
{
  if (in == "no" || in == "da" || in == "sv" || in == "fi" || in == "en" || in == "uk") return in;
  return "no";
}

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

void config_apply_model_modbus_defaults(DeviceConfig& c, bool force)
{
  // Current recommended defaults for Nordic Basic Modbus family.
  // Keep model switch explicit so future models can diverge safely.
  if (force || c.modbus_transport_mode.length() == 0) c.modbus_transport_mode = "AUTO";
  if (force || c.modbus_serial_format.length() == 0) c.modbus_serial_format = "8N1";
  if (force || c.modbus_baud == 0) c.modbus_baud = 19200;
  if (force || c.modbus_slave_id == 0) c.modbus_slave_id = 1;
  if (force) c.modbus_addr_offset = 0;
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
  if (!is_supported_model(c.model)) c.model = "S3";

  c.modbus_enabled = prefs.getBool("modbus", false);
  c.homey_enabled  = prefs.getBool("homey", true);
  c.ha_enabled     = prefs.getBool("ha", true);
  c.control_enabled = prefs.getBool("ctrl", false);
  c.ui_language = normalize_lang(prefs.getString("lang", "no"));
  c.modbus_transport_mode = prefs.getString("mbtr", "");
  if (c.modbus_transport_mode != "AUTO" && c.modbus_transport_mode != "MANUAL")
    c.modbus_transport_mode = "";

  c.modbus_serial_format = prefs.getString("mbser", "");
  if (c.modbus_serial_format != "8N1" &&
      c.modbus_serial_format != "8E1" &&
      c.modbus_serial_format != "8O1")
    c.modbus_serial_format = "";

  c.modbus_baud = prefs.getUInt("mbbaud", 0);
  if (c.modbus_baud != 0 && (c.modbus_baud < 1200 || c.modbus_baud > 115200)) c.modbus_baud = 0;

  c.modbus_slave_id = (uint8_t)prefs.getUChar("mbid", 0);
  if (c.modbus_slave_id != 0 && (c.modbus_slave_id < 1 || c.modbus_slave_id > 247)) c.modbus_slave_id = 0;

  c.modbus_addr_offset = (int8_t)prefs.getChar("mboff", 0);
  if (c.modbus_addr_offset < -5 || c.modbus_addr_offset > 5) c.modbus_addr_offset = 0;

  // Fill missing values with model defaults.
  config_apply_model_modbus_defaults(c, false);

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

  prefs.putBool("modbus", c.modbus_enabled);
  prefs.putBool("homey", c.homey_enabled);
  prefs.putBool("ha", c.ha_enabled);
  prefs.putBool("ctrl", c.control_enabled);
  prefs.putString("lang", normalize_lang(c.ui_language));
  prefs.putString("mbtr", c.modbus_transport_mode);
  prefs.putString("mbser", c.modbus_serial_format);
  prefs.putUInt("mbbaud", c.modbus_baud);
  prefs.putUChar("mbid", c.modbus_slave_id);
  prefs.putChar("mboff", c.modbus_addr_offset);

  prefs.putULong("pollms", c.poll_interval_ms);
}

void config_factory_reset() {
  // wipe all keys in this namespace
  prefs.clear();
}
