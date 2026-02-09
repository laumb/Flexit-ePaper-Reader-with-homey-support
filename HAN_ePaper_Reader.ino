#include <WiFi.h>
#include <ArduinoOTA.h>
#include <time.h>

#include "src/han_types.h"
#include "src/config_store.h"
#include "src/han_reader.h"
#include "src/price_engine.h"
#include "src/tariff_engine.h"
#include "src/homey_http.h"
#include "src/ui_display.h"
#include "src/version.h"

#ifndef HANREADER_FORCE_HEADLESS
#define HANREADER_FORCE_HEADLESS 0
#endif

static const char* NTP1 = "pool.ntp.org";
static const char* NTP2 = "time.cloudflare.com";
static const char* TZ_INFO = "CET-1CEST,M3.5.0/2,M10.5.0/3";

static const char* PRODUCT_DEVICE_NAME = "han-reader";
static const char* PRODUCT_AP_PREFIX = "HANReader";
static const char* PRODUCT_AP_PASS = "hanreader";

static DeviceConfig cfg;
static HanSnapshot data;
static HourBar bars[24];

static uint32_t lastLoopSampleMs = 0;
static uint32_t lastRefreshMs = 0;
static uint32_t refreshMs = 180000UL;
static bool timeReady = false;

static int lastHour = -1;
static int lastDay = -1;
static int lastMonth = -1;
static int lastYear = -1;

static float hourEnergyKwh = 0.0f;
static float hourPowerL1Ws = 0.0f;
static float hourPowerL2Ws = 0.0f;
static float hourPowerL3Ws = 0.0f;
static float hourPowerTotWs = 0.0f;
static float hourSpanSeconds = 0.0f;

static float monthTop3Kw[3] = {0.0f, 0.0f, 0.0f};
static uint8_t currentBarHour = 0;

static bool displayActive()
{
  return cfg.display_enabled && HANREADER_FORCE_HEADLESS == 0;
}

static String nowHHMM()
{
  tm t;
  if (!getLocalTime(&t, 50)) return "--:--";
  char buf[6];
  snprintf(buf, sizeof(buf), "%02d:%02d", t.tm_hour, t.tm_min);
  return String(buf);
}

static String ipLastOctetDot()
{
  if (WiFi.status() != WL_CONNECTED) return "";
  IPAddress ip = WiFi.localIP();
  char buf[8];
  snprintf(buf, sizeof(buf), ".%u", ip[3]);
  return String(buf);
}

static String buildApSsid()
{
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char buf[32];
  snprintf(buf, sizeof(buf), "%s-%02X%02X%02X", PRODUCT_AP_PREFIX, mac[3], mac[4], mac[5]);
  return String(buf);
}

