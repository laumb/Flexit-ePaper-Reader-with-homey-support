# HAN Reader - Setup (EN)

## 1. Wiring

- Connect ESP32-S3 RX/TX through a suitable HAN meter interface/adapter.
- Firmware defaults:
  - RX: GPIO 44
  - TX: GPIO 43
  - baud: 115200 (editable in admin)

## 2. Initial setup

1. Boot device.
2. Join AP `HANReader-XXXXXX` (password: `hanreader`) if WiFi is not configured.
3. Open `http://192.168.4.1/admin`.
4. Set WiFi, admin password, and price zone (`NO1..NO5`).
5. Save.

## 3. Pricing/tariff

- Use live spot API + zone (`NO1..NO5`) or manual spot override.
- Choose a tariff profile (example) or `CUSTOM`.
- For `CUSTOM`, configure energy charge, capacity tiers, fixed monthly fee, taxes and VAT.

## 4. API

- `GET /status` with `Authorization: Bearer <token>`
- Dedicated Homey/HA endpoints: `/homey/status`, `/ha/status`.

## 5. Headless mode

Set `display_enabled` to `0` in admin to run without a physical ePaper panel.
