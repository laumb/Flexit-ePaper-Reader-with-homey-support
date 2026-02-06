#pragma once

#include <Arduino.h>
#include "flexit_types.h"
#include "config_store.h"

// Flexit cloud data source (read-only).
// Uses app credentials and cloud API endpoints to read current values.
void flexit_web_set_runtime_config(const DeviceConfig& cfg);
bool flexit_web_poll(FlexitData& out);
const char* flexit_web_last_error();
bool flexit_web_is_ready();

