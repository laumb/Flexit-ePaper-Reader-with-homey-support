# Home Assistant Setup Guide (Step by Step)

This guide explains how to integrate VentReader with Home Assistant via local REST APIs.

You get:
- live values
- history graphs
- optional write control (mode + setpoint)

## Quick start (recommended)

1. Verify `Home Assistant/API` is enabled in VentReader admin.
2. Test `http://<VENTREADER_IP>/ha/status?token=<TOKEN>&pretty=1`.
3. Add REST sensor in `configuration.yaml`.
4. Add template sensors.
5. Restart Home Assistant.
6. Verify values in Entities/History.

## Required vs optional

### Required
1. `Home Assistant/API` enabled.
2. Correct token in REST resource URL.
3. At least one REST sensor and relevant template sensors.

### Optional
1. REST commands for write control.
2. Automations for mode/setpoint.
3. Custom alert/notification logic.

## 1) Prepare VentReader

In VentReader admin (`/admin`):
1. Enable `Home Assistant/API`.
2. Keep a strong API token.
3. Note local IP address.

Test in browser:
- `http://<VENTREADER_IP>/ha/status?token=<TOKEN>&pretty=1`

## 2) Add REST sensor in Home Assistant

In `configuration.yaml`, add:

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

Template sensors:

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

Add entities and history graph cards for the created sensors.

## 4) Optional write control (mode + setpoint)

In VentReader admin (`/admin`), enable:
1. `Modbus`
2. `Enable remote control writes (experimental)`

REST commands:

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
1. `input_select.vent_mode` with options `AWAY`, `HOME`, `HIGH`, `FIRE`
2. `input_number.vent_setpoint_home` with range `10-30`

## 5) Troubleshooting

- `401 missing/invalid token`: wrong token in URL.
- `403 home assistant/api disabled`: `Home Assistant/API` is disabled.
- `403 control disabled`: write control disabled.
- `409 modbus disabled`: `Modbus` disabled for write calls.
- `500 write ... failed`: Modbus settings/transport/physical bus issue.
- No values in HA: verify resource URL, restart, and template entity names.

## 6) Notes

- API calls are local LAN by default.
- Keep token private.
- Keep write control disabled unless needed.
