# Changelog

## 0.1.0 - 2026-02-09

- Rebuilt firmware from VentReader codebase into a HAN-focused product.
- Added HAN OBIS parser over UART (`Serial1`).
- Added spot price source for NO1-NO5 via hvakosterstrommen API.
- Added manual spot override.
- Added configurable Norwegian tariff engine:
  - day/night/weekend energy component
  - capacity tiers
  - fixed monthly component
  - taxes and VAT support
- Added low-power ePaper dashboard with per-phase metrics and 24h bars.
- Added admin UI, token auth API, Homey/HA dedicated status endpoints.
- Removed legacy ventilation-source modules from active firmware.
