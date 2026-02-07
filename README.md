# VentReader – Flexit Modbus Reader with ePaper UI (v4.0.3)

VentReader is an ESP32-based local gateway for Flexit ventilation systems (Nordic S3 / S4 + selected experimental models).
It provides local ePaper display, local web admin, and Homey/Home Assistant integrations over local APIs.

Default behavior is read-only monitoring. Modbus write control is optional and disabled by default.

## Changelog (short)

### v4.0.3
- Admin/setup input modules were split into a dedicated section separate from API/integration settings.
- Advanced Modbus settings now appear only when `Modbus` is the selected data source and enabled.
- BACnet settings now appear only when `BACnet` is the selected data source.
- Added dashed gold separators between datasource input blocks for clearer visual separation.

### v4.0.2
- `/status` now always returns a complete dataset independent of active datasource (`MODBUS` or `BACNET`).
- `pretty=1` now includes structured groups and a readable `field_map`.
- Kept legacy aliases (`time`, `modbus`) for backward compatibility.

### v4.0.1
- Display timing clarified:
  - Header clock now shows last ePaper refresh time.
  - Per-card `updated` timestamp now shows last successful datasource update.
- Added `data_time` to status payload for explicit last data-update timestamp.

### v4.0.0
- New optional data source: `BACnet (local, read-only)` as an alternative to local Modbus.
- New BACnet settings in wizard/admin: device IP, Device ID, object mapping, polling `5-60 min`, test and autodiscover.
- Control writes are now allowed only when data source is `Modbus`.
- Separate API tokens for `main/control`, `Homey (/status)`, and `Home Assistant (/ha/*)` plus rotate buttons in admin.
- `Modbus` is marked experimental. `BACnet` is production-ready (read-only).

### v3.7.0
- Setup wizard step 3 now requires explicit enable/disable selection for `Homey/API` and `Home Assistant/API`.
- Homey export added in admin: ready-to-use setup file with script/mapping/endpoints.
- Admin layout improved and public front page now shows clearer runtime/module status.

### v3.6.0
- New history/diagnostics endpoints (`/status/history`, `/status/diag`, `/status/storage`).
- New quick-control panel and graphs page in admin.
- Improved language coverage in admin and ePaper UI.

---

## First boot

On first start:
1. Device enters setup mode.
2. Device starts provisioning AP.
3. Setup must be completed in browser before normal operation.

## Connect to device

1. Join the WiFi network shown on the display.
2. Use password shown on the display.
3. Open the displayed IP in browser (auto-redirects to setup).

## Factory login

- User: `admin`
- Password: `ventreader`

Admin password must be changed during setup.

## Setup wizard

Wizard has 3 steps:
1. Admin password
2. WiFi
3. Model + modules

Step 3 now requires explicit selection for both:
- `Homey/API`: Enable or Disable
- `Home Assistant/API`: Enable or Disable
- Data source:
  - `Modbus (experimental, local)`
  - `BACnet (local, read-only)` with separate polling interval (`5-60 min`)

Press **Complete & restart** to persist settings and reboot.

## Normal operation

After setup is complete:
- ePaper shows live values.
- Admin is available on device IP (`/admin`).
- API availability follows your module selections.
- Active data source is visible in public status/admin.

## BACnet local source (optional)

If you do not want Modbus wiring, select `BACnet (local, read-only)` as data source.

Required:
1. Flexit device IP (same LAN/VLAN).
2. BACnet Device ID.
3. BACnet polling interval (`5-60 min`).

Recommended:
1. Use **Autodiscover** in admin to prefill IP + Device ID.
2. Run **Test BACnet** before saving.
3. Verify/adjust BACnet object mapping in advanced fields.

## Local concept test in Safari

For a quick standalone test page (outside firmware/admin), open:

- `/Users/laumb/Documents/GitHub/Flexit-ePaper-Reader-with-homey-support/flexit_local_test.html`

This page can test local HTTP auth/data endpoints, but cannot access BACnet/UDP directly from a browser.

## Quick start: Homey

1. In `/admin`, verify `Homey/API` is enabled.
2. Click **Export Homey setup**.
3. Mobile: use **Share file (mobile)** and send to your own email.
4. Desktop: download file directly.
5. In Homey, create virtual devices from mapping in export file.
6. Copy `homey_script_js` from export file into HomeyScript.
7. Create polling flow every 1-2 minutes.

