#include "price_engine.h"

#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <time.h>

static int cached_day = -1;
static int cached_hour = -1;
static String cached_zone = "";
static float cached_price = NAN;

static int hour_from_iso(const String& iso)
{
  // 2026-02-09T13:00:00+01:00
  const int t = iso.indexOf('T');
  if (t < 0 || t + 3 >= static_cast<int>(iso.length())) return -1;
  return iso.substring(t + 1, t + 3).toInt();
}

static bool parse_current_hour_price(const String& payload, int currentHour, float& out)
{
  int pos = 0;
  while (true)
  {
    int ts = payload.indexOf("\"time_start\":\"", pos);
    if (ts < 0) break;
    int te = payload.indexOf('"', ts + 14);
    if (te < 0) break;

    String iso = payload.substring(ts + 14, te);
    int h = hour_from_iso(iso);

    int ps = payload.indexOf("\"NOK_per_kWh\":", te);
    if (ps < 0) break;
    int pe = payload.indexOf(',', ps);
    if (pe < 0) pe = payload.indexOf('}', ps);
    if (pe < 0) break;

    String val = payload.substring(ps + 14, pe);
    val.trim();

    if (h == currentHour)
    {
      out = val.toFloat();
      return true;
    }

    pos = pe + 1;
  }

  return false;
}

SpotPriceResult price_engine_get_now(const DeviceConfig& cfg)
{
  SpotPriceResult r;

  if (cfg.manual_spot_enabled)
  {
    r.ok = true;
    r.nok_per_kwh = cfg.manual_spot_nok_kwh;
    r.source = "manual_override";
    r.message = "Manual spot override";
    return r;
  }

  if (!cfg.price_api_enabled)
  {
    r.ok = false;
    r.message = "Price API disabled";
    return r;
  }

  if (WiFi.status() != WL_CONNECTED)
  {
    r.ok = false;
    r.message = "WiFi disconnected";
    return r;
  }

  tm t;
  if (!getLocalTime(&t, 200))
  {
    r.ok = false;
    r.message = "Time not synced";
    return r;
  }

  if (cached_day == t.tm_mday && cached_hour == t.tm_hour && cached_zone == cfg.price_zone && !isnan(cached_price))
  {
    r.ok = true;
    r.nok_per_kwh = cached_price;
    r.source = "hvakosterstrommen_cache";
    r.message = "Cached";
    return r;
  }

  char path[128];
  snprintf(path, sizeof(path), "https://www.hvakosterstrommen.no/api/v1/prices/%04d/%02d-%02d_%s.json",
           t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, cfg.price_zone.c_str());

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  if (!http.begin(client, path))
  {
    r.ok = false;
    r.message = "HTTP begin failed";
    return r;
  }

  http.setTimeout(7000);
  const int code = http.GET();
  if (code != 200)
  {
    http.end();
    r.ok = false;
    r.message = String("HTTP ") + code;
    return r;
  }

  String payload = http.getString();
  http.end();

  float p = NAN;
  if (!parse_current_hour_price(payload, t.tm_hour, p))
  {
    r.ok = false;
    r.message = "No current-hour price in payload";
    return r;
  }

  cached_day = t.tm_mday;
  cached_hour = t.tm_hour;
  cached_zone = cfg.price_zone;
  cached_price = p;

  r.ok = true;
  r.nok_per_kwh = p;
  r.source = "hvakosterstrommen";
  r.message = "Live";
  return r;
}
