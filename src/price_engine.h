#pragma once

#include <Arduino.h>
#include "config_store.h"

struct SpotPriceResult {
  bool ok = false;
  float nok_per_kwh = NAN;
  String source = "manual";
  String message = "NO DATA";
};

SpotPriceResult price_engine_get_now(const DeviceConfig& cfg);
