# HAN ePaper Reader (ESP32-S3)

Version: `0.1.0` (February 9, 2026)

This is a new HAN power-reader firmware inspired by the VentReader architecture (ePaper + admin/web + API + headless/sleep), but rebuilt for electricity metering.

## Scope

- Read HAN data from meter (OBIS lines over UART)
- Fetch spot prices for Norwegian zones (`NO1..NO5`)
- Compute estimated total cost (`spot + grid tariff + taxes + VAT`)
- Show clean low-power dashboard on ePaper
- Provide token-protected API for Homey and Home Assistant

## Features

- HAN parser for common OBIS fields
- Live spot from [hvakosterstrommen API](https://www.hvakosterstrommen.no/strompris-api)
- Manual spot override from admin
- Flexible tariff engine:
  - day/night/weekend energy charge
  - capacity charge via configurable tiers (`kW:NOK/month`)
  - fixed monthly fee, electricity tax, Enova fee, VAT
  - profiles: `ELVIA_EXAMPLE`, `BKK_EXAMPLE`, `TENSIO_EXAMPLE`, `CUSTOM`
- ePaper dashboard:
  - per-phase current/power (L1/L2/L3)
  - current import power
  - spot/grid/total price
  - day/month/year energy
  - 24h bars
- Basic-auth admin panel
- Bearer-token API (`/status`, `/homey/status`, `/ha/status`)
- Display hibernate between refreshes (low-power behavior)

## Norway tariff note

Grid tariffs in Norway vary by DSO and customer profile. The firmware provides a configurable model and example presets, but users must verify current values with their own DSO.

References used for the model (checked February 9, 2026):
- [RME: New grid tariff model](https://www.rme.no/kunde/stromforbrukeren/ny-nettleiemodell-for-kunder-med-arsforbruk-under-100-000-kwh)
- [NVE: Household support context](https://www.nve.no/reguleringsmyndigheten/kunde/stroemkunde/stroemstoette-til-private-husholdninger/)
- [hvakosterstrommen API](https://www.hvakosterstrommen.no/strompris-api)

## API

All protected endpoints require:

`Authorization: Bearer <token>`

- `GET /health`
- `GET /status`
- `GET /status/history?limit=24`
- `GET /homey/status`
- `GET /ha/status`

## Admin

- `GET /admin`
- `POST /admin/save`
- `POST /admin/refresh_now`
- `POST /admin/toggle_panic`
- `POST /admin/reboot`

Default credentials:
- user: `admin`
- pass: `hanreader`

## Build

- Arduino IDE + ESP32 core
- Library: `GxEPD2`

## Implemented OBIS keys

- Voltage: `1-0:32.7.0`, `52.7.0`, `72.7.0`
- Current: `1-0:31.7.0`, `51.7.0`, `71.7.0`
- Per-phase power: `1-0:21.7.0`, `41.7.0`, `61.7.0`
- Import/export power: `1-0:1.7.0`, `2.7.0`
- Import/export total energy: `1-0:1.8.0`, `2.8.0`

## Next

- More HAN telegram variants and robust auto-detection
- DSO-specific tariff import helpers
- Richer browser dashboard
- Optional MQTT discovery for Home Assistant
