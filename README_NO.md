# VentReader – Brukerveiledning (v3.6.0)

VentReader er en liten enhet som leser data fra Flexit ventilasjonsanlegg
(Nordic S3 / S4 + utvalgte eksperimentelle modeller) og viser informasjon på skjerm og i nettleser.

Standardoppsett er kun lesing. Eksperimentell styring via Modbus-skriv kan aktiveres i admin.

### v3.6.0 høydepunkter

- Språkvalg gjelder nå også admin-undersider og ePaper-tekster
- Dashboardets modusverdier oversettes nå etter valgt språk
- Status-JSON inkluderer nå tidsfeltene `ts_epoch_ms` og `ts_iso` for logging/grafer
- Ny lokal historikk-API for grafer (`/status/history`, `/ha/history`)
- Ny diagnostikk-API (`/status/diag`) med Modbus-kvalitetstellere
- Ny hurtigstyring i admin (modus + setpunkt) når kontrollskriving er aktivert
- Hurtigstyring i admin er nå språkstyrt
- Ny graf-side i admin (`/admin/graphs`) som viser lokale historikktrender
- Graf-siden har toggle per serie + CSV-eksport
- Ny storage-sjekk (`/status/storage`) for å verifisere avgrenset RAM-bruk
- Ny manual/changelog-side i admin (`/admin/manual`)
- Mer konsistente kvitteringssider i admin (lagre/restart/OTA)

---

## Første oppstart

Når enheten startes første gang:

- Skjermen viser oppsettsinformasjon
- Enheten starter sitt eget WiFi-nettverk
- Du må gjennom et oppsett i nettleser

---

## Koble til enheten

1. Koble mobilen eller PC til WiFi-nettverket som vises på skjermen
2. Bruk passordet som står på skjermen
3. Åpne nettleser og gå til IP-adressen som vises
   (du blir automatisk sendt til oppsett)

---

## Innlogging

Standard innlogging (kun første gang):

- Bruker: `admin`
- Passord: `ventreader`

Du må endre passordet under oppsett.

---

## Oppsett (wizard)

Oppsettet består av 3 steg:

1. Velg nytt admin-passord
2. Koble enheten til ditt WiFi
3. Velg modell og funksjoner (Modbus, Homey/API, Home Assistant/API)

Når du trykker **Fullfør og restart**, lagres alt og enheten starter på nytt.

---

## Normal bruk

Når oppsett er fullført:

- Skjermen viser ventilasjonsdata
- Nettgrensesnitt er tilgjengelig på enhetens IP
- Enheten oppdaterer seg automatisk

---

## Homey-oppsett

Full steg-for-steg guider:

- Norsk: `/Users/laumb/Documents/GitHub/Flexit-ePaper-Reader-with-homey-support/HOMEY_SETUP_NO.md`
- English: `/Users/laumb/Documents/GitHub/Flexit-ePaper-Reader-with-homey-support/HOMEY_SETUP.md`

## Home Assistant-oppsett

Full steg-for-steg guider:

- Norsk: `/Users/laumb/Documents/GitHub/Flexit-ePaper-Reader-with-homey-support/HOME_ASSISTANT_SETUP_NO.md`
- English: `/Users/laumb/Documents/GitHub/Flexit-ePaper-Reader-with-homey-support/HOME_ASSISTANT_SETUP.md`

## API-endepunkt

- Helse:
  - `GET /health`
- Status:
  - `GET /status?token=<TOKEN>`
  - `GET /ha/status?token=<TOKEN>` (krever `Home Assistant/API` aktivert)
  - Inkluderer `ts_epoch_ms` og `ts_iso` i hver datapakke for tidsserier/grafer
  - Inkluderer `stale`-felt per datapakke
- Historikk:
  - `GET /status/history?token=<TOKEN>&limit=120`
  - `GET /ha/history?token=<TOKEN>&limit=120`
  - `GET /status/history.csv?token=<TOKEN>&limit=120`
  - `GET /ha/history.csv?token=<TOKEN>&limit=120`
- Diagnostikk:
  - `GET /status/diag?token=<TOKEN>`
  - `GET /status/storage?token=<TOKEN>`
- Styring (eksperimentelt, krever både `Modbus` + `Control writes` aktivert):
  - `POST /api/control/mode?token=<TOKEN>&mode=AWAY|HOME|HIGH|FIRE`
  - `POST /api/control/setpoint?token=<TOKEN>&profile=home|away&value=18.5`

---

## OTA-oppdatering

To måter:

1. Nettleser-opplasting i admin (`/admin/ota`) med `.bin`-fil
2. Arduino OTA via nettverksport (kun når STA WiFi er oppe)

OTA sletter ikke konfigurasjon.

---

## Tilbakestille til fabrikkinnstillinger

1. Slå av strømmen
2. Hold inne **BOOT-knappen**
3. Slå på strømmen
4. Hold BOOT i ca. 6 sekunder

Alt slettes og enheten starter på nytt som ny.

---

## Sikkerhet

- Standardpassord brukes kun før oppsett
- Etter oppsett må eget passord brukes
- Ingen data sendes til sky automatisk

---

VentReader er ikke tilknyttet Flexit.
