#pragma once

#include <Arduino.h>
#include "flexit_types.h"
#include "config_store.h"

// Legacy compatibility layer:
// FlexitWeb source has been replaced by local BACnet source.
// These symbols are kept so older sketch copies still compile.

void flexit_web_set_runtime_config(const DeviceConfig& cfg);
bool flexit_web_poll(FlexitData& out);
const char* flexit_web_last_error();
bool flexit_web_is_ready();
