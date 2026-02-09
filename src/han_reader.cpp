#include "han_reader.h"

static HardwareSerial HanSerial(1);
static String lineBuf;
static const char* g_last_error = "NO DATA";

static float parse_obis_value(const String& line)
{
  const int open = line.indexOf('(');
  const int close = line.indexOf('*', open + 1);
  if (open < 0) return NAN;
  const int end = (close > open) ? close : line.indexOf(')', open + 1);
  if (end <= open) return NAN;

  String num = line.substring(open + 1, end);
  num.trim();
  num.replace(',', '.');
  return num.toFloat();
}

static void parse_line(HanSnapshot& s, const String& line)
{
  if (line.startsWith("/"))
  {
    s.meter_id = line;
    return;
  }

  if (line.startsWith("1-0:32.7.0")) s.voltage_v[0] = parse_obis_value(line);
  else if (line.startsWith("1-0:52.7.0")) s.voltage_v[1] = parse_obis_value(line);
  else if (line.startsWith("1-0:72.7.0")) s.voltage_v[2] = parse_obis_value(line);
  else if (line.startsWith("1-0:31.7.0")) s.current_a[0] = parse_obis_value(line);
  else if (line.startsWith("1-0:51.7.0")) s.current_a[1] = parse_obis_value(line);
  else if (line.startsWith("1-0:71.7.0")) s.current_a[2] = parse_obis_value(line);
  else if (line.startsWith("1-0:21.7.0")) s.phase_power_w[0] = parse_obis_value(line) * 1000.0f;
  else if (line.startsWith("1-0:41.7.0")) s.phase_power_w[1] = parse_obis_value(line) * 1000.0f;
  else if (line.startsWith("1-0:61.7.0")) s.phase_power_w[2] = parse_obis_value(line) * 1000.0f;
  else if (line.startsWith("1-0:1.7.0")) s.import_power_w = parse_obis_value(line) * 1000.0f;
  else if (line.startsWith("1-0:2.7.0")) s.export_power_w = parse_obis_value(line) * 1000.0f;
  else if (line.startsWith("1-0:1.8.0")) s.import_energy_kwh_total = parse_obis_value(line);
  else if (line.startsWith("1-0:2.8.0")) s.export_energy_kwh_total = parse_obis_value(line);
}

void han_reader_begin(const DeviceConfig& cfg)
{
  lineBuf.reserve(160);
  HanSerial.begin(cfg.han_baud, SERIAL_8N1, cfg.han_rx_pin, cfg.han_tx_pin, cfg.han_invert);
}

bool han_reader_poll(HanSnapshot& snapshot)
{
  bool gotNew = false;

  while (HanSerial.available() > 0)
  {
    char c = static_cast<char>(HanSerial.read());
    if (c == '\r') continue;

    if (c == '\n')
    {
      String line = lineBuf;
      lineBuf = "";
      line.trim();
      if (line.length() == 0) continue;

      parse_line(snapshot, line);
      if (line == "!") gotNew = true;
      continue;
    }

    if (lineBuf.length() < 220) lineBuf += c;
  }

  if (!isnan(snapshot.import_power_w))
  {
    g_last_error = "OK";
    return true;
  }

  if (gotNew)
  {
    g_last_error = "TELEGRAM INCOMPLETE";
    return false;
  }

  g_last_error = "NO HAN TELEGRAM";
  return false;
}

const char* han_reader_last_error()
{
  return g_last_error;
}
