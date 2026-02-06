#pragma once
#include <Arduino.h>
#include "flexit_types.h"
#include "config_store.h"

// =========================================================
// WEB PORTAL (Product Admin + Homey Read-only API)
// ---------------------------------------------------------
// - GET  /health -> ok (no auth)
// - GET  /status?token=... [&pretty=1] -> JSON (token required)
// - Admin UI (Basic Auth):
//     GET  /admin
//     GET  /admin/setup        (forced on first login until password changed)
//     POST /admin/setup_save
//     POST /admin/save         (normal settings)
//     POST /admin/reboot
//     POST /admin/factory_reset
//
// Fallback AP:
// - If STA WiFi can't connect, device starts AP:
//     SSID: Flexit-Setup-XXXX (XXXX = MAC suffix)
//     PASS: product AP password (default: ventreader)
//
// First login:
// - API token is auto-generated securely on first boot (stored in NVS).
// Default admin creds are same for all units (as requested):
//     user: admin
//     pass: ventreader
// - On first login, user must change password (forced setup screen).
//
// Factory reset:
// - Via admin UI button
// - Via BOOT button (GPIO0) hold at power-on (optional, implemented in .ino)
//
// =========================================================

void webportal_begin(DeviceConfig& cfg);
void webportal_loop();
void webportal_set_data(const FlexitData& data, const String& mbStatus);

// Helpers
bool webportal_sta_connected();
bool webportal_ap_active();
