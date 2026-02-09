# HAN Reader - Oppsett (NO)

## 1. Kabling

- ESP32-S3 RX/TX mot HAN-port adapter (nivåtilpasset for målerens HAN-grensesnitt).
- Standard i firmware:
  - RX: GPIO 44
  - TX: GPIO 43
  - baud: 115200 (kan endres i admin)

## 2. Førstegangsoppsett

1. Start enheten.
2. Koble til AP `HANReader-XXXXXX` (passord: `hanreader`) hvis enheten ikke er på WiFi.
3. Åpne `http://192.168.4.1/admin`.
4. Sett WiFi, admin-passord og ønsket strømprissone (`NO1..NO5`).
5. Lagre.

## 3. Pris/netteleie

- Velg spotpris API + sone (`NO1..NO5`) eller manuell spotpris.
- Velg tariffprofil (eksempel) eller `CUSTOM`.
- For `CUSTOM`: fyll ut energiledd, kapasitetsledd, fastledd, avgifter og mva.

## 4. API-bruk

- `GET /status` med `Authorization: Bearer <token>`
- Egen token-støtte for Homey (`/homey/status`) og HA (`/ha/status`).

## 5. Headless

Sett `display_enabled` til `0` i admin for å kjøre uten fysisk ePaper-panel.
