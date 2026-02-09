#pragma once

#include <Arduino.h>
#include "han_types.h"

void ui_init();
void ui_render(const HanSnapshot& s, const HourBar bars[24]);
void ui_render_onboarding(const String& ssid, const String& pass, const String& ip, const String& url);
void ui_epaper_hard_clear();
