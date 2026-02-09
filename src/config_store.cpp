#include "config_store.h"

static Preferences prefs;

static String gen_token(size_t bytes_len = 16)
{
  String out;
  out.reserve(bytes_len * 2);
  const char* hex = "0123456789abcdef";
  for (size_t i = 0; i < bytes_len; ++i)
  {
    uint8_t b = static_cast<uint8_t>(esp_random() & 0xFF);
    out += hex[(b >> 4) & 0x0F];
    out += hex[b & 0x0F];
  }
  return out;
}

static String normalized_zone(const String& in)
{
  if (in == "NO1" || in == "NO2" || in == "NO3" || in == "NO4" || in == "NO5") return in;
  return "NO1";
}

static String normalized_tariff_profile(const String& in)
{
  if (in == "ELVIA_EXAMPLE" || in == "BKK_EXAMPLE" || in == "TENSIO_EXAMPLE" || in == "CUSTOM") return in;
  return "CUSTOM";
}

void config_begin()
{
  prefs.begin("hanreader", false);
}

static String chip_suffix4()
{
  uint16_t s = static_cast<uint16_t>(ESP.getEfuseMac() & 0xFFFF);
  char buf[5];
  snprintf(buf, sizeof(buf), "%04X", s);
  return String(buf);
}

String config_chip_suffix4()
{
  return chip_suffix4();
}

String DeviceConfig::ap_ssid() const
{
  return String("HANReader-") + chip_suffix4();
}

void config_apply_tariff_profile(DeviceConfig& cfg, bool force)
{
  if (!force && cfg.tariff_profile == "CUSTOM") return;

  if (cfg.tariff_profile == "ELVIA_EXAMPLE")
  {
    cfg.tariff_energy_day_ore = 39.0f;
    cfg.tariff_energy_night_ore = 31.0f;
    cfg.tariff_energy_weekend_ore = 31.0f;
    cfg.tariff_fixed_monthly_nok = 49.0f;
    cfg.tariff_capacity_tiers = "2:219,5:299,10:399,15:549,20:699,25:899,50:1499";
  }
  else if (cfg.tariff_profile == "BKK_EXAMPLE")
  {
    cfg.tariff_energy_day_ore = 42.0f;
    cfg.tariff_energy_night_ore = 34.0f;
    cfg.tariff_energy_weekend_ore = 34.0f;
    cfg.tariff_fixed_monthly_nok = 59.0f;
    cfg.tariff_capacity_tiers = "2:229,5:309,10:419,15:579,20:739,25:939,50:1549";
  }
  else if (cfg.tariff_profile == "TENSIO_EXAMPLE")
  {
    cfg.tariff_energy_day_ore = 37.0f;
    cfg.tariff_energy_night_ore = 29.0f;
    cfg.tariff_energy_weekend_ore = 29.0f;
    cfg.tariff_fixed_monthly_nok = 45.0f;
    cfg.tariff_capacity_tiers = "2:209,5:289,10:389,15:529,20:679,25:879,50:1449";
  }
}

