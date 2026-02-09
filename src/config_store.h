#pragma once

#include <Arduino.h>
#include <Preferences.h>

struct TariffTier {
  float limit_kw;
  float monthly_nok;
};

struct DeviceConfig {
  String wifi_ssid;
  String wifi_pass;

  String admin_user;
  String admin_pass;
  bool admin_pass_changed;

  String api_token;
  String homey_api_token;
  String ha_api_token;
  bool token_generated;
  bool api_panic_stop;

  bool setup_completed;
  bool display_enabled;

  uint32_t poll_interval_ms;

  bool han_enabled;
  int han_rx_pin;
  int han_tx_pin;
  bool han_invert;
  uint32_t han_baud;

  String price_zone; // NO1..NO5
  bool price_api_enabled;
  bool manual_spot_enabled;
  float manual_spot_nok_kwh;

  String tariff_profile; // CUSTOM/ELVIA_EXAMPLE/BKK_EXAMPLE/TENSIO_EXAMPLE
  float tariff_energy_day_ore;
  float tariff_energy_night_ore;
  float tariff_energy_weekend_ore;
  int tariff_day_start_hour;
  int tariff_day_end_hour;
  float tariff_elavgift_ore;
  float tariff_enova_ore;
  float tariff_fixed_monthly_nok;
  float tariff_expected_monthly_kwh;
  bool tariff_include_vat;
  float tariff_vat_percent;
  String tariff_capacity_tiers; // "2:219,5:299,..."

  bool homey_enabled;
  bool ha_enabled;

  String ap_ssid() const;
};

void config_begin();
DeviceConfig config_load();
void config_save(const DeviceConfig& cfg);
void config_factory_reset();
String config_chip_suffix4();
void config_apply_tariff_profile(DeviceConfig& cfg, bool force);
