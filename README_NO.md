# VentReader – Brukerveiledning (v3.7.0)

VentReader er en lokal gateway for Flexit ventilasjonsanlegg (Nordic S3/S4 + utvalgte eksperimentelle modeller).
Enheten viser data på ePaper-skjerm, tilbyr lokalt webgrensesnitt, og kan integreres med Homey/Home Assistant via lokalt API.

Standarddrift er lesing. Modbus-skriv (fjernstyring) er valgfritt og deaktivert som standard.

## Changelog (kort)

### v3.7.0
- Setup wizard steg 3 krever eksplisitt valg for `Homey/API` og `Home Assistant/API` (aktiver/deaktiver).
- Homey-eksport fra admin: ferdig oppsettfil med script/mapping/endepunkt.
- Forbedret admin-layout og mer statusinformasjon på offentlig forside.

### v3.6.0
- Historikk- og diagnostikkendepunkt lagt til (`/status/history`, `/status/diag`, `/status/storage`).
- Hurtigstyring og graf-side lagt til i admin (`/admin`).
- Utvidet språkstøtte i admin og ePaper.

---

## Første oppstart

Når enheten startes første gang:
1. Skjermen viser oppsettmodus med informasjon om lokal tilgang.
2. Enheten starter eget WiFi (provisioning AP).
3. Du fullfører oppsett i nettleser før normal drift aktiveres.

## Koble til enheten

1. Koble mobil/PC til WiFi-nettverket som vises på skjermen.
2. Bruk passordet som vises på skjermen.
3. Åpne nettleser på IP-adressen som vises (du sendes til setup).

## Innlogging (fabrikk)

- Bruker: `admin`
- Passord: `ventreader`

Du må endre passord i oppsettet før admin kan brukes normalt.

## Oppsett (wizard)

Wizard består av 3 steg:
1. Admin-passord
2. WiFi-tilkobling
3. Modell + moduler

I steg 3 må du velge eksplisitt for begge:
- `Homey/API`: Aktiver eller Deaktiver
- `Home Assistant/API`: Aktiver eller Deaktiver

Når du trykker **Fullfør og restart**, lagres konfigurasjonen og enheten restarter.

## Normal bruk etter oppsett

Når setup er fullført:
- Skjermen viser løpende ventilasjonsdata.
- Admin er tilgjengelig på enhetens IP (`/admin`).
- API er kun tilgjengelig i henhold til valgene du gjorde i setup/admin.

## Quick start: Homey (enkelt oppsett)

1. Gå til `/admin` og verifiser at `Homey/API` er aktivert.
2. Trykk **Eksporter Homey-oppsett** i admin.
3. Mobil: bruk **Del fil (mobil)** og send til din egen e-post.
4. Desktop: last ned filen direkte.
5. I Homey: opprett virtuelle enheter etter mapping i eksportfilen.
6. Kopier `homey_script_js` fra eksportfilen inn i HomeyScript.
7. Lag flow som kjører script hvert 1-2 minutt.

## Quick start: Home Assistant (enkelt oppsett)

1. Gå til `/admin` og verifiser at `Home Assistant/API` er aktivert.
2. Test `http://<VENTREADER_IP>/ha/status?token=<TOKEN>&pretty=1`.
3. Legg inn REST-sensor i `configuration.yaml`.
4. Legg inn templatesensorer for temperatur/prosent.
5. Restart Home Assistant.
6. Verifiser verdier i Entities + History.

## Skriving av modus og setpunkt (valgfritt)

Må aktiveres i admin:
1. `Modbus`
2. `Enable remote control writes (experimental)`

Når aktivert:
- Admin har **Hurtigstyring** (modus + setpunkt)
- API blir tilgjengelig:
  - `POST /api/control/mode?token=<TOKEN>&mode=AWAY|HOME|HIGH|FIRE`
  - `POST /api/control/setpoint?token=<TOKEN>&profile=home|away&value=18.5`

## Må gjøres vs valgfritt

### Må gjøres
1. Endre admin-passord ved første oppstart.
2. Velg API-moduler eksplisitt i wizard.
3. Sett/oppbevar token sikkert.
4. Bekreft at status-endepunkt svarer før integrasjon settes i drift.