DeviceConfig config_load()
{
  DeviceConfig cfg;

  cfg.wifi_ssid = prefs.getString("ssid", "");
  cfg.wifi_pass = prefs.getString("wpass", "");

  cfg.admin_user = prefs.getString("auser", "admin");
  cfg.admin_pass = prefs.getString("apass", "hanreader");
  cfg.admin_pass_changed = prefs.getBool("apchg", false);

  cfg.api_token = prefs.getString("token", "");
  cfg.homey_api_token = prefs.getString("htoken", "");
  cfg.ha_api_token = prefs.getString("hatoken", "");
  cfg.token_generated = prefs.getBool("tgen", false);
  cfg.api_panic_stop = prefs.getBool("apikill", false);

  if (!cfg.token_generated || cfg.api_token.length() < 16)
  {
    cfg.api_token = gen_token(16);
    cfg.token_generated = true;
    prefs.putString("token", cfg.api_token);
    prefs.putBool("tgen", true);
  }
  if (cfg.homey_api_token.length() < 16) cfg.homey_api_token = cfg.api_token;
  if (cfg.ha_api_token.length() < 16) cfg.ha_api_token = cfg.api_token;

  cfg.setup_completed = prefs.getBool("setup", false);
  cfg.display_enabled = prefs.getBool("disp", true);
  cfg.poll_interval_ms = prefs.getULong("pollms", 180000UL);
  if (cfg.poll_interval_ms < 180000UL) cfg.poll_interval_ms = 180000UL;

  cfg.han_enabled = prefs.getBool("hanon", true);
  cfg.han_rx_pin = prefs.getInt("hanrx", 44);
  cfg.han_tx_pin = prefs.getInt("hantx", 43);
  cfg.han_invert = prefs.getBool("haninv", false);
  cfg.han_baud = prefs.getUInt("hanbaud", 115200);

  cfg.price_zone = normalized_zone(prefs.getString("zone", "NO1"));
  cfg.price_api_enabled = prefs.getBool("papi", true);
  cfg.manual_spot_enabled = prefs.getBool("mspot", false);
  cfg.manual_spot_nok_kwh = prefs.getFloat("mspotv", 1.25f);

  cfg.tariff_profile = normalized_tariff_profile(prefs.getString("tprof", "CUSTOM"));
  cfg.tariff_energy_day_ore = prefs.getFloat("teday", 35.0f);
  cfg.tariff_energy_night_ore = prefs.getFloat("tenight", 28.0f);
  cfg.tariff_energy_weekend_ore = prefs.getFloat("teweek", 28.0f);
  cfg.tariff_day_start_hour = prefs.getInt("tdstart", 6);
  cfg.tariff_day_end_hour = prefs.getInt("tdend", 22);
  cfg.tariff_elavgift_ore = prefs.getFloat("telavg", 9.79f);
  cfg.tariff_enova_ore = prefs.getFloat("tenova", 1.0f);
  cfg.tariff_fixed_monthly_nok = prefs.getFloat("tfix", 45.0f);
  cfg.tariff_expected_monthly_kwh = prefs.getFloat("texpm", 900.0f);
  cfg.tariff_include_vat = prefs.getBool("tvaton", true);
  cfg.tariff_vat_percent = prefs.getFloat("tvat", 25.0f);
  cfg.tariff_capacity_tiers = prefs.getString("tcap", "2:199,5:279,10:379,15:519,20:669,25:869,50:1399");

  if (cfg.tariff_day_start_hour < 0) cfg.tariff_day_start_hour = 0;
  if (cfg.tariff_day_start_hour > 23) cfg.tariff_day_start_hour = 23;
  if (cfg.tariff_day_end_hour < 1) cfg.tariff_day_end_hour = 1;
  if (cfg.tariff_day_end_hour > 24) cfg.tariff_day_end_hour = 24;

  config_apply_tariff_profile(cfg, false);

  cfg.homey_enabled = prefs.getBool("homey", true);
  cfg.ha_enabled = prefs.getBool("ha", true);

  return cfg;
}

void config_save(const DeviceConfig& cfg)
{
  prefs.putString("ssid", cfg.wifi_ssid);
  prefs.putString("wpass", cfg.wifi_pass);

  prefs.putString("auser", cfg.admin_user);
  prefs.putString("apass", cfg.admin_pass);
  prefs.putBool("apchg", cfg.admin_pass_changed);

  prefs.putString("token", cfg.api_token);
  prefs.putString("htoken", cfg.homey_api_token);
  prefs.putString("hatoken", cfg.ha_api_token);
  prefs.putBool("tgen", cfg.token_generated);
  prefs.putBool("apikill", cfg.api_panic_stop);

  prefs.putBool("setup", cfg.setup_completed);
  prefs.putBool("disp", cfg.display_enabled);
  prefs.putULong("pollms", cfg.poll_interval_ms);

  prefs.putBool("hanon", cfg.han_enabled);
  prefs.putInt("hanrx", cfg.han_rx_pin);
  prefs.putInt("hantx", cfg.han_tx_pin);
  prefs.putBool("haninv", cfg.han_invert);
  prefs.putUInt("hanbaud", cfg.han_baud);

  prefs.putString("zone", normalized_zone(cfg.price_zone));
  prefs.putBool("papi", cfg.price_api_enabled);
  prefs.putBool("mspot", cfg.manual_spot_enabled);
  prefs.putFloat("mspotv", cfg.manual_spot_nok_kwh);

  prefs.putString("tprof", normalized_tariff_profile(cfg.tariff_profile));
  prefs.putFloat("teday", cfg.tariff_energy_day_ore);
  prefs.putFloat("tenight", cfg.tariff_energy_night_ore);
  prefs.putFloat("teweek", cfg.tariff_energy_weekend_ore);
  prefs.putInt("tdstart", cfg.tariff_day_start_hour);
  prefs.putInt("tdend", cfg.tariff_day_end_hour);
  prefs.putFloat("telavg", cfg.tariff_elavgift_ore);
  prefs.putFloat("tenova", cfg.tariff_enova_ore);
  prefs.putFloat("tfix", cfg.tariff_fixed_monthly_nok);
  prefs.putFloat("texpm", cfg.tariff_expected_monthly_kwh);
  prefs.putBool("tvaton", cfg.tariff_include_vat);
  prefs.putFloat("tvat", cfg.tariff_vat_percent);
  prefs.putString("tcap", cfg.tariff_capacity_tiers);

  prefs.putBool("homey", cfg.homey_enabled);
  prefs.putBool("ha", cfg.ha_enabled);
}

void config_factory_reset()
{
  prefs.clear();
}
