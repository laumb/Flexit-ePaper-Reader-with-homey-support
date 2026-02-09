#pragma once

#include <Arduino.h>
#include "han_types.h"
#include "config_store.h"

void han_reader_begin(const DeviceConfig& cfg);
bool han_reader_poll(HanSnapshot& snapshot);
const char* han_reader_last_error();
