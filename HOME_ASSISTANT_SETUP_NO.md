# Home Assistant-oppsett (steg for steg)

Denne guiden viser hvordan du kobler VentReader til Home Assistant via lokale REST-API.

Du får:
- live verdier
- historiske grafer
- valgfri skrivestyring (modus + setpunkt)

## 1) Klargjør VentReader

I VentReader admin (`/admin`):
1. Aktiver `Home Assistant/API`.
2. Bruk en sterk API-token.
3. Noter lokal IP-adresse til VentReader.

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

Lag deretter templatesensorer for grafer:

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

Legg til entities-kort og historikk-grafkort for sensorene over.

## 4) Valgfri skrivestyring (modus + setpunkt)

I VentReader admin (`/admin`), aktiver:
1. `Modbus`
2. `Enable remote control writes (experimental)`

Legg til REST-kommandoer:

```yaml
rest_command:
  ventreader_mode:
    url: "http://192.168.1.50/api/control/mode?token=REPLACE_TOKEN&mode={{ mode }}"
    method: POST

  ventreader_setpoint:
    url: "http://192.168.1.50/api/control/setpoint?token=REPLACE_TOKEN&profile={{ profile }}&value={{ value }}"
    method: POST
```

Lag helpers i UI:
1. `input_select.vent_mode` med valg `AWAY`, `HOME`, `HIGH`, `FIRE`
2. `input_number.vent_setpoint_home` (f.eks. område 10-30)

Eksempel automasjoner:

```yaml
automation:
  - alias: VentReader sett modus
    trigger:
      - platform: state
        entity_id: input_select.vent_mode
    action:
      - service: rest_command.ventreader_mode
        data:
          mode: "{{ states('input_select.vent_mode') }}"

  - alias: VentReader sett home setpunkt
    trigger:
      - platform: state
        entity_id: input_number.vent_setpoint_home
    action:
      - service: rest_command.ventreader_setpoint
        data:
          profile: home
          value: "{{ states('input_number.vent_setpoint_home') }}"
```

## 5) Merknader

- API-kall er lokale (LAN) som standard.
- Hold token privat.
- Hvis Modbus er av, vil skrive-endepunkt returnere forventet feilstatus.
