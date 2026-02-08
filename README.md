# VentReader – Flexit Modbus Reader with ePaper UI (v4.2.6)

VentReader is an ESP32-based local gateway for Flexit ventilation systems (Nordic S3 / S4 + selected experimental models).
It provides local ePaper display, local web admin, and Homey/Home Assistant integrations over local APIs.

Default behavior is read-focused monitoring. Write control is optional and disabled by default.

## Changelog (short)

### v4.2.6
- Display header now uses compact model title (`NORDIC <model>`) and shows active setpoint (`SET xx.xC`) between model and clock.
- Added `set_temp` to `/status` and `pretty` JSON output (active setpoint from datasource, best-effort).
- Provisioning SSID prefix is now `Ventreader` (no hyphen in name prefix).
- Added experimental BACnet `Write probe` button in setup/admin to test mode/setpoint write capability without saving changes.

### v4.2.4
- Onboarding/admin bugfix: BACnet settings can now be saved without requiring `Test BACnet` success first.
- BACnet validation remains available as a separate explicit test action after save.

### v4.2.3
- API authentication now uses `Authorization: Bearer <token>` (query token auth removed from API endpoints).
- Added API emergency stop in admin: instantly blocks all token-protected API calls until re-enabled.
- Added admin API preview endpoints (`/admin/api/preview` + `?pretty=1`) for quick browser inspection.
- Status payload now includes `api_panic_stop`.

### v4.2.2
- Internal codebase refactor for maintainability: shared control-write engine and shared quick-control UI renderer.
- Setup/admin/public pages now reuse common control logic, reducing duplication and future bug surface.
- BACnet object-scan debug path optimized to avoid heavy string building when debug logging is disabled.
- No intended functional behavior changes.

### v4.2.1
- Admin/settings now has a dedicated `Display` section with `Headless mode` + display update interval.
- Quick control was moved directly below status on both public page and admin page.
- First-time setup is now pre-auth (no auth prompt before setup is completed).

### v4.2.0
- Added experimental BACnet write support for mode and setpoint.
- Added BACnet write settings in setup/admin (`Enable BACnet writes`, setpoint object mapping for `home/away`).
- `/api/control/*` and admin quick control now write through the active data source (Modbus or BACnet).

### v4.1.0
- Added `Headless mode (no display mounted)` in setup wizard and admin.
- Firmware now skips all ePaper init/render calls when headless is enabled, so unit runs stable without physical display.
- Added clear one-click API shortcuts inside admin (`Vanlig API` + `Pretty API`).
- Added `display_enabled`/`headless` fields in status payload.

### v4.0.4
- Added native Home Assistant MQTT Discovery integration (standard MQTT entities, no custom HA component).
- Added HA MQTT settings in setup/admin: broker, auth, discovery prefix, state topic base, publish interval.
- Added runtime HA MQTT status in admin and full compatibility with existing `/ha/*` REST endpoints.

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
- New optional data source: `BACnet (local)` as an alternative to local Modbus.
- New BACnet settings in wizard/admin: device IP, Device ID, object mapping, polling `5-60 min`, test and autodiscover.
- At this version stage, control writes were limited to data source `Modbus`.
- Separate API tokens for `main/control`, `Homey (/status)`, and `Home Assistant (/ha/*)` plus rotate buttons in admin.
- `Modbus` is marked experimental. `BACnet` read path is production-ready.

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

## Headless quick start (no display mounted)

1. Power the unit.
2. Connect to AP `Ventreader-XXXXXX` (password `ventreader`).
3. Open `http://192.168.4.1/admin/setup`.
4. In setup step 3, enable `Headless mode (no display mounted)`.
5. Complete setup and reboot.

For already configured units, use:
- `http://<device-ip>/admin`
- Login: `admin` + your configured password

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
- `Headless mode (no display mounted)`: Enable if running without physical ePaper
- Data source:
  - `Modbus (experimental, local)`
  - `BACnet (local)` with separate polling interval (`5-60 min`)

Press **Complete & restart** to persist settings and reboot.

## Normal operation

After setup is complete:
- ePaper shows live values.
- Admin is available on device IP (`/admin`).
- API availability follows your module selections.
- Active data source is visible in public status/admin.

## BACnet local source (optional)

If you do not want Modbus wiring, select `BACnet (local)` as data source.

