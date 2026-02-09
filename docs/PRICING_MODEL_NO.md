# Pris- og nettleiemodell (NO)

Firmware beregner estimert totalpris for "neste kWh" slik:

`total = spot + nettleddsvariabel + (kapasitetsledd + fastledd)/forventet_mndforbruk`

Deretter kan mva legges på hele summen.

## Nettleddsvariabel

- energiledd (dag/natt/helg)
- elavgift
- Enova-paslag

## Kapasitetsledd

Kapasitetsledd velges fra trinnliste (`kW:NOK/mnd`) basert pa snitt av de 3 hoyeste registrerte timeeffektene i innevarende maned.

## Viktig

- Modell og satser varierer per nettselskap.
- Eksempelprofiler i firmware er startpunkt, ikke offisiell fasit.
- Bruker ma kontrollere satser i eget nettomrade.
