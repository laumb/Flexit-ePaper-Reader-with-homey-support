# Homey Setup Guide (Step by Step)

This guide shows how to integrate VentReader with Homey Pro and get both:
- current values in Homey devices
- history graphs (Insights)

The method below uses:
- VentReader `/status` API
- HomeyScript
- Virtual sensor devices in Homey (from a virtual devices app)

## 1) Prepare VentReader

In VentReader admin (`/admin`):
1. Enable `Homey/API`.
2. Keep a strong API token.
3. Note the local IP address.

Test in browser:
- `http://<VENTREADER_IP>/status?token=<TOKEN>&pretty=1`

You should see JSON with fields such as:
- `uteluft`, `tilluft`, `avtrekk`, `avkast`
- `fan`, `heat`, `efficiency`
- `mode`, `modbus`, `model`, `time`

## 2) Install apps on Homey Pro

Install:
1. `HomeyScript` (official Athom app)
2. A virtual devices app (any app that can create numeric/temperature sensors with Insights)

## 3) Create virtual devices in Homey

Create one virtual sensor per metric you want to graph, for example:
1. `VentReader - Uteluft` (temperature capability)
2. `VentReader - Tilluft` (temperature capability)
3. `VentReader - Avtrekk` (temperature capability)
4. `VentReader - Avkast` (temperature capability)
5. `VentReader - Fan %` (percentage/number capability)
6. `VentReader - Heat %` (percentage/number capability)
7. `VentReader - Gjenvinning %` (percentage/number capability)
8. `VentReader - Modbus Alarm` (boolean/alarm capability, optional)
9. `VentReader - Status tekst` (text capability, optional)

Important:
- Use device capabilities that support Insights history (number/temperature/percentage).

## 4) Create HomeyScript

Create a script in HomeyScript, e.g. `VentReader Poll`.

Paste and edit this script:

```javascript
// VentReader -> Homey virtual devices
const VENTREADER_URL = 'http://192.168.1.50/status?token=REPLACE_TOKEN';

// Map: Homey device name -> capability id
const MAP = {
  'VentReader - Uteluft': { field: 'uteluft', cap: 'measure_temperature' },
  'VentReader - Tilluft': { field: 'tilluft', cap: 'measure_temperature' },
  'VentReader - Avtrekk': { field: 'avtrekk', cap: 'measure_temperature' },
  'VentReader - Avkast':  { field: 'avkast',  cap: 'measure_temperature' },
  'VentReader - Fan %':   { field: 'fan', cap: 'measure_percentage' },
  'VentReader - Heat %':  { field: 'heat', cap: 'measure_percentage' },
  'VentReader - Gjenvinning %': { field: 'efficiency', cap: 'measure_percentage' },
};

const ALARM_DEVICE = 'VentReader - Modbus Alarm'; // optional
const ALARM_CAP = 'alarm_generic'; // adjust if your virtual app uses another id

const STATUS_DEVICE = 'VentReader - Status tekst'; // optional
const STATUS_CAP = 'measure_text'; // adjust for your virtual app

function num(v) {
  if (v === null || v === undefined) return null;
  const n = Number(v);
  return Number.isFinite(n) ? n : null;
}

async function setByName(devices, name, capability, value) {
  const d = Object.values(devices).find(x => x.name === name);
  if (!d) return;
  if (!d.capabilitiesObj || !d.capabilitiesObj[capability]) return;
  await d.setCapabilityValue(capability, value);
}

const res = await fetch(VENTREADER_URL);
if (!res.ok) throw new Error(`HTTP ${res.status}`);
const s = await res.json();

const devices = await Homey.devices.getDevices();

for (const [name, cfg] of Object.entries(MAP)) {
  const v = num(s[cfg.field]);
  if (v === null) continue; // keep previous value if missing
  await setByName(devices, name, cfg.cap, v);
}

// Optional alarm + status helpers
if (ALARM_DEVICE) {
  const modbusStr = String(s.modbus || '');
  const bad = !(modbusStr.startsWith('MB OK'));
  await setByName(devices, ALARM_DEVICE, ALARM_CAP, bad);
}

if (STATUS_DEVICE) {
  const status = `${s.model || 'N/A'} | ${s.mode || 'N/A'} | ${s.modbus || 'N/A'} | ${s.time || '--:--'}`;
  await setByName(devices, STATUS_DEVICE, STATUS_CAP, status);
}

return `VentReader OK: ${s.time || '--:--'} ${s.modbus || ''}`;
```

Notes:
- Some virtual device apps use different capability IDs. If needed, adjust `cap` names.
- Start with the 3-4 most important sensors first, then expand.

## 5) Create polling flow

Create a flow in Homey:
1. `When`: Every 1 minute (or 2-5 min if you prefer lower load)
2. `Then`: Run script `VentReader Poll`

Optional second flow:
1. `When`: Script failed (or use alarm virtual device trigger)
2. `Then`: Send push notification

## 6) Verify history graphs

After a few polling cycles:
1. Open each virtual sensor device in Homey.
2. Confirm value updates.
3. Confirm Insights graph is building.

## 7) Recommended intervals and reliability

Recommended:
- Poll every 1-2 minutes for smooth graphs.
- Keep VentReader poll interval at 30-120 seconds if you want responsive data.

Reliability tips:
1. Keep VentReader on fixed LAN IP (DHCP reservation).
2. Keep Homey and VentReader on same LAN/subnet.
3. If `modbus` field returns stale/error, use alarm flow to notify.

## 8) Alternative architecture (future)

For the smoothest user experience, build a dedicated Homey app (LAN driver) for VentReader:
- Pair by IP/token
- Native capabilities
- Built-in Insights and flow cards

This project currently provides a stable local JSON API that is ready for that next step.
