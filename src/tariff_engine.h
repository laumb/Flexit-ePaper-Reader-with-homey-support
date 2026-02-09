#pragma once

#include <Arduino.h>
#include "config_store.h"

struct TariffResult {
  float grid_nok_kwh = NAN;
  float total_nok_kwh = NAN;
  float selected_capacity_kw = NAN;
  float selected_capacity_nok_month = NAN;
};

float tariff_select_capacity_monthly_nok(const DeviceConfig& cfg, float top3_hourly_kw);
TariffResult tariff_compute_now(const DeviceConfig& cfg,
                                float spot_nok_kwh,
                                float top3_hourly_kw,
                                bool isWeekend,
                                int hourLocal);
