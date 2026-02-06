#include "flexit_web.h"

#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ctype.h>

static DeviceConfig g_cfg_web;
static String g_last_error = "WEB OFF";
static String g_token;
static String g_cached_serial;
static uint32_t g_last_auth_ms = 0;

static String trimCopy(const String& in)
{
  String s = in;
  s.trim();
  return s;
}

static bool startsWithHttp(const String& s)
{
  return s.startsWith("http://") || s.startsWith("https://");
}

static int findKeyPos(const String& json, const String& key)
{
  int k = json.indexOf(String("\"") + key + "\"");
  if (k < 0) return -1;
  int c = json.indexOf(':', k);
  if (c < 0) return -1;
  return c + 1;
}

static String parseJsonScalar(const String& json, int startPos)
{
  if (startPos < 0 || startPos >= (int)json.length()) return "";
  int i = startPos;
  while (i < (int)json.length() && isspace((unsigned char)json[i])) i++;
  if (i >= (int)json.length()) return "";

  if (json[i] == '"')
  {
    i++;
    String out;
    out.reserve(32);
    bool esc = false;
    for (; i < (int)json.length(); i++)
    {
      char c = json[i];
      if (esc) { out += c; esc = false; continue; }
      if (c == '\\') { esc = true; continue; }
      if (c == '"') break;
      out += c;
    }
    return out;
  }

  int j = i;
  while (j < (int)json.length())
  {
    char c = json[j];
    if (c == ',' || c == '}' || c == ']' || isspace((unsigned char)c)) break;
    j++;
  }
  return trimCopy(json.substring(i, j));
}

static String jsonGetString(const String& json, const String& key)
{
  int p = findKeyPos(json, key);
  if (p < 0) return "";
  return parseJsonScalar(json, p);
}

static bool jsonGetDataPointRaw(const String& json, int pointId, String& out)
{
  String p1 = String("\"dataPointType\":") + String(pointId);
  String p2 = String("\"dataPointTypeId\":") + String(pointId);
  int p = json.indexOf(p1);
  if (p < 0) p = json.indexOf(p2);
  if (p < 0) return false;

  int v = json.indexOf("\"value\"", p);
  if (v < 0) return false;
  int c = json.indexOf(':', v);
  if (c < 0) return false;
  out = parseJsonScalar(json, c + 1);
  return out.length() > 0;
}

static bool jsonGetDataPointFloat(const String& json, int pointId, float& out)
{
  String raw;
  if (!jsonGetDataPointRaw(json, pointId, raw)) return false;
  raw.trim();
  if (raw.length() == 0) return false;
  out = raw.toFloat();
  return true;
}

static bool jsonGetDataPointInt(const String& json, int pointId, int& out)
{
  String raw;
  if (!jsonGetDataPointRaw(json, pointId, raw)) return false;
  raw.trim();
  if (raw.length() == 0) return false;
  out = raw.toInt();
  return true;
}

static bool jsonGetDataPointString(const String& json, int pointId, String& out)
{
  return jsonGetDataPointRaw(json, pointId, out);
}

static bool httpPostJson(const String& url, const String& body, String& resp, int& httpCode)
{
  WiFiClientSecure cli;
  cli.setInsecure();
  HTTPClient http;
  if (!http.begin(cli, url))
  {
    g_last_error = "WEB auth begin failed";
    return false;
  }
  http.addHeader("Content-Type", "application/json");
  httpCode = http.POST(body);
  resp = http.getString();
  http.end();
  return (httpCode > 0);
}

static bool httpGetAuth(const String& url, const String& bearer, String& resp, int& httpCode)
{
  WiFiClientSecure cli;
  cli.setInsecure();
  HTTPClient http;
  if (!http.begin(cli, url))
  {
    g_last_error = "WEB get begin failed";
    return false;
  }
  if (bearer.length() > 0) http.addHeader("Authorization", String("Bearer ") + bearer);
  httpCode = http.GET();
  resp = http.getString();
  http.end();
  return (httpCode > 0);
}

static bool authenticate()
{
  const String user = trimCopy(g_cfg_web.flexitweb_user);
  const String pass = g_cfg_web.flexitweb_pass;
  const String authUrl = trimCopy(g_cfg_web.flexitweb_auth_url);
  if (user.length() == 0 || pass.length() == 0)
  {
    g_last_error = "WEB missing credentials";
    return false;
  }
  if (!startsWithHttp(authUrl))
  {
    g_last_error = "WEB bad auth URL";
    return false;
  }

  String resp;
  int code = 0;

  String body = String("{\"Email\":\"") + user + "\",\"Password\":\"" + pass + "\"}";
  if (!httpPostJson(authUrl, body, resp, code))
    return false;
  if (code < 200 || code >= 300)
  {
    g_last_error = String("WEB auth HTTP ") + code;
    return false;
  }

  String token = jsonGetString(resp, "token");
  if (token.length() == 0) token = jsonGetString(resp, "accessToken");
  if (token.length() == 0) token = jsonGetString(resp, "access_token");
  if (token.length() == 0)
  {
    g_last_error = "WEB auth token missing";
    return false;
  }

  g_token = token;
  g_last_auth_ms = millis();
  return true;
}

