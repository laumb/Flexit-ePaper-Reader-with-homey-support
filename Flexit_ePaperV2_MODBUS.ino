#include <WiFi.h>
#include <ArduinoOTA.h>
#include <time.h>
#include <math.h>

#include "flexit_types.h"
#include "ui_display.h"
#include "data_example.h"
#include "flexit_modbus.h"
#include "flexit_web.h"
#include "config_store.h"
#include "homey_http.h" // web portal + /status API

// Keep credentials/tokens out of source control

// WiFi credentials live in secrets.h

// =======================
// NTP / TIME
// =======================
static const char* NTP1 = "pool.ntp.org";
static const char* NTP2 = "time.cloudflare.com";
// Norway CET/CEST (DST auto)
static const char* TZ_INFO = "CET-1CEST,M3.5.0/2,M10.5.0/3";

// =======================
// PRODUCT IDENTITY (no trademarked brand name)
// =======================
// Visible device name for users (WiFi AP / mDNS). Keep this generic for commercialization.
static const char* PRODUCT_DEVICE_NAME = "vent-reader";     // used as hostname/mDNS base
static const char* PRODUCT_AP_PREFIX   = "Vent-reader";     // shown to user in WiFi list
static const char* PRODUCT_AP_PASS     = "ventreader";      // WPA2 (>=8 chars). Change in config later if desired.


static bool timeReady = false;

static void setupTimeNTP()
{
  configTime(0, 0, NTP1, NTP2);
  setenv("TZ", TZ_INFO, 1);
  tzset();

  tm t;
  for (int i = 0; i < 40; i++)
  {
    if (getLocalTime(&t, 250))
    {
      timeReady = true;
      Serial.println("NTP time OK");
      return;
    }
    delay(250);
  }
  Serial.println("NTP time NOT ready (yet)");
  timeReady = false;
}

static String nowHHMM()
{
  tm t;
  if (!getLocalTime(&t, 50)) return "--:--";
  char buf[6];
  snprintf(buf, sizeof(buf), "%02d:%02d", t.tm_hour, t.tm_min);
  return String(buf);
}


