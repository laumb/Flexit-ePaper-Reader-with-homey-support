# Home Assistant Setup Guide (Step by Step)

This guide shows how to integrate VentReader with Home Assistant using local REST APIs.

You get:
- live values
- historical graphs
- optional write controls (mode + setpoint)

## 1) Prepare VentReader

In VentReader admin (`/admin`):
1. Enable `Home Assistant/API`.
2. Keep a strong API token.
3. Note VentReader local IP.

Test in browser:
- `http://<VENTREADER_IP>/ha/status?token=<TOKEN>&pretty=1`

## 2) Add REST sensors in Home Assistant

In `configuration.yaml`, add sensors:

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

Then create template sensors for graph-friendly entities:

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

Add entities card and history graph card using sensors above.

## 4) Optional write controls (mode + setpoint)

In VentReader admin (`/admin`), enable:
1. `Modbus`
2. `Enable remote control writes (experimental)`

Add REST commands:

```yaml
rest_command:
  ventreader_mode:
    url: "http://192.168.1.50/api/control/mode?token=REPLACE_TOKEN&mode={{ mode }}"
    method: POST

  ventreader_setpoint:
    url: "http://192.168.1.50/api/control/setpoint?token=REPLACE_TOKEN&profile={{ profile }}&value={{ value }}"
    method: POST
```

Add helpers (UI):
1. `input_select.vent_mode` with options `AWAY`, `HOME`, `HIGH`, `FIRE`
2. `input_number.vent_setpoint_home` (range e.g. 10-30)

Example automations:

```yaml
automation:
  - alias: VentReader apply mode
    trigger:
      - platform: state
        entity_id: input_select.vent_mode
    action:
      - service: rest_command.ventreader_mode
        data:
          mode: "{{ states('input_select.vent_mode') }}"

  - alias: VentReader apply home setpoint
    trigger:
      - platform: state
        entity_id: input_number.vent_setpoint_home
    action:
      - service: rest_command.ventreader_setpoint
        data:
          profile: home
          value: "{{ states('input_number.vent_setpoint_home') }}"
```

## 5) Notes

- API calls are local LAN only by default.
- Keep token private.
- If Modbus is disabled, write endpoints return conflict/error as expected.