## Quick start: Home Assistant

1. In `/admin`, verify `Home Assistant/API` is enabled.
2. Test `http://<VENTREADER_IP>/ha/status?token=<TOKEN>&pretty=1`.
3. Add REST sensor in `configuration.yaml`.
4. Add template sensors for temperatures/percentages.
5. Restart Home Assistant.
6. Verify values in Entities + History.

## Mode/setpoint writes (optional)

Requires all enabled in admin:
1. Data source = `Modbus`
2. `Modbus`
3. `Enable remote control writes (experimental)`

When enabled:
- Admin quick control becomes available.
- API write endpoints are active:
  - `POST /api/control/mode?token=<TOKEN>&mode=AWAY|HOME|HIGH|FIRE`
  - `POST /api/control/setpoint?token=<TOKEN>&profile=home|away&value=18.5`

## Required vs optional

### Required
1. Change default admin password.
2. Explicitly choose API modules in wizard.
3. Keep token secure.
4. Verify status endpoint before enabling automation.

### Optional
1. Modbus writes.
2. Alarm/notification flows.
3. Local history graphs and CSV exports.

## Troubleshooting (quick)

- `401 missing/invalid token`: wrong or missing token.
- `403 api disabled`: selected API module is disabled.
- `403 control disabled`: write control disabled.
- `409 modbus disabled`: Modbus is disabled for write operations.
- `500 write ... failed`: Modbus transport/settings or physical bus issue.
- Empty/stale values: check Modbus status, wiring, slave ID, baud.

## Homey setup

Detailed guides:
- English: `/Users/laumb/Documents/GitHub/Flexit-ePaper-Reader-with-homey-support/HOMEY_SETUP.md`
- Norsk: `/Users/laumb/Documents/GitHub/Flexit-ePaper-Reader-with-homey-support/HOMEY_SETUP_NO.md`

## Home Assistant setup

Detailed guides:
- English: `/Users/laumb/Documents/GitHub/Flexit-ePaper-Reader-with-homey-support/HOME_ASSISTANT_SETUP.md`
- Norsk: `/Users/laumb/Documents/GitHub/Flexit-ePaper-Reader-with-homey-support/HOME_ASSISTANT_SETUP_NO.md`

## API endpoints

### Health
- `GET /health`

### Status
- `GET /status?token=<HOMEY_TOKEN>`
- `GET /ha/status?token=<HA_TOKEN>` (requires `Home Assistant/API` enabled)

Status payload includes:
- Temperatures: `uteluft`, `tilluft`, `avtrekk`, `avkast`
- Aggregates: `fan`, `heat`, `efficiency`
- Metadata: `mode`, `modbus`, `source_status`, `model`, `time`, `screen_time`, `data_time`, `ts_epoch_ms`, `ts_iso`, `stale`, `data_source`

### History
- `GET /status/history?token=<HOMEY_TOKEN>&limit=120`
- `GET /ha/history?token=<HA_TOKEN>&limit=120`
- `GET /status/history.csv?token=<HOMEY_TOKEN>&limit=120`
- `GET /ha/history.csv?token=<HA_TOKEN>&limit=120`

### Diagnostics
- `GET /status/diag?token=<HOMEY_TOKEN>`
- `GET /status/storage?token=<HOMEY_TOKEN>`

### Control (experimental)
Requires data source `Modbus`, `Modbus` enabled, and `Enable remote control writes`:
- `POST /api/control/mode?token=<MAIN_TOKEN>&mode=AWAY|HOME|HIGH|FIRE`
- `POST /api/control/setpoint?token=<MAIN_TOKEN>&profile=home|away&value=18.5`

## OTA updates

1. Web upload in admin: `/admin/ota` with `.bin`.
2. Arduino OTA over network (STA WiFi required).

Configuration in NVS is preserved during normal OTA updates.

## Factory reset

Hold `BOOT` while powering on for ~6 seconds.

## Security

- Change default password on first boot.
- Share API token only with trusted local integrations.
- Keep write control disabled unless required.

---

VentReader is not affiliated with Flexit.