static String buildApSsid()
{
  // Example: Vent-reader-115 (last IP octet) is not available yet in AP, so use MAC suffix
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

static String ipLastOctetDot()
{
  if (WiFi.status() != WL_CONNECTED) return "";
  IPAddress ip = WiFi.localIP();
  char buf[8];
  snprintf(buf, sizeof(buf), ".%u", ip[3]);
  return String(buf);
}

static int calcEtaPercent(float uteluft, float tilluft, float avtrekk)
{
  float denom = (avtrekk - uteluft);
  if (fabsf(denom) < 0.01f) return 0;
  float eta = ((tilluft - uteluft) / denom) * 100.0f;
  if (eta < 0) eta = 0;
  if (eta > 100) eta = 100;
  return (int)lroundf(eta);
}

// =======================
// GLOBAL STATE
// =======================
static DeviceConfig cfg;
static FlexitData data;
static String mbStatus = "MB OFF";
static FlexitData lastGoodModbusData;
static bool hasLastGoodModbusData = false;
static FlexitData lastGoodWebData;
static bool hasLastGoodWebData = false;
static uint32_t lastFlexitWebPollMs = 0;

// UI refresh every 10 minutes
static uint32_t UI_REFRESH_MS = 10UL * 60UL * 1000UL;
static uint32_t lastRefresh = 0;
static String lastUiLanguageApplied = "";

static String modelLabel(const String& key)
{
  if (key == "S4") return "S4";
  if (key == "S2_EXP") return "S2 EXP";
  if (key == "S7_EXP") return "S7 EXP";
  if (key == "CL3_EXP") return "CL3 EXP";
  if (key == "CL4_EXP") return "CL4 EXP";
  return "S3";
}

static String normDataSource(const String& in)
{
  if (in == "FLEXITWEB") return "FLEXITWEB";
  return "MODBUS";
}

static bool useFlexitWebSource()
{
  return normDataSource(cfg.data_source) == "FLEXITWEB";
}

static uint32_t flexitWebPollIntervalMs()
{
  uint32_t m = cfg.flexitweb_poll_minutes;
  if (m < 5) m = 5;
  if (m > 60) m = 60;
  return m * 60UL * 1000UL;
}

static void updateCommonMeta()
{
  data.time = nowHHMM();
  data.wifi_status = (WiFi.status() == WL_CONNECTED) ? "OK" : "NO";
  data.ip = ipLastOctetDot();
  data.device_model = modelLabel(cfg.model);
}

static void applyRuntimeModbusConfig()
{
  FlexitModbusRuntimeConfig mcfg;
  mcfg.model = cfg.model;
  mcfg.baud = cfg.modbus_baud;
  mcfg.slave_id = cfg.modbus_slave_id;
  mcfg.addr_offset = cfg.modbus_addr_offset;
  mcfg.serial_format = cfg.modbus_serial_format;
  mcfg.transport_mode = cfg.modbus_transport_mode;
  flexit_modbus_set_runtime_config(mcfg);
}

static void clearModbusDataUnknown()
{
  data.uteluft = NAN;
  data.tilluft = NAN;
  data.avtrekk = NAN;
  data.avkast = NAN;
  data.fan_percent = 0;
  data.heat_element_percent = 0;
  data.mode = "N/A";
}

static void recomputeEfficiency()
{
  if (!isnan(data.uteluft) && !isnan(data.tilluft) && !isnan(data.avtrekk))
    data.efficiency_percent = calcEtaPercent(data.uteluft, data.tilluft, data.avtrekk);
  else
    data.efficiency_percent = 0;
}

static void updateDataFromModbusOrFallback()
{
  updateCommonMeta();

  flexit_modbus_set_enabled(cfg.modbus_enabled);
  applyRuntimeModbusConfig();

  if (!flexit_modbus_is_enabled())
  {
    mbStatus = "MB OFF";
    data_example_fill(data);
    data.device_model = cfg.model;
    hasLastGoodModbusData = false;
  }
  else
  {
    if (flexit_modbus_poll(data))
    {
      mbStatus = String("MB OK (") + flexit_modbus_active_map() + ")";
      lastGoodModbusData = data;
      hasLastGoodModbusData = true;
    }
    else
    {
      const char* err = flexit_modbus_last_error();
      if (hasLastGoodModbusData)
      {
        data = lastGoodModbusData;
        updateCommonMeta();
        mbStatus = String("MB ") + (err ? err : "ERR") + " (stale)";
      }
      else
      {
        clearModbusDataUnknown();
        updateCommonMeta();
        mbStatus = String("MB ") + (err ? err : "ERR");
      }
    }
  }
  recomputeEfficiency();
}

static void updateDataFromFlexitWeb(bool forceRead)
{
  updateCommonMeta();
  flexit_web_set_runtime_config(cfg);

  const uint32_t nowMs = millis();
  const uint32_t webIntMs = flexitWebPollIntervalMs();
  const bool shouldRead = forceRead || !hasLastGoodWebData || (nowMs - lastFlexitWebPollMs >= webIntMs);

  if (!shouldRead)
  {
    data = lastGoodWebData;
    updateCommonMeta();
    mbStatus = "WEB OK (cached)";
  }
  else if (flexit_web_poll(data))
  {
    mbStatus = "WEB OK";
    lastGoodWebData = data;
    hasLastGoodWebData = true;
    lastFlexitWebPollMs = nowMs;
  }
  else
  {
    const char* err = flexit_web_last_error();
    if (hasLastGoodWebData)
    {
      data = lastGoodWebData;
      updateCommonMeta();
      mbStatus = String("WEB ") + (err ? err : "ERR") + " (stale)";
    }
    else
    {
      clearModbusDataUnknown();
      updateCommonMeta();
      mbStatus = String("WEB ") + (err ? err : "ERR");
    }
  }

  recomputeEfficiency();
}

static void updateDataFromActiveSource(bool forceRead)
{
  if (useFlexitWebSource())
    updateDataFromFlexitWeb(forceRead);
  else
    updateDataFromModbusOrFallback();
}

void setup()
{
  Serial.begin(115200);
  delay(250);

  // ---- Factory reset via BOOT button (GPIO0) ----
  // Hold BOOT during power-on for ~6 seconds to wipe config and restore defaults.
  pinMode(0, INPUT_PULLUP);
  uint32_t t0 = millis();
  while (digitalRead(0) == LOW && millis() - t0 < 6000) { delay(10); }
  if (digitalRead(0) == LOW) {
    Serial.println("FACTORY RESET (BOOT held)");
    config_begin();
    config_factory_reset();
    delay(200);
    ESP.restart();
  }

  config_begin();
  cfg = config_load();

  const bool setup_done = cfg.setup_completed;

// TVUNGEN reset av e-paper etter flashing / cold boot
ui_epaper_hard_clear();

  ui_init();
  ui_set_language(cfg.ui_language);
  lastUiLanguageApplied = cfg.ui_language;

  // Apply poll interval from config
  UI_REFRESH_MS = cfg.poll_interval_ms;


  // ---- WiFi connect (STA) ----
  WiFi.setHostname(PRODUCT_DEVICE_NAME);
  bool sta_ok = false;
if (cfg.wifi_ssid.length() > 0)
{
  WiFi.mode(WIFI_STA);
  WiFi.begin(cfg.wifi_ssid.c_str(), cfg.wifi_pass.c_str());
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) delay(250);
  sta_ok = (WiFi.status() == WL_CONNECTED);
}

  // ---- Onboarding / Setup mode ----
  // If setup is not completed yet, ALWAYS start the provisioning AP and show the onboarding screen,
  // even if STA connects successfully (so Q/A can verify the customer-friendly info screen).
  if (!setup_done)
  {
    WiFi.mode(WIFI_AP_STA);
    String apSsid = buildApSsid();
    WiFi.softAP(apSsid.c_str(), PRODUCT_AP_PASS);

    // Prefer showing STA IP if connected, otherwise AP IP
    String ip = sta_ok ? WiFi.localIP().toString() : apIp();
    String url = String("http://") + ip + "/admin/setup";

    ui_render_onboarding(apSsid, PRODUCT_AP_PASS, ip, url);
  }
  else if (!sta_ok)
  {
    // setup done, but STA failed => keep AP as fallback for recovery
    WiFi.mode(WIFI_AP_STA);
    String apSsid = buildApSsid();
    WiFi.softAP(apSsid.c_str(), PRODUCT_AP_PASS);
  }

if (WiFi.status() == WL_CONNECTED)
  setupTimeNTP();

  // Enable OTA only when device is configured and has STA connectivity
  if (setup_done && sta_ok)
  {
    ArduinoOTA.setHostname(PRODUCT_DEVICE_NAME);
    ArduinoOTA.begin();
  }

// Start web portal (admin + /status API)
webportal_begin(cfg);

  updateDataFromActiveSource(true);

  // Only show dashboard after wizard is completed
  if (setup_done)
  {
    ui_set_language(cfg.ui_language);
    lastUiLanguageApplied = cfg.ui_language;
    ui_render(data, mbStatus);
  }
  webportal_set_data(data, mbStatus);

  lastRefresh = millis();
}

