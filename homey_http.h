#pragma once
#include <Arduino.h>
#include "flexit_types.h"
#include "config_store.h"

// =========================================================
// WEB PORTAL (Product Admin + Homey/HA API)
// ---------------------------------------------------------
// - GET  /health -> ok (no auth)
// - GET  /status?token=... [&pretty=1] -> JSON (token required)
// - GET  /status/history?token=... [&limit=120] -> JSON history buffer
// - GET  /status/history.csv?token=... [&limit=120] -> CSV history export
// - GET  /status/diag?token=... -> JSON diagnostics counters
// - GET  /status/storage?token=... -> JSON storage/heap status
// - GET  /ha/status?token=... [&pretty=1] -> JSON (token required + HA enabled)
// - GET  /ha/history?token=... [&limit=120] -> JSON history buffer
// - GET  /ha/history.csv?token=... [&limit=120] -> CSV history export
// - POST /api/control/mode?token=...&mode=AWAY|HOME|HIGH|FIRE
// - POST /api/control/setpoint?token=...&profile=home|away&value=18.5
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
