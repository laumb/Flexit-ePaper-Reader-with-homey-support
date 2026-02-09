#pragma once

#include <Arduino.h>
#include "config_store.h"
#include "han_types.h"

void webportal_begin(DeviceConfig& cfg);
void webportal_loop();
void webportal_set_data(const HanSnapshot& data, const HourBar bars[24], float top3HourlyKw);
bool webportal_consume_refresh_request();

bool webportal_sta_connected();
bool webportal_ap_active();