void loop()
{
  // Handle incoming HTTP requests for /status
  webportal_loop();

  // Arduino OTA (enabled when on network)
  ArduinoOTA.handle();

  // Apply language changes from admin immediately on ePaper, without waiting for poll interval.
  if (cfg.setup_completed && cfg.ui_language != lastUiLanguageApplied)
  {
    ui_set_language(cfg.ui_language);
    lastUiLanguageApplied = cfg.ui_language;
    ui_render(data, mbStatus);
  }

  // Force immediate source refresh after admin test/save changes.
  if (webportal_consume_refresh_request())
  {
    updateDataFromActiveSource(true);
    if (cfg.setup_completed)
    {
      ui_set_language(cfg.ui_language);
      lastUiLanguageApplied = cfg.ui_language;
      ui_render(data, mbStatus);
    }
    webportal_set_data(data, mbStatus);
    lastRefresh = millis();
  }

  if (millis() - lastRefresh >= UI_REFRESH_MS)
  {
    lastRefresh = millis();

    if (!timeReady && WiFi.status() == WL_CONNECTED)
      setupTimeNTP();

    updateDataFromActiveSource(false);
    if (cfg.setup_completed)
    {
      ui_set_language(cfg.ui_language);
      lastUiLanguageApplied = cfg.ui_language;
      ui_render(data, mbStatus);
    }

    // Optional: send to Homey (stub for now)
    // homey_post_status(data);
    webportal_set_data(data, mbStatus);

    // Optional push (not needed for read-only):
    // homey_post_status(data);
  }

  delay(50);
}
