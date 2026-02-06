#include "flexit_modbus.h"

#if FLEXIT_MODBUS_ENABLED

#include <HardwareSerial.h>
#include <ModbusMaster.h>

static HardwareSerial& RS485Serial = Serial2;
static ModbusMaster node;

static bool started = false;
static bool g_enabled = true;
static const char* lastError = "OK";

static void preTransmission()
{
  digitalWrite(FLEXIT_RS485_EN, HIGH);
  delayMicroseconds(50);
}

static void postTransmission()
{
  delayMicroseconds(50);
  digitalWrite(FLEXIT_RS485_EN, LOW);
}

bool flexit_modbus_is_enabled() { return g_enabled; }
void flexit_modbus_set_enabled(bool enabled) { g_enabled = enabled; }
const char* flexit_modbus_last_error() { return lastError; }

bool flexit_modbus_begin()
{
  if (started) return true;

  pinMode(FLEXIT_RS485_EN, OUTPUT);
  digitalWrite(FLEXIT_RS485_EN, LOW); // listen by default

  RS485Serial.begin(FLEXIT_MODBUS_BAUD, SERIAL_8N1, FLEXIT_RS485_RX, FLEXIT_RS485_TX);

  node.begin(FLEXIT_MODBUS_ID, RS485Serial);
  node.preTransmission(preTransmission);
  node.postTransmission(postTransmission);

  started = true;
  lastError = "OK";
  return true;
}

static bool readHolding(uint16_t reg, uint16_t count, uint16_t* out)
{
  uint8_t result = node.readHoldingRegisters(reg, count);
  if (result != node.ku8MBSuccess) return false;
  for (uint16_t i = 0; i < count; i++) out[i] = node.getResponseBuffer(i);
  return true;
}

static bool readInput(uint16_t reg, uint16_t count, uint16_t* out)
{
  uint8_t result = node.readInputRegisters(reg, count);
  if (result != node.ku8MBSuccess) return false;
  for (uint16_t i = 0; i < count; i++) out[i] = node.getResponseBuffer(i);
  return true;
}

// Float32 big-endian word order (hi word first)
static float decodeFloat32_BE(uint16_t hi, uint16_t lo)
{
  union { uint32_t u32; float f; } u;
  u.u32 = ((uint32_t)hi << 16) | (uint32_t)lo;
  return u.f;
}

const char* flexit_mode_to_text(uint16_t v)
{
  switch (v)
  {
    case 1: return "OFF";
    case 2: return "AWAY";
    case 3: return "HOME";
    case 4: return "HIGH";
    case 5: return "FUME";
    case 6: return "FIRE";
    case 7: return "TMP HIGH";
    default: return "UNKNOWN";
  }
}

bool flexit_modbus_poll(FlexitData& data)
{
  if (!g_enabled) { lastError = "MB OFF"; return false; }

  if (!flexit_modbus_begin())
  {
    lastError = "BEGIN";
    return false;
  }

  const int off = FLEXIT_ADDR_OFFSET;
  uint16_t t[2];

  // Temps (Input, 2 regs)
  if (!readInput((uint16_t)(FLEXIT_REG_UTELUFT_IN + off), 2, t)) { lastError = "UTELUFT"; return false; }
  data.uteluft = decodeFloat32_BE(t[0], t[1]);

  if (!readInput((uint16_t)(FLEXIT_REG_TILLUFT_IN + off), 2, t)) { lastError = "TILLUFT"; return false; }
  data.tilluft = decodeFloat32_BE(t[0], t[1]);

  if (!readInput((uint16_t)(FLEXIT_REG_AVTREKK_IN + off), 2, t)) { lastError = "AVTREKK"; return false; }
  data.avtrekk = decodeFloat32_BE(t[0], t[1]);

  if (!readInput((uint16_t)(FLEXIT_REG_AVKAST_IN + off), 2, t)) { lastError = "AVKAST"; return false; }
  data.avkast = decodeFloat32_BE(t[0], t[1]);

  // FAN % (Holding, 2 regs)
  if (!readHolding((uint16_t)(FLEXIT_REG_FAN_PCT_HOLD + off), 2, t)) { lastError = "FAN"; return false; }
  float fanPct = decodeFloat32_BE(t[0], t[1]);
  if (fanPct < 0) fanPct = 0;
  if (fanPct > 100) fanPct = 100;
  data.fan_percent = (int)lroundf(fanPct);

  // HEAT % (Holding, 2 regs)
  if (!readHolding((uint16_t)(FLEXIT_REG_HEAT_PCT_HOLD + off), 2, t)) { lastError = "HEAT"; return false; }
  float heatPct = decodeFloat32_BE(t[0], t[1]);
  if (heatPct < 0) heatPct = 0;
  if (heatPct > 100) heatPct = 100;
  data.heat_element_percent = (int)lroundf(heatPct);

  // MODE (Input, 1 reg)
  uint16_t m = 0;
  if (!readInput((uint16_t)(FLEXIT_REG_MODE_IN + off), 1, &m)) { lastError = "MODE"; return false; }
  data.mode = flexit_mode_to_text(m);

  lastError = "OK";
  return true;
}

#else

static const char* lastError = "MODBUS DISABLED";

static bool g_enabled = false;
bool flexit_modbus_is_enabled() { return g_enabled; }
void flexit_modbus_set_enabled(bool enabled) { g_enabled = enabled; }
bool flexit_modbus_begin() { lastError = "MODBUS DISABLED"; return false; }
bool flexit_modbus_poll(FlexitData& data) { (void)data; lastError = "MODBUS DISABLED"; return false; }
const char* flexit_modbus_last_error() { return lastError; }
const char* flexit_mode_to_text(uint16_t v) { (void)v; return "DISABLED"; }

#endif