### Valgfritt
1. Modbus-skriv (kontroll).
2. Alarm/feilflows i Homey/HA.
3. Historikk-grafer og CSV-eksport.

## Feilsøking (rask tabell)

- `401 missing/invalid token`: Feil token eller token mangler.
- `403 api disabled`: Modulen (`Homey/API` eller `Home Assistant/API`) er av.
- `403 control disabled`: `Enable remote control writes` er av.
- `409 modbus disabled`: `Modbus` er av når du prøver å skrive.
- `500 write ... failed`: Modbus-transport/innstillinger eller fysisk bus-problem.
- Tomme/stale verdier: sjekk Modbus-status, kabel, slave-ID og baud.

## Forenklet Homey-oppsett (anbefalt flyt)

For å redusere manuelt arbeid i Homey skal oppsett bruke eksportknapp i admin:
- Knapp: **Eksporter Homey-oppsett**
- Formål: Generere én ferdig fil med alle nødvendige data for HomeyScript/oppsett.

Eksporten inkluderer:
1. Enhetsinfo (`model`, lokal IP/host, tidspunkt)
2. API-endepunkt og token
3. Ferdig HomeyScript-mal
4. Mapping for anbefalte virtuelle enheter/capabilities
5. Anbefalt polling-intervall og feilhåndteringsforslag
6. Valgfri kontrollseksjon (hvis `Modbus` + control writes er aktivert)

## Homey-oppsett

Detaljguide:
- Norsk: `/Users/laumb/Documents/GitHub/Flexit-ePaper-Reader-with-homey-support/HOMEY_SETUP_NO.md`
- English: `/Users/laumb/Documents/GitHub/Flexit-ePaper-Reader-with-homey-support/HOMEY_SETUP.md`

## Home Assistant-oppsett

Detaljguide:
- Norsk: `/Users/laumb/Documents/GitHub/Flexit-ePaper-Reader-with-homey-support/HOME_ASSISTANT_SETUP_NO.md`
- English: `/Users/laumb/Documents/GitHub/Flexit-ePaper-Reader-with-homey-support/HOME_ASSISTANT_SETUP.md`

## API-endepunkter

### Helse
- `GET /health`

### Status
- `GET /status?token=<TOKEN>`
- `GET /ha/status?token=<TOKEN>` (krever `Home Assistant/API` aktivert)

Status-respons inkluderer blant annet:
- Temperaturer (`uteluft`, `tilluft`, `avtrekk`, `avkast`)
- Aggregater (`fan`, `heat`, `efficiency`)
- Metadata (`mode`, `modbus`, `model`, `time`, `ts_epoch_ms`, `ts_iso`, `stale`)

### Historikk
- `GET /status/history?token=<TOKEN>&limit=120`
- `GET /ha/history?token=<TOKEN>&limit=120`
- `GET /status/history.csv?token=<TOKEN>&limit=120`
- `GET /ha/history.csv?token=<TOKEN>&limit=120`

### Diagnostikk
- `GET /status/diag?token=<TOKEN>`
- `GET /status/storage?token=<TOKEN>`

### Styring (eksperimentelt)
Krever både `Modbus` og `Enable remote control writes` aktivert:
- `POST /api/control/mode?token=<TOKEN>&mode=AWAY|HOME|HIGH|FIRE`
- `POST /api/control/setpoint?token=<TOKEN>&profile=home|away&value=18.5`

## OTA-oppdatering

To metoder:
1. Web-opplasting i admin: `/admin/ota` med `.bin`
2. Arduino OTA over nettverk (kun når STA WiFi er aktiv)

Konfigurasjon i NVS beholdes ved normal OTA-oppdatering.

## Fabrikkreset

1. Kutt strøm.
2. Hold inne `BOOT`.
3. Slå på strøm.
4. Hold `BOOT` i ca. 6 sekunder.

Dette sletter konfigurasjon og setter enheten tilbake til førstegangsoppsett.

## Sikkerhet

- Endre fabrikkpassord ved første oppstart.
- Del API-token kun med lokale, betrodde integrasjoner.
- Hold Modbus-skriv deaktivert hvis du ikke trenger aktiv styring.
- Lås ned lokalnettet der enheten står.

---

VentReader er ikke tilknyttet Flexit.
