# HAN ePaper Reader (ESP32-S3)

Versjon: `0.1.0` (9. februar 2026)

Dette prosjektet er en ny HAN-strømmåler-leser inspirert av arkitektur fra tidligere ePaper-prosjekter (admin/web + API + headless/sleep), men bygget spesifikt for strømdata.

Mål:
- lese strømdata fra HAN-port (OBIS-linjer)
- hente spotpris per norsk prisområde (NO1..NO5)
- beregne total estimert strømpris (spot + nettleie + avgifter + mva)
- vise data elegant og strømvennlig på ePaper
- eksponere API for Homey og Home Assistant

## Funksjoner

- HAN-datakilde over UART (`Serial1`) med OBIS-parser
- Spotpris fra [hvakosterstrommen.no API](https://www.hvakosterstrommen.no/strompris-api)
- Manuell spotpris-overstyring i admin
- Fleksibel nettleiemotor:
  - energiledd dag/natt/helg
  - kapasitetsledd via trinn (`kW:NOK/mnd`)
  - fastledd, elavgift, Enova, mva
  - eksempelprofiler (`ELVIA_EXAMPLE`, `BKK_EXAMPLE`, `TENSIO_EXAMPLE`) + `CUSTOM`
- Dashboard på ePaper:
  - L1/L2/L3 i A og W
  - importeffekt nå
  - pris (spot/nett/total)
  - dag/måned/år-kWh
  - 24t søyler for fasefordeling
- Web/admin med Basic Auth
- Token-beskyttet API for main/Homey/HA
- Headless-modus (`display_enabled=0`) og skjerm-hibernate mellom refresh

## Viktig om nettleie i Norge

Nettleie varierer per nettselskap og består typisk av:
- energiledd (øre/kWh)
- kapasitetsledd (ofte trinnbasert etter effekt)
- faste månedsledd
- offentlige avgifter og mva

Denne firmware lar brukeren velge/tilpasse modell. Eksempelprofiler i koden er **startverdier**, ikke juridisk/faktisk fasit for alle kunder. Bruker må alltid kontrollere satser hos eget nettselskap.

### Kilder brukt i designet (sjekket 9. februar 2026)
- RME om ny nettleiemodell for husholdninger: [rme.no](https://www.rme.no/kunde/stromforbrukeren/ny-nettleiemodell-for-kunder-med-arsforbruk-under-100-000-kwh)
- NVE om strømstøtteordning og forbrukerinfo: [nve.no](https://www.nve.no/reguleringsmyndigheten/kunde/stroemkunde/stroemstoette-til-private-husholdninger/)
- Spotpris-API: [hvakosterstrommen.no](https://www.hvakosterstrommen.no/strompris-api)

## API

Alle token-endepunkter bruker `Authorization: Bearer <token>`.

- `GET /health` (uten auth)
- `GET /status` (main token)
- `GET /status/history?limit=24` (main token)
- `GET /homey/status` (homey token)
- `GET /ha/status` (ha token)

## Admin

- `GET /admin` (Basic Auth)
- `POST /admin/save`
- `POST /admin/refresh_now`
- `POST /admin/toggle_panic`
- `POST /admin/reboot`

Standard admin:
- bruker: `admin`
- passord: `hanreader`

## Første oppstart

1. Flashe firmware.
2. Enheten prøver lagret WiFi.
3. Ved manglende WiFi eller uferdig setup oppretter enheten AP `HANReader-XXXXXX` med passord `hanreader`.
4. Åpne `http://192.168.4.1/admin` (eller vist IP) og lagre innstillinger.

## Kompilering

Arduino IDE / ESP32 core.

Avhengigheter:
- ESP32 board package
- `GxEPD2`

## Støttede OBIS-felt (nå)

- Spenning: `1-0:32.7.0`, `52.7.0`, `72.7.0`
- Strøm: `1-0:31.7.0`, `51.7.0`, `71.7.0`
- Effekt per fase: `1-0:21.7.0`, `41.7.0`, `61.7.0`
- Import/eksport effekt: `1-0:1.7.0`, `2.7.0`
- Import/eksport energi total: `1-0:1.8.0`, `2.8.0`

## Status nå

Dette er første HAN-versjon (grunnmur). Neste naturlige steg:
- flere HAN-telegramvarianter (Aidon/Kaifa/Kamstrup forskjeller)
- mer presis tariffprofil-import per nettselskap
- bedre frontend-dashboard i admin (grafikk i nettleser)
- valgfri MQTT discovery mot Home Assistant