static String apIp()
{
  IPAddress ip = WiFi.softAPIP();
  char buf[16];
  snprintf(buf, sizeof(buf), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
  return String(buf);
}

static void setupTimeNTP()
{
  configTime(0, 0, NTP1, NTP2);
  setenv("TZ", TZ_INFO, 1);
  tzset();

  tm t;
  for (int i = 0; i < 40; ++i)
  {
    if (getLocalTime(&t, 250))
    {
      timeReady = true;
      return;
    }
    delay(250);
  }
  timeReady = false;
}

static void initBars()
{
  for (int i = 0; i < 24; ++i)
  {
    bars[i].hour = static_cast<uint8_t>(i);
    bars[i].l1_w = 0.0f;
    bars[i].l2_w = 0.0f;
    bars[i].l3_w = 0.0f;
    bars[i].total_w = 0.0f;
    bars[i].kwh = 0.0f;
  }
}

static void pushHourToBars(uint8_t hour, float avgL1, float avgL2, float avgL3, float avgTot, float kwh)
{
  for (int i = 0; i < 23; ++i) bars[i] = bars[i + 1];
  bars[23].hour = hour;
  bars[23].l1_w = avgL1;
  bars[23].l2_w = avgL2;
  bars[23].l3_w = avgL3;
  bars[23].total_w = avgTot;
  bars[23].kwh = kwh;
}

static void updateTop3(float kw)
{
  if (kw > monthTop3Kw[0])
  {
    monthTop3Kw[2] = monthTop3Kw[1];
    monthTop3Kw[1] = monthTop3Kw[0];
    monthTop3Kw[0] = kw;
  }
  else if (kw > monthTop3Kw[1])
  {
    monthTop3Kw[2] = monthTop3Kw[1];
    monthTop3Kw[1] = kw;
  }
  else if (kw > monthTop3Kw[2])
  {
    monthTop3Kw[2] = kw;
  }
}

static float top3AvgKw()
{
  int n = 0;
  float s = 0.0f;
  for (int i = 0; i < 3; ++i)
  {
    if (monthTop3Kw[i] > 0.01f)
    {
      s += monthTop3Kw[i];
      ++n;
    }
  }
  if (n == 0) return 0.0f;
  return s / static_cast<float>(n);
}

static bool isWeekend(const tm& t)
{
  return (t.tm_wday == 0 || t.tm_wday == 6);
}

static void finalizeHourIfNeeded(const tm& nowTm)
{
  if (lastHour < 0)
  {
    lastHour = nowTm.tm_hour;
    currentBarHour = static_cast<uint8_t>(nowTm.tm_hour);
    return;
  }

  if (nowTm.tm_hour == lastHour) return;

  float avgL1 = (hourSpanSeconds > 0.1f) ? (hourPowerL1Ws / hourSpanSeconds) : 0.0f;
  float avgL2 = (hourSpanSeconds > 0.1f) ? (hourPowerL2Ws / hourSpanSeconds) : 0.0f;
  float avgL3 = (hourSpanSeconds > 0.1f) ? (hourPowerL3Ws / hourSpanSeconds) : 0.0f;
  float avgTot = (hourSpanSeconds > 0.1f) ? (hourPowerTotWs / hourSpanSeconds) : 0.0f;

  pushHourToBars(currentBarHour, avgL1, avgL2, avgL3, avgTot, hourEnergyKwh);
  updateTop3(avgTot / 1000.0f);

  hourEnergyKwh = 0.0f;
  hourPowerL1Ws = 0.0f;
  hourPowerL2Ws = 0.0f;
  hourPowerL3Ws = 0.0f;
  hourPowerTotWs = 0.0f;
  hourSpanSeconds = 0.0f;

  lastHour = nowTm.tm_hour;
  currentBarHour = static_cast<uint8_t>(nowTm.tm_hour);
}

static void resetTimeBucketsIfNeeded(const tm& nowTm)
{
  if (lastDay < 0)
  {
    lastDay = nowTm.tm_mday;
    lastMonth = nowTm.tm_mon;
    lastYear = nowTm.tm_year;
    return;
  }

  if (nowTm.tm_mday != lastDay)
  {
    data.day_energy_kwh = 0.0f;
    lastDay = nowTm.tm_mday;
  }

  if (nowTm.tm_mon != lastMonth)
  {
    data.month_energy_kwh = 0.0f;
    monthTop3Kw[0] = monthTop3Kw[1] = monthTop3Kw[2] = 0.0f;
    lastMonth = nowTm.tm_mon;
  }

  if (nowTm.tm_year != lastYear)
  {
    data.year_energy_kwh = 0.0f;
    lastYear = nowTm.tm_year;
  }
}

static void applyEnergyIntegration(float dtSeconds)
{
  float importW = (data.stale || isnan(data.import_power_w)) ? 0.0f : max(data.import_power_w, 0.0f);
  float l1 = isnan(data.phase_power_w[0]) ? 0.0f : max(data.phase_power_w[0], 0.0f);
  float l2 = isnan(data.phase_power_w[1]) ? 0.0f : max(data.phase_power_w[1], 0.0f);
  float l3 = isnan(data.phase_power_w[2]) ? 0.0f : max(data.phase_power_w[2], 0.0f);

  float kwh = (importW * dtSeconds) / 3600000.0f;
  data.day_energy_kwh += kwh;
  data.month_energy_kwh += kwh;
  data.year_energy_kwh += kwh;

  hourEnergyKwh += kwh;
  hourPowerL1Ws += l1 * dtSeconds;
  hourPowerL2Ws += l2 * dtSeconds;
  hourPowerL3Ws += l3 * dtSeconds;
  hourPowerTotWs += importW * dtSeconds;
  hourSpanSeconds += dtSeconds;
}

static void updateMetadata()
{
  data.wifi_status = (WiFi.status() == WL_CONNECTED) ? "OK" : "NO";
  data.ip_suffix = ipLastOctetDot();
  data.zone = cfg.price_zone;
  data.refresh_time = nowHHMM();
}

static void updatePriceAndTariff(const tm& nowTm)
{
  SpotPriceResult spot = price_engine_get_now(cfg);
  if (spot.ok) data.price_spot_nok_kwh = spot.nok_per_kwh;

  float spotNow = isnan(data.price_spot_nok_kwh) ? 0.0f : data.price_spot_nok_kwh;
  TariffResult tariff = tariff_compute_now(cfg,
                                           spotNow,
                                           top3AvgKw(),
                                           isWeekend(nowTm),
                                           nowTm.tm_hour);
  data.price_grid_nok_kwh = tariff.grid_nok_kwh;
  data.price_total_nok_kwh = tariff.total_nok_kwh;
  data.selected_capacity_step_kw = tariff.selected_capacity_kw;
  data.selected_capacity_step_nok_month = tariff.selected_capacity_nok_month;
}

static void updateDataFromSources()
{
  HanSnapshot parsed = data;
  bool got = false;
  if (cfg.han_enabled)
  {
    got = han_reader_poll(parsed);
  }

  if (got)
  {
    data = parsed;
    data.stale = false;
    data.data_time = nowHHMM();
  }
  else
  {
    data.stale = true;
  }

  tm nowTm;
  if (getLocalTime(&nowTm, 20))
  {
    finalizeHourIfNeeded(nowTm);
    resetTimeBucketsIfNeeded(nowTm);
    updatePriceAndTariff(nowTm);
  }

  updateMetadata();
}

void setup()
{
  Serial.begin(115200);
  delay(200);

  config_begin();
  cfg = config_load();
  refreshMs = cfg.poll_interval_ms;

  initBars();

  if (displayActive())
  {
    ui_epaper_hard_clear();
    ui_init();
  }

  WiFi.setHostname(PRODUCT_DEVICE_NAME);
  bool staOk = false;

  if (cfg.wifi_ssid.length() > 0)
  {
    WiFi.mode(WIFI_STA);
    WiFi.begin(cfg.wifi_ssid.c_str(), cfg.wifi_pass.c_str());
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 20000UL) delay(250);
    staOk = WiFi.status() == WL_CONNECTED;
  }

  if (!cfg.setup_completed || !staOk)
  {
    WiFi.mode(WIFI_AP_STA);
    String ssid = buildApSsid();
    WiFi.softAP(ssid.c_str(), PRODUCT_AP_PASS);

    String ip = staOk ? WiFi.localIP().toString() : apIp();
    String url = String("http://") + ip + "/admin";
    if (displayActive()) ui_render_onboarding(ssid, PRODUCT_AP_PASS, ip, url);
  }

  if (WiFi.status() == WL_CONNECTED)
  {
    setupTimeNTP();
    ArduinoOTA.setHostname(PRODUCT_DEVICE_NAME);
    ArduinoOTA.begin();
  }

  han_reader_begin(cfg);
  webportal_begin(cfg);

  updateDataFromSources();
  webportal_set_data(data, bars, top3AvgKw());

  if (displayActive() && cfg.setup_completed)
  {
    ui_render(data, bars);
  }

  lastLoopSampleMs = millis();
  lastRefreshMs = millis();
}

void loop()
{
  webportal_loop();
  ArduinoOTA.handle();

  if (!timeReady && WiFi.status() == WL_CONNECTED) setupTimeNTP();

  uint32_t nowMs = millis();
  float dt = (nowMs >= lastLoopSampleMs) ? (nowMs - lastLoopSampleMs) / 1000.0f : 0.0f;
  lastLoopSampleMs = nowMs;

  updateDataFromSources();
  applyEnergyIntegration(dt);

  if (webportal_consume_refresh_request())
  {
    refreshMs = cfg.poll_interval_ms;
    if (refreshMs < 180000UL) refreshMs = 180000UL;
    han_reader_begin(cfg);
    updateDataFromSources();
    lastRefreshMs = 0;
  }

  webportal_set_data(data, bars, top3AvgKw());

  if (nowMs - lastRefreshMs >= refreshMs)
  {
    lastRefreshMs = nowMs;
    data.refresh_time = nowHHMM();

    if (displayActive() && cfg.setup_completed)
    {
      ui_render(data, bars);
    }
  }

  delay(50);
}
