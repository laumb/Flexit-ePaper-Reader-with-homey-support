# VentReader – Brukerveiledning (v4.0.4)

VentReader er en liten enhet som leser data fra Flexit ventilasjonsanlegg
(Nordic S3 / S4 + utvalgte eksperimentelle modeller) og viser informasjon på skjerm og i nettleser.

Standardoppsett er kun lesing. Eksperimentell styring via Modbus-skriv kan aktiveres i admin.

## Changelog (kort)

### v4.0.4

- Ny native Home Assistant MQTT Discovery-integrasjon (standard MQTT entities, ingen custom HA-komponent).
- Nye HA MQTT-innstillinger i setup/admin: broker, auth, discovery-prefix, state-topic base og publiseringsintervall.
- Admin viser nå HA MQTT runtime-status, samtidig som eksisterende `/ha/*` REST-endepunkter fortsatt støttes.

### v4.0.3

- Inndata-moduler i setup/admin er flyttet til egen seksjon, separat fra API/integrasjoner.
- Avanserte Modbus-innstillinger vises nå kun når `Modbus` er valgt datakilde og aktivert.
- BACnet-innstillinger vises nå kun når `BACnet` er valgt datakilde.
- Lagt til stiplet gull-separator mellom inndata-blokker for tydeligere visuelt skille.

### v4.0.2

- `/status` returnerer nå komplett datasett uavhengig av aktiv datakilde (`MODBUS` eller `BACNET`).
- `pretty=1` inkluderer nå strukturert gruppering og lesbar `field_map`.
- Beholder legacy-alias (`time`, `modbus`) for bakoverkompatibilitet.

### v4.0.1

- Tidsvisning i display er tydeliggjort:
  - Klokke i toppfelt viser siste skjermrefresh.
  - `siste` under hvert datakort viser siste vellykkede dataoppdatering fra datakilde.
- Status-JSON inkluderer nå `data_time` som eksplisitt tid for siste dataoppdatering.

### v4.0.0

- Ny valgfri datakilde: `BACnet (lokal, kun lesing)` som alternativ til lokal Modbus.
- Nye BACnet-innstillinger i wizard/admin: enhets-IP, Device ID, objektmapping, polling `5-60 min`, test og autodiscover.
- Styringsskriving er nå kun tilgjengelig når datakilde er `Modbus`.
- Egne API-tokens for `main/control`, `Homey (/status)` og `Home Assistant (/ha/*)` + rotasjonsknapper i admin.
- `Modbus` er merket som eksperimentell datakilde. `BACnet` er produksjonsklar (read-only).

### v3.7.0

- Oppsettguide steg 3 krever nå eksplisitt valg av `Aktiver` eller `Deaktiver` for både `Homey/API` og `Home Assistant/API`.
- Ny Homey-eksport i admin: ferdig `.json`/`.txt` med script, mapping og endepunkter.
- Mobil-knapp for deling via e-post (`mailto`) med ferdig utfylt innhold.
- Forside og admin har tydeligere statuskort og bedre handlingslayout.

### v3.6.0

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
   samt datakilde (`Modbus (eksperimentell)` eller `BACnet`).

Når du trykker **Fullfør og restart**, lagres alt og enheten starter på nytt.

---

## Normal bruk

Når oppsett er fullført:

- Skjermen viser ventilasjonsdata
- Nettgrensesnitt er tilgjengelig på enhetens IP
- Enheten oppdaterer seg automatisk
- Aktiv datakilde vises i offentlig status/admin.

---

## BACnet lokal datakilde (valgfritt)

Brukes når du vil hente data uten Modbus-kabling.

Påkrevd:
- Enhets-IP (samme LAN/VLAN)
- BACnet Device ID
- BACnet polling-intervall (`5-60 min`)

Valgfritt:
- Objektmapping i avanserte felt
- Autodiscover for å finne IP + Device ID

---

## Lokal konsepttest i Safari

For en rask, separat testside (uten firmware/admin), åpne:

- `/Users/laumb/Documents/GitHub/Flexit-ePaper-Reader-with-homey-support/flexit_local_test.html`

Siden kan teste HTTP-baserte auth/data-endepunkter lokalt, men ikke BACnet/UDP direkte fra nettleser.

---

## Homey-oppsett

Full steg-for-steg guider:

- Norsk: `/Users/laumb/Documents/GitHub/Flexit-ePaper-Reader-with-homey-support/HOMEY_SETUP_NO.md`
- English: `/Users/laumb/Documents/GitHub/Flexit-ePaper-Reader-with-homey-support/HOMEY_SETUP.md`

## Home Assistant-oppsett

Full steg-for-steg guider:

- Norsk: `/Users/laumb/Documents/GitHub/Flexit-ePaper-Reader-with-homey-support/HOME_ASSISTANT_SETUP_NO.md`
- English: `/Users/laumb/Documents/GitHub/Flexit-ePaper-Reader-with-homey-support/HOME_ASSISTANT_SETUP.md`

Anbefalt metode er nå native MQTT Discovery i Home Assistant (ingen custom komponent).

## API-endepunkt

- Helse:
  - `GET /health`
- Status:
  - `GET /status?token=<HOMEY_TOKEN>`
  - `GET /ha/status?token=<HA_TOKEN>` (krever `Home Assistant/API` aktivert)
  - Inkluderer `ts_epoch_ms` og `ts_iso` i hver datapakke for tidsserier/grafer
  - Inkluderer `stale`-felt og `ha_mqtt_enabled` per datapakke
- Historikk:
  - `GET /status/history?token=<HOMEY_TOKEN>&limit=120`
  - `GET /ha/history?token=<HA_TOKEN>&limit=120`
  - `GET /status/history.csv?token=<HOMEY_TOKEN>&limit=120`
  - `GET /ha/history.csv?token=<HA_TOKEN>&limit=120`
- Diagnostikk:
  - `GET /status/diag?token=<HOMEY_TOKEN>`
  - `GET /status/storage?token=<HOMEY_TOKEN>`
- Styring (eksperimentelt, krever datakilde=`Modbus`, `Modbus` + `Control writes` aktivert):
  - `POST /api/control/mode?token=<MAIN_TOKEN>&mode=AWAY|HOME|HIGH|FIRE`
  - `POST /api/control/setpoint?token=<MAIN_TOKEN>&profile=home|away&value=18.5`

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
- Ingen data sendes til sky automatisk (lokal BACnet og lokale API-kall)

---

VentReader er ikke tilknyttet Flexit.