static bool ensureToken()
{
  if (g_token.length() > 10)
  {
    uint32_t age = millis() - g_last_auth_ms;
    if (age < (50UL * 60UL * 1000UL)) return true;
  }
  return authenticate();
}

static bool ensureSerial()
{
  if (trimCopy(g_cfg_web.flexitweb_serial).length() > 0)
  {
    g_cached_serial = trimCopy(g_cfg_web.flexitweb_serial);
    return true;
  }
  if (g_cached_serial.length() > 0) return true;

  const String devUrl = trimCopy(g_cfg_web.flexitweb_device_url);
  if (!startsWithHttp(devUrl))
  {
    g_last_error = "WEB bad device URL";
    return false;
  }

  String resp;
  int code = 0;
  if (!httpGetAuth(devUrl, g_token, resp, code))
    return false;
  if (code == 401 || code == 403)
  {
    if (!authenticate()) return false;
    if (!httpGetAuth(devUrl, g_token, resp, code)) return false;
  }
  if (code < 200 || code >= 300)
  {
    g_last_error = String("WEB device HTTP ") + code;
    return false;
  }

  String serial = jsonGetString(resp, "serialNumber");
  if (serial.length() == 0)
  {
    g_last_error = "WEB serial not found";
    return false;
  }
  g_cached_serial = serial;
  return true;
}

static String buildDatapointUrl(const String& serial)
{
  String u = trimCopy(g_cfg_web.flexitweb_datapoint_url);
  if (u.indexOf("{serial}") >= 0)
  {
    u.replace("{serial}", serial);
    return u;
  }
  if (u.endsWith("/")) return u + serial;
  return u + "/" + serial;
}

void flexit_web_set_runtime_config(const DeviceConfig& cfg)
{
  g_cfg_web = cfg;
  if (trimCopy(cfg.flexitweb_serial).length() > 0)
    g_cached_serial = trimCopy(cfg.flexitweb_serial);
}

bool flexit_web_poll(FlexitData& out)
{
  if (WiFi.status() != WL_CONNECTED)
  {
    g_last_error = "WEB no WiFi";
    return false;
  }
  if (!flexit_web_is_ready())
  {
    g_last_error = "WEB not configured";
    return false;
  }
  if (!ensureToken()) return false;
  if (!ensureSerial()) return false;

  String url = buildDatapointUrl(g_cached_serial);
  if (!startsWithHttp(url))
  {
    g_last_error = "WEB bad datapoint URL";
    return false;
  }

  String resp;
  int code = 0;
  if (!httpGetAuth(url, g_token, resp, code))
    return false;
  if (code == 401 || code == 403)
  {
    if (!authenticate()) return false;
    if (!httpGetAuth(url, g_token, resp, code)) return false;
  }
  if (code < 200 || code >= 300)
  {
    g_last_error = String("WEB datapoint HTTP ") + code;
    return false;
  }

  bool any = false;
  float fv = 0.0f;
  int iv = 0;
  String sv;

  if (jsonGetDataPointFloat(resp, 100004, fv)) { out.uteluft = fv; any = true; }
  if (jsonGetDataPointFloat(resp, 100000, fv)) { out.tilluft = fv; any = true; }
  if (jsonGetDataPointFloat(resp, 100001, fv)) { out.avtrekk = fv; any = true; }
  if (jsonGetDataPointFloat(resp, 100002, fv)) { out.avkast = fv; any = true; }
  if (jsonGetDataPointInt(resp, 100805, iv))   { out.fan_percent = iv; any = true; }
  if (jsonGetDataPointInt(resp, 100500, iv))   { out.heat_element_percent = iv; any = true; }

  if (jsonGetDataPointString(resp, 100700, sv) || jsonGetDataPointString(resp, 100710, sv))
  {
    out.mode = sv;
    any = true;
  }

  if (!any)
  {
    g_last_error = "WEB datapoints missing";
    return false;
  }

  g_last_error = "OK";
  return true;
}

const char* flexit_web_last_error()
{
  return g_last_error.c_str();
}

bool flexit_web_is_ready()
{
  return trimCopy(g_cfg_web.flexitweb_user).length() > 0 &&
         g_cfg_web.flexitweb_pass.length() > 0 &&
         startsWithHttp(trimCopy(g_cfg_web.flexitweb_auth_url)) &&
         startsWithHttp(trimCopy(g_cfg_web.flexitweb_device_url)) &&
         startsWithHttp(trimCopy(g_cfg_web.flexitweb_datapoint_url));
}