Required:
1. Flexit device IP (same LAN/VLAN).
2. BACnet Device ID.
3. BACnet polling interval (`5-60 min`).

Recommended:
1. Use **Autodiscover** in admin to prefill IP + Device ID.
2. Run **Test BACnet** before saving.
3. Verify/adjust BACnet object mapping in advanced fields.

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
2. Enable `HA MQTT Discovery (native)`.
3. Fill MQTT broker host/IP (+ optional auth), then save.
4. In Home Assistant, ensure MQTT integration is connected to the same broker.
5. Verify entities auto-appear in HA.
6. Optional fallback: use `/ha/status` REST endpoint if you prefer REST sensors.

## Mode/setpoint writes (optional)

Requires one of:
1. `Data source = Modbus`, `Modbus enabled`, and `Enable remote control writes (experimental)`.
2. `Data source = BACnet` and `Enable BACnet writes (experimental)`.

When enabled:
- Admin quick control becomes available.
- API write endpoints are active:
  - `POST /api/control/mode` + header `Authorization: Bearer <TOKEN>` + `mode=AWAY|HOME|HIGH|FIRE`
  - `POST /api/control/setpoint` + header `Authorization: Bearer <TOKEN>` + `profile=home|away&value=18.5`

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

- `401 missing/invalid bearer token`: wrong/missing `Authorization: Bearer ...`.
- `503 api emergency stop active`: API panic-stop is enabled in admin.
- `403 api disabled`: selected API module is disabled.
- `403 control disabled`: write control disabled.
- `409 modbus disabled`: Modbus is disabled for write operations.
- `500 write ... failed`: Modbus transport/settings or physical bus issue.
- Empty/stale values: check Modbus status, wiring, slave ID, baud.

## Homey setup

Detailed guides:
- English: `/Users/laumb/Documents/GitHub/Flexit-ePaper-Reader-with-homey-support/docs/HOMEY_SETUP.md`
- Norsk: `/Users/laumb/Documents/GitHub/Flexit-ePaper-Reader-with-homey-support/docs/HOMEY_SETUP_NO.md`

## Home Assistant setup

Detailed guides:
- English: `/Users/laumb/Documents/GitHub/Flexit-ePaper-Reader-with-homey-support/docs/HOME_ASSISTANT_SETUP.md`
- Norsk: `/Users/laumb/Documents/GitHub/Flexit-ePaper-Reader-with-homey-support/docs/HOME_ASSISTANT_SETUP_NO.md`

Recommended method is now native Home Assistant MQTT Discovery (no custom component).

## API endpoints

### Health
- `GET /health`

### Status
- `GET /status` with header `Authorization: Bearer <HOMEY_TOKEN>`
- `GET /ha/status` with header `Authorization: Bearer <HA_TOKEN>` (requires `Home Assistant/API` enabled)

Status payload includes:
- Temperatures: `uteluft`, `tilluft`, `avtrekk`, `avkast`
- Aggregates: `fan`, `heat`, `efficiency`
- Metadata: `mode`, `modbus`, `source_status`, `model`, `time`, `screen_time`, `data_time`, `ts_epoch_ms`, `ts_iso`, `stale`, `data_source`, `ha_mqtt_enabled`, `display_enabled`, `headless`, `api_panic_stop`

### History
- `GET /status/history?limit=120` with Bearer header
- `GET /ha/history?limit=120` with Bearer header
- `GET /status/history.csv?limit=120` with Bearer header
- `GET /ha/history.csv?limit=120` with Bearer header

### Diagnostics
- `GET /status/diag` with Bearer header
- `GET /status/storage` with Bearer header

### Control (experimental)
Writes through active data source:
- Modbus path requires `Modbus` + `Enable remote control writes`.
- BACnet path requires `Enable BACnet writes`.
- `POST /api/control/mode` with Bearer header and `mode=AWAY|HOME|HIGH|FIRE`
- `POST /api/control/setpoint` with Bearer header and `profile=home|away&value=18.5`

## OTA updates

1. Web upload in admin: `/admin/ota` with `.bin`.
2. Arduino OTA over network (STA WiFi required).

Configuration in NVS is preserved during normal OTA updates.

## Factory reset

Hold `BOOT` while powering on for ~6 seconds.

## Security

- Change default password on first boot.
- Share API bearer tokens only with trusted local integrations.
- Keep write control disabled unless required.

---

VentReader is not affiliated with Flexit.
