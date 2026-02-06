# VentReader – Flexit Modbus Reader with ePaper UI

VentReader is an ESP32-based read-only gateway for Flexit ventilation systems
(Nordic S3 / S4), featuring:

- Local ePaper display
- Web-based admin & setup wizard
- Modbus RTU (RS-485) read-only integration
- Optional Homey integration
- OTA firmware updates
- Commercial-friendly provisioning flow

⚠️ This product is NOT affiliated with Flexit.  
The name “Flexit” is used only to describe supported equipment.

---

## Features

- ESP32-S3 based
- Waveshare / WeAct 4.2" ePaper display (SSD1683)
- Modbus RTU via MAX3485 (read-only)
- Setup wizard (3-step):
  1. Admin password
  2. WiFi credentials
  3. Feature & model selection
- Factory reset via BOOT button
- OTA firmware updates (after setup), including web upload in admin
- Web API for Homey / integrations

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

---

## Factory reset

Hold the **BOOT (GPIO0)** button during power-on for ~6 seconds:

- All NVS config erased
- Device reboots
- Returns to factory / manufacturing state

---

## Disclaimer

This project is intended for **read-only monitoring**.
No Modbus writes are performed.

Use at your own risk.
