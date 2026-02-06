# VentReader – Brukerveiledning

VentReader er en liten enhet som leser data fra Flexit ventilasjonsanlegg
(Nordic S3 / S4) og viser informasjon på skjerm og i nettleser.

Enheten er kun lesende og kan ikke styre anlegget.

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
3. Velg modell og funksjoner (S3 / S4, Modbus, Homey)

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
