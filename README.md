# VentReader – Flexit Modbus Reader with ePaper UI (v4.0.0)

VentReader is an ESP32-based local gateway for Flexit ventilation systems (Nordic S3 / S4 + selected experimental models).
It provides local ePaper display, local web admin, and Homey/Home Assistant integrations over local APIs.

Default behavior is read-only monitoring. Modbus write control is optional and disabled by default.

## Changelog (short)

### v4.0.0
- New optional data source: `FlexitWeb Cloud` (read-only) as an alternative to local Modbus.
- New cloud settings in wizard/admin: login, optional serial, endpoint overrides, and polling `5-60 min`.
- Control writes are now allowed only when data source is `Modbus`.

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
  - `Modbus (local)`
  - `FlexitWeb Cloud (read-only)` with separate cloud polling interval (`5-60 min`)

Press **Complete & restart** to persist settings and reboot.

## Normal operation

After setup is complete:
- ePaper shows live values.
- Admin is available on device IP (`/admin`).
- API availability follows your module selections.
- Active data source is visible in public status/admin.

## FlexitWeb Cloud source (optional)

If you do not want Modbus wiring, select `FlexitWeb Cloud (read-only)` as data source.

Required:
1. Flexit app username/email.
2. Flexit app password.
3. Cloud polling interval (`5-60 min`).

Optional:
1. Device serial override (otherwise first cloud device is auto-discovered).
2. Endpoint override fields under advanced settings.

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
- `GET /status?token=<TOKEN>`
- `GET /ha/status?token=<TOKEN>` (requires `Home Assistant/API` enabled)

Status payload includes:
- Temperatures: `uteluft`, `tilluft`, `avtrekk`, `avkast`
- Aggregates: `fan`, `heat`, `efficiency`
- Metadata: `mode`, `modbus`, `model`, `time`, `ts_epoch_ms`, `ts_iso`, `stale`, `data_source`

### History
- `GET /status/history?token=<TOKEN>&limit=120`
- `GET /ha/history?token=<TOKEN>&limit=120`
- `GET /status/history.csv?token=<TOKEN>&limit=120`
- `GET /ha/history.csv?token=<TOKEN>&limit=120`

### Diagnostics
- `GET /status/diag?token=<TOKEN>`
- `GET /status/storage?token=<TOKEN>`

### Control (experimental)
Requires data source `Modbus`, `Modbus` enabled, and `Enable remote control writes`:
- `POST /api/control/mode?token=<TOKEN>&mode=AWAY|HOME|HIGH|FIRE`
- `POST /api/control/setpoint?token=<TOKEN>&profile=home|away&value=18.5`

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
