# VentReader – Flexit Modbus Reader with ePaper UI (v3.6.0)

VentReader is an ESP32-based local gateway for Flexit ventilation systems
(Nordic S3 / S4 + selected experimental variants), featuring:

- Local ePaper display
- Web-based admin & setup wizard
- Modbus RTU (RS-485) integration
- Optional Homey and Home Assistant integration
- Optional experimental control writes (mode + setpoint)
- OTA firmware updates
- Commercial-friendly provisioning flow

⚠️ This product is NOT affiliated with Flexit.  
The name “Flexit” is used only to describe supported equipment.

---

## Features

- ESP32-S3 based
- Waveshare / WeAct 4.2" ePaper display (SSD1683)
- Modbus RTU via MAX3485
- Setup wizard (3-step):
  1. Admin password
  2. WiFi credentials
  3. Feature & model selection
- Language selector in admin/setup (`NO`, `DA`, `SV`, `FI`, `EN`, `UKR`)
- Flexit model selector:
  - `S3`
  - `S4`
  - `S2 (Experimental)`
  - `S7 (Experimental)`
  - `CL3 (Experimental)`
  - `CL4 (Experimental)`
- Factory reset via BOOT button
- OTA firmware updates (after setup), including web upload in admin
- Web API for Homey / Home Assistant / integrations

### v3.6.0 highlights

- Language selection now applies to admin sub-pages and ePaper texts
- Dashboard mode values are now translated per selected language
- Status JSON now includes machine-friendly timestamps (`ts_epoch_ms`, `ts_iso`) for logging/graph pipelines
- New local history API for graphing (`/status/history`, `/ha/history`)
- New diagnostics API (`/status/diag`) with Modbus quality counters
- New quick-control panel in admin (mode + setpoint) when control writes are enabled
- Quick-control panel is now language-aware
- New admin graphs page (`/admin/graphs`) showing local history trends
- Graph page includes per-series toggles + CSV export
- Storage safety endpoint added (`/status/storage`) to verify bounded RAM usage
- New admin manual/changelog page (`/admin/manual`)
- More consistent admin response pages (save/restart/OTA)

---

## Device lifecycle

### Manufacturing / Factory state
- All configuration erased (NVS empty)
- `setup_completed = false`
- Setup wizard shown on display
- Device starts WiFi AP
- Default admin credentials active

### End-user configured state
- Setup completed
- Admin password changed
- STA WiFi connected
- Dashboard shown on display
- OTA + API enabled

---

## Default credentials (factory only)

- Username: `admin`
- Password: `ventreader`
- WiFi AP password: `ventreader`

User is forced to change admin password during setup.

---

## WiFi behaviour

- If setup not completed:
  - Device ALWAYS starts AP
  - Onboarding screen is shown on ePaper
- If setup completed:
  - Device connects to STA
  - AP is only enabled as fallback

---

## OTA updates

Arduino OTA (IDE network upload) is enabled **only when**:
- Setup is completed
- STA WiFi is connected

Web OTA upload from admin (`/admin/ota`) is available when setup is completed and admin page is reachable.

OTA does not erase configuration.

---

## Homey setup

Step-by-step Homey guides:
- English: `/Users/laumb/Documents/GitHub/Flexit-ePaper-Reader-with-homey-support/HOMEY_SETUP.md`
- Norsk: `/Users/laumb/Documents/GitHub/Flexit-ePaper-Reader-with-homey-support/HOMEY_SETUP_NO.md`

## Home Assistant setup

Step-by-step Home Assistant guides:
- English: `/Users/laumb/Documents/GitHub/Flexit-ePaper-Reader-with-homey-support/HOME_ASSISTANT_SETUP.md`
- Norsk: `/Users/laumb/Documents/GitHub/Flexit-ePaper-Reader-with-homey-support/HOME_ASSISTANT_SETUP_NO.md`

## API endpoints

- Health:
  - `GET /health`
- Status:
  - `GET /status?token=<TOKEN>`
  - `GET /ha/status?token=<TOKEN>` (requires `Home Assistant/API` enabled)
  - Includes `ts_epoch_ms` and `ts_iso` in each payload for time-series use
  - Includes `stale` flag per sample
- History:
  - `GET /status/history?token=<TOKEN>&limit=120`
  - `GET /ha/history?token=<TOKEN>&limit=120`
  - `GET /status/history.csv?token=<TOKEN>&limit=120`
  - `GET /ha/history.csv?token=<TOKEN>&limit=120`
- Diagnostics:
  - `GET /status/diag?token=<TOKEN>`
  - `GET /status/storage?token=<TOKEN>`
- Control (experimental, requires both `Modbus` + `Control writes` enabled):
  - `POST /api/control/mode?token=<TOKEN>&mode=AWAY|HOME|HIGH|FIRE`
  - `POST /api/control/setpoint?token=<TOKEN>&profile=home|away&value=18.5`

---

## Factory reset

Hold the **BOOT (GPIO0)** button during power-on for ~6 seconds:

- All NVS config erased
- Device reboots
- Returns to factory / manufacturing state

---

## Disclaimer

Default behavior is monitoring/read-only.
Modbus writes are optional and disabled by default.

Use at your own risk.
