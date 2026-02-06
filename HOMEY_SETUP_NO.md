# Homey-oppsett (steg for steg)

Mål med denne guiden:
- Gjøre Homey-oppsett raskt og forutsigbart for sluttbruker
- Minimere manuell kopiering av script og mapping
- Sikre at alle nødvendige data følger med fra VentReader

## Quick start (anbefalt)

1. Verifiser i VentReader admin at `Homey/API` er aktivert.
2. Trykk **Eksporter Homey-oppsett**.
3. Mobil: bruk **Del fil (mobil)** og send fil til din e-post.
4. Desktop: last ned filen direkte.
5. Opprett virtuelle enheter i Homey etter mapping i filen.
6. Lim inn `homey_script_js` i HomeyScript.
7. Lag polling-flow (1-2 min).

## Må gjøres vs valgfritt

### Må gjøres
1. Aktiv `Homey/API`.
2. Korrekt token i URL/script.
3. Opprett nødvendige virtuelle enheter med riktige capability IDs.
4. Opprett flow som kjører script periodisk.

### Valgfritt
1. Alarm-enhet (`VentReader - Modbus Alarm`).
2. Status-tekst-enhet.
3. Kontrollskriving (modus/setpunkt).

## Anbefalt standardflyt: Eksportfil fra VentReader

For HomeyScript og tilhørende oppsett skal bruker få en knapp i VentReader admin:
- **Eksporter Homey-oppsett**

Knappen genererer en ferdig oppsettfil med:
1. VentReader-base URL (`http://<ip>`)
2. Token
3. Ferdig HomeyScript-mal
4. Mappingtabell for anbefalte virtuelle enheter/capabilities
5. Forslag til polling-flow
6. Valgfri kontrollseksjon (hvis styring er aktivert)

Distribusjon av fil:
1. Mobil/nettbrett: del/send til e-post.
2. Ikke-mobil enhet: last ned fil lokalt.

---

## 1) Klargjør VentReader

I VentReader admin (`/admin`):
1. Verifiser at `Homey/API` er aktivert.
2. Verifiser/oppdater API-token.
3. Bekreft lokal IP-adresse/hostname.

Hurtigtest:
- `http://<VENTREADER_IP>/status?token=<TOKEN>&pretty=1`

## 2) Eksporter Homey-oppsettfil

1. Åpne VentReader admin.
2. Trykk **Eksporter Homey-oppsett**.
3. Del eller last ned filen.

Anbefalt filinnhold:
- `homey_script_js`
- mapping for virtuelle enheter
- flow-notater

## 3) Installer nødvendige Homey-apper

Installer:
1. `HomeyScript`
2. En virtual devices-app som støtter numeriske/temperatur-sensorer med Insights

## 4) Opprett virtuelle enheter i Homey

Minimum:
1. `VentReader - Uteluft` (temperatur)
2. `VentReader - Tilluft` (temperatur)
3. `VentReader - Avtrekk` (temperatur)
4. `VentReader - Avkast` (temperatur)
5. `VentReader - Fan %` (prosent/tall)
6. `VentReader - Heat %` (prosent/tall)
7. `VentReader - Gjenvinning %` (prosent/tall)

Valgfritt:
1. `VentReader - Modbus Alarm`
2. `VentReader - Status tekst`

## 5) Importer script fra eksportfil

1. Kopier `homey_script_js` fra eksportfilen.
2. Opprett script, f.eks. `VentReader Poll`.
3. Lim inn script uten manuell omskriving av logikk.

## 6) Lag polling-flow i Homey

1. `Når`: Hvert 1-2 minutt
2. `Så`: Kjør script `VentReader Poll`

Valgfritt:
1. Push-varsel ved script-feil/alarm.

## 7) Verifiser drift

1. Verdier oppdateres i virtuelle enheter.
2. Insights-grafer bygges over tid.
3. Alarmen reagerer ved feil (hvis konfigurert).

## 8) Valgfri styring fra Homey (modus + setpunkt)

Krever i VentReader:
1. `Modbus`
2. `Enable remote control writes (experimental)`

Endepunkt:
1. `POST /api/control/mode?token=<TOKEN>&mode=AWAY|HOME|HIGH|FIRE`
2. `POST /api/control/setpoint?token=<TOKEN>&profile=home|away&value=18.5`

## 9) Feilsøking

- `401 missing/invalid token`: feil token i script/URL.
- `403 api disabled`: `Homey/API` er av.
- `403 control disabled`: `Enable remote control writes` er av.
- `409 modbus disabled`: `Modbus` er av ved skrivekall.
- `500 write ... failed`: Modbus-innstillinger/busproblem.
- Ingen oppdatering i Homey: sjekk flow-trigger og capability IDs.

## 10) Drift og sikkerhet

1. Bruk DHCP-reservasjon for stabil IP.
2. Hold Homey og VentReader på samme LAN/subnett.
3. Del token kun med betrodde lokale automasjoner.
4. Hold skrivestyring avskrudd når den ikke trengs.
