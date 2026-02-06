#pragma once
#include <Arduino.h>
#include <Preferences.h>

struct DeviceConfig {
  String wifi_ssid;
  String wifi_pass;

  String api_token;     // required for /status
  String admin_user;    // default: admin
  String admin_pass;    // default: flexit123 (until changed)

  bool admin_pass_changed; // must be true before allowing normal admin actions

  bool token_generated; // set once after secure token is generated

  // Setup flow
  bool setup_completed; // true after wizard is finished

  // Ventilation unit model (affects Modbus register map)
  // Values: "S3" or "S4" (extend later)
  String model;

  bool modbus_enabled;
  bool homey_enabled;

  uint32_t poll_interval_ms; // data/UI refresh (default 10min)

  // computed helpers
  String ap_ssid() const; // Flexit-Setup-XXXX
};

void config_begin();
DeviceConfig config_load();
void config_save(const DeviceConfig& cfg);
void config_factory_reset();        // wipes config namespace
String config_chip_suffix4();       // last 4 hex of MAC (for SSID)
