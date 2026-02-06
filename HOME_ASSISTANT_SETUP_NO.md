# Home Assistant-oppsett (steg for steg)

Denne guiden viser hvordan du kobler VentReader til Home Assistant via lokale REST-API.

Du får:
- live verdier
- historiske grafer
- valgfri skrivestyring (modus + setpunkt)

## Quick start (anbefalt)

1. Verifiser i VentReader admin at `Home Assistant/API` er aktivert.
2. Test `http://<VENTREADER_IP>/ha/status?token=<TOKEN>&pretty=1`.
3. Legg inn REST-sensor i `configuration.yaml`.
4. Legg inn templatesensorer.
5. Restart Home Assistant.
6. Sjekk Entities/History.

## Må gjøres vs valgfritt

### Må gjøres
1. Aktiv `Home Assistant/API`.
2. Korrekt token i `resource`-URL.
3. Minst én REST-sensor + relevante templatesensorer.

### Valgfritt
1. REST-kommandoer for skrivestyring.
2. Automasjoner for modus/setpunkt.
3. Egne alarm/varslingsregler.

## 1) Klargjør VentReader

I VentReader admin (`/admin`):
1. Aktiver `Home Assistant/API`.
2. Bruk en sterk API-token.
3. Noter lokal IP-adresse.

Test i nettleser:
- `http://<VENTREADER_IP>/ha/status?token=<TOKEN>&pretty=1`

## 2) Legg til REST-sensor i Home Assistant

I `configuration.yaml`, legg til:

```yaml
sensor:
  - platform: rest
    name: ventreader_status
    resource: "http://192.168.1.50/ha/status?token=REPLACE_TOKEN"
    scan_interval: 60
    value_template: "{{ value_json.mode }}"
    json_attributes:
      - uteluft
      - tilluft
      - avtrekk
      - avkast
      - fan
      - heat
      - efficiency
      - modbus
      - model
      - fw
      - time
```

Templatesensorer:

```yaml
template:
  - sensor:
      - name: "VentReader Uteluft"
        unit_of_measurement: "°C"
        device_class: temperature
        state_class: measurement
        state: "{{ state_attr('sensor.ventreader_status', 'uteluft') }}"

      - name: "VentReader Tilluft"
        unit_of_measurement: "°C"
        device_class: temperature
        state_class: measurement
        state: "{{ state_attr('sensor.ventreader_status', 'tilluft') }}"

      - name: "VentReader Avtrekk"
        unit_of_measurement: "°C"
        device_class: temperature
        state_class: measurement
        state: "{{ state_attr('sensor.ventreader_status', 'avtrekk') }}"

      - name: "VentReader Avkast"
        unit_of_measurement: "°C"
        device_class: temperature
        state_class: measurement
        state: "{{ state_attr('sensor.ventreader_status', 'avkast') }}"

      - name: "VentReader Fan"
        unit_of_measurement: "%"
        state_class: measurement
        state: "{{ state_attr('sensor.ventreader_status', 'fan') }}"

      - name: "VentReader Heat"
        unit_of_measurement: "%"
        state_class: measurement
        state: "{{ state_attr('sensor.ventreader_status', 'heat') }}"

      - name: "VentReader Efficiency"
        unit_of_measurement: "%"
        state_class: measurement
        state: "{{ state_attr('sensor.ventreader_status', 'efficiency') }}"
```

Restart Home Assistant.

## 3) Dashboard (Lovelace)

Legg til entities-kort og historikk-kort for sensorene.

## 4) Valgfri skrivestyring (modus + setpunkt)

I VentReader admin (`/admin`), aktiver:
1. `Modbus`
2. `Enable remote control writes (experimental)`

REST-kommandoer:

```yaml
rest_command:
  ventreader_mode:
    url: "http://192.168.1.50/api/control/mode?token=REPLACE_TOKEN&mode={{ mode }}"
    method: POST

  ventreader_setpoint:
    url: "http://192.168.1.50/api/control/setpoint?token=REPLACE_TOKEN&profile={{ profile }}&value={{ value }}"
    method: POST
```

Helpers:
1. `input_select.vent_mode` med `AWAY`, `HOME`, `HIGH`, `FIRE`
2. `input_number.vent_setpoint_home` (10-30)

## 5) Feilsøking

- `401 missing/invalid token`: feil token i URL.
- `403 home assistant/api disabled`: `Home Assistant/API` er av.
- `403 control disabled`: control writes er av.
- `409 modbus disabled`: `Modbus` er av.
- `500 write ... failed`: Modbus-transport/innstillinger eller fysisk busproblem.
- Ingen verdier i HA: sjekk `resource`-URL, restart, og template-navn.

## 6) Merknader

- API-kall er lokale (LAN) som standard.
- Hold token privat.
- Hold skrivestyring deaktivert når den ikke trengs.
