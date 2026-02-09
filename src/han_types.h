#pragma once

#include <Arduino.h>

struct HanSnapshot {
  float voltage_v[3] = {NAN, NAN, NAN};
  float current_a[3] = {NAN, NAN, NAN};
  float phase_power_w[3] = {NAN, NAN, NAN};

  float import_power_w = NAN;
  float export_power_w = NAN;
  float import_energy_kwh_total = NAN;
  float export_energy_kwh_total = NAN;

  float day_energy_kwh = 0.0f;
  float month_energy_kwh = 0.0f;
  float year_energy_kwh = 0.0f;

  float price_spot_nok_kwh = NAN;
  float price_total_nok_kwh = NAN;
  float price_grid_nok_kwh = NAN;
  float selected_capacity_step_kw = NAN;
  float selected_capacity_step_nok_month = NAN;

  String meter_id = "N/A";
  String source = "HAN";
  String wifi_status = "NO";
  String ip_suffix = "";
  String data_time = "--:--";
  String refresh_time = "--:--";
  String zone = "NO1";
  bool stale = true;
};

struct HourBar {
  uint8_t hour = 0;
  float l1_w = 0.0f;
  float l2_w = 0.0f;
  float l3_w = 0.0f;
  float total_w = 0.0f;
  float kwh = 0.0f;
};
