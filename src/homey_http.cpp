#include "homey_http.h"

#include <WiFi.h>
#include <WebServer.h>

static DeviceConfig* g_cfg = nullptr;
static HanSnapshot g_data;
static HourBar g_bars[24];
static float g_top3_kw = 0.0f;
static bool g_refresh_requested = false;

static WebServer server(80);

static bool auth_admin()
{
  if (!g_cfg) return false;
  return server.authenticate(g_cfg->admin_user.c_str(), g_cfg->admin_pass.c_str());
}

static String bearer_from_header()
{
  String h = server.header("Authorization");
  const String p = "Bearer ";
  if (!h.startsWith(p)) return "";
  return h.substring(p.length());
}

static bool auth_token(const String& expected)
{
  if (!g_cfg) return false;
  if (g_cfg->api_panic_stop) return false;
  return bearer_from_header() == expected;
}

static void send_json_unauthorized()
{
  server.send(401, "application/json", "{\"ok\":false,\"error\":\"unauthorized\"}");
}

static String status_json()
{
  String out;
  out.reserve(1800);

  out += "{";
  out += "\"ok\":true,";
  out += "\"source\":\"" + g_data.source + "\",";
  out += "\"zone\":\"" + g_data.zone + "\",";
  out += "\"stale\":" + String(g_data.stale ? "true" : "false") + ",";
  out += "\"data_time\":\"" + g_data.data_time + "\",";
  out += "\"refresh_time\":\"" + g_data.refresh_time + "\",";
  out += "\"wifi\":\"" + g_data.wifi_status + "\",";
  out += "\"ip_suffix\":\"" + g_data.ip_suffix + "\",";
  out += "\"meter_id\":\"" + g_data.meter_id + "\",";

  out += "\"phase\":[";
  for (int i = 0; i < 3; ++i)
  {
    if (i > 0) out += ",";
    out += "{";
    out += "\"id\":" + String(i + 1) + ",";
    out += "\"voltage_v\":" + String(g_data.voltage_v[i], 1) + ",";
    out += "\"current_a\":" + String(g_data.current_a[i], 2) + ",";
    out += "\"power_w\":" + String(g_data.phase_power_w[i], 1);
    out += "}";
  }
  out += "],";

  out += "\"power\":{";
  out += "\"import_w\":" + String(g_data.import_power_w, 1) + ",";
  out += "\"export_w\":" + String(g_data.export_power_w, 1) + ",";
  out += "\"import_energy_total_kwh\":" + String(g_data.import_energy_kwh_total, 3) + ",";
  out += "\"export_energy_total_kwh\":" + String(g_data.export_energy_kwh_total, 3);
  out += "},";

  out += "\"energy\":{";
  out += "\"day_kwh\":" + String(g_data.day_energy_kwh, 3) + ",";
  out += "\"month_kwh\":" + String(g_data.month_energy_kwh, 3) + ",";
  out += "\"year_kwh\":" + String(g_data.year_energy_kwh, 3);
  out += "},";

  out += "\"price\":{";
  out += "\"spot_nok_kwh\":" + String(g_data.price_spot_nok_kwh, 4) + ",";
  out += "\"grid_nok_kwh\":" + String(g_data.price_grid_nok_kwh, 4) + ",";
  out += "\"total_nok_kwh\":" + String(g_data.price_total_nok_kwh, 4) + ",";
  out += "\"capacity_top3_kw\":" + String(g_top3_kw, 3) + ",";
  out += "\"capacity_step_nok_month\":" + String(g_data.selected_capacity_step_nok_month, 2);
  out += "}";

  out += "}";
  return out;
}

static String history_json(int limit)
{
  if (limit < 1) limit = 1;
  if (limit > 24) limit = 24;

  String out = "{\"ok\":true,\"hours\":[";
  for (int i = 24 - limit; i < 24; ++i)
  {
    if (i > 24 - limit) out += ",";
    out += "{";
    out += "\"hour\":" + String(g_bars[i].hour) + ",";
    out += "\"l1_w\":" + String(g_bars[i].l1_w, 1) + ",";
    out += "\"l2_w\":" + String(g_bars[i].l2_w, 1) + ",";
    out += "\"l3_w\":" + String(g_bars[i].l3_w, 1) + ",";
    out += "\"total_w\":" + String(g_bars[i].total_w, 1) + ",";
    out += "\"kwh\":" + String(g_bars[i].kwh, 3);
    out += "}";
  }
  out += "]}";
  return out;
}

static String html_page(const String& body)
{
  String h;
  h.reserve(body.length() + 700);
  h += "<!doctype html><html><head><meta charset='utf-8'>";
  h += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
  h += "<title>HAN Reader</title>";
  h += "<style>body{font-family:ui-sans-serif,system-ui;margin:0;background:#f4f5f6;color:#111}main{max-width:940px;margin:20px auto;padding:0 14px}"
       "h1{font-size:1.4rem}.card{background:#fff;border:1px solid #d7dbdf;border-radius:12px;padding:14px;margin:10px 0}.g{display:grid;grid-template-columns:repeat(auto-fit,minmax(220px,1fr));gap:10px}"
       "label{display:block;font-size:.85rem;color:#555;margin:.4rem 0 .2rem}input,select{width:100%;padding:.5rem;border-radius:8px;border:1px solid #bcc3c9}"
       "button{background:#0a4d68;color:#fff;border:0;border-radius:8px;padding:.55rem .85rem;cursor:pointer;margin-top:.6rem}"
       "small{color:#566}code{background:#eef;padding:1px 4px;border-radius:4px}</style></head><body><main>";
  h += body;
  h += "</main></body></html>";
  return h;
}

static void handle_health()
{
  server.send(200, "application/json", "{\"ok\":true,\"service\":\"han-reader\"}");
}

static void handle_status_main()
{
  if (!auth_token(g_cfg->api_token)) return send_json_unauthorized();
  server.send(200, "application/json", status_json());
}

static void handle_status_homey()
{
  if (!g_cfg->homey_enabled || !auth_token(g_cfg->homey_api_token)) return send_json_unauthorized();
  server.send(200, "application/json", status_json());
}

static void handle_status_ha()
{
  if (!g_cfg->ha_enabled || !auth_token(g_cfg->ha_api_token)) return send_json_unauthorized();
  server.send(200, "application/json", status_json());
}

static void handle_history()
{
  if (!auth_token(g_cfg->api_token)) return send_json_unauthorized();
  int limit = server.hasArg("limit") ? server.arg("limit").toInt() : 24;
  server.send(200, "application/json", history_json(limit));
}

static void handle_public()
{
  String b;
  b.reserve(1600);
  b += "<h1>HAN Reader</h1>";
  b += "<div class='card'>";
  b += "<p><b>Import na:</b> ";
  b += String(g_data.import_power_w, 0);
  b += " W | <b>Pris:</b> ";
  b += String(g_data.price_total_nok_kwh, 2);
  b += " NOK/kWh</p>";
  b += "<p><b>Dag:</b> ";
  b += String(g_data.day_energy_kwh, 2);
  b += " kWh | <b>Mnd:</b> ";
  b += String(g_data.month_energy_kwh, 1);
  b += " kWh | <b>Ar:</b> ";
  b += String(g_data.year_energy_kwh, 0);
  b += " kWh</p></div>";

  b += "<div class='card'><h3>Faser</h3><div class='g'>";
  for (int i = 0; i < 3; ++i)
  {
    b += "<div><b>L" + String(i + 1) + "</b><br>A: " + String(g_data.current_a[i], 2) + "<br>W: " + String(g_data.phase_power_w[i], 0) + "</div>";
  }
  b += "</div></div>";

  b += "<div class='card'><p>API: <code>/status</code> (Bearer), Homey: <code>/homey/status</code>, HA: <code>/ha/status</code></p>";
  b += "<p><a href='/admin'>Admin</a></p></div>";

  server.send(200, "text/html", html_page(b));
}

static bool parse_bool_arg(const String& v)
{
  return (v == "1" || v == "on" || v == "true" || v == "TRUE");
}

static void handle_admin()
{
  if (!auth_admin()) return server.requestAuthentication();

  String b;
  b.reserve(7200);
  b += "<h1>HAN Reader Admin</h1>";

  b += "<div class='card'><h3>Status</h3>";
  b += "<p>Data: <b>" + g_data.data_time + "</b> | Refresh: <b>" + g_data.refresh_time + "</b> | Zone: <b>" + g_data.zone + "</b></p>";
  b += "<p>Import: <b>" + String(g_data.import_power_w, 0) + " W</b>, Spot: <b>" + String(g_data.price_spot_nok_kwh, 2) + "</b>, Total: <b>" + String(g_data.price_total_nok_kwh, 2) + " NOK/kWh</b></p>";
  b += "<form method='post' action='/admin/refresh_now'><button type='submit'>Refresh now</button></form>";
  b += "</div>";

  b += "<div class='card'><h3>Innstillinger</h3><form method='post' action='/admin/save'>";

  b += "<div class='g'>";

  b += "<div><label>WiFi SSID</label><input name='ssid' value='" + g_cfg->wifi_ssid + "'></div>";
  b += "<div><label>WiFi passord</label><input name='wpass' value='" + g_cfg->wifi_pass + "'></div>";

  b += "<div><label>Admin passord</label><input name='apass' value='" + g_cfg->admin_pass + "'></div>";
  b += "<div><label>Display aktivert</label><input name='disp' value='" + String(g_cfg->display_enabled ? "1" : "0") + "'></div>";

  b += "<div><label>Poll intervall ms (>=180000)</label><input name='poll' value='" + String(g_cfg->poll_interval_ms) + "'></div>";
  b += "<div><label>HAN baud</label><input name='hanbaud' value='" + String(g_cfg->han_baud) + "'></div>";

  b += "<div><label>HAN RX pin</label><input name='hanrx' value='" + String(g_cfg->han_rx_pin) + "'></div>";
  b += "<div><label>HAN TX pin</label><input name='hantx' value='" + String(g_cfg->han_tx_pin) + "'></div>";

  b += "<div><label>Pris-sone (NO1..NO5)</label><input name='zone' value='" + g_cfg->price_zone + "'></div>";
  b += "<div><label>Pris API enabled (1/0)</label><input name='papi' value='" + String(g_cfg->price_api_enabled ? "1" : "0") + "'></div>";

  b += "<div><label>Manuell spot enabled (1/0)</label><input name='mspot' value='" + String(g_cfg->manual_spot_enabled ? "1" : "0") + "'></div>";
  b += "<div><label>Manuell spot NOK/kWh</label><input name='mspotv' value='" + String(g_cfg->manual_spot_nok_kwh, 3) + "'></div>";

  b += "<div><label>Tariff profile (CUSTOM/ELVIA_EXAMPLE/BKK_EXAMPLE/TENSIO_EXAMPLE)</label><input name='tprof' value='" + g_cfg->tariff_profile + "'></div>";
  b += "<div><label>Kapasitetsledd tiers (kw:nok,kw:nok)</label><input name='tcap' value='" + g_cfg->tariff_capacity_tiers + "'></div>";

  b += "<div><label>Energiledd dag ore/kWh</label><input name='teday' value='" + String(g_cfg->tariff_energy_day_ore, 2) + "'></div>";
  b += "<div><label>Energiledd natt ore/kWh</label><input name='tenight' value='" + String(g_cfg->tariff_energy_night_ore, 2) + "'></div>";

  b += "<div><label>Energiledd helg ore/kWh</label><input name='teweek' value='" + String(g_cfg->tariff_energy_weekend_ore, 2) + "'></div>";
  b += "<div><label>Dagvindu start-slutt (timer)</label><input name='tdstart' value='" + String(g_cfg->tariff_day_start_hour) + "'><input name='tdend' value='" + String(g_cfg->tariff_day_end_hour) + "'></div>";

  b += "<div><label>Elavgift ore/kWh</label><input name='telavg' value='" + String(g_cfg->tariff_elavgift_ore, 2) + "'></div>";
  b += "<div><label>Enova ore/kWh</label><input name='tenova' value='" + String(g_cfg->tariff_enova_ore, 2) + "'></div>";

  b += "<div><label>Fastledd NOK/mnd</label><input name='tfix' value='" + String(g_cfg->tariff_fixed_monthly_nok, 2) + "'></div>";
  b += "<div><label>Forventet mndforbruk kWh</label><input name='texpm' value='" + String(g_cfg->tariff_expected_monthly_kwh, 1) + "'></div>";

  b += "<div><label>Inkl mva (1/0)</label><input name='tvaton' value='" + String(g_cfg->tariff_include_vat ? "1" : "0") + "'></div>";
  b += "<div><label>MVA prosent</label><input name='tvat' value='" + String(g_cfg->tariff_vat_percent, 2) + "'></div>";

  b += "</div>";
  b += "<button type='submit'>Lagre</button></form></div>";

  b += "<div class='card'><h3>API tokens</h3>";
  b += "<p>Main: <code>" + g_cfg->api_token + "</code></p>";
  b += "<p>Homey: <code>" + g_cfg->homey_api_token + "</code></p>";
  b += "<p>HA: <code>" + g_cfg->ha_api_token + "</code></p>";
  b += "<p>Emergency stop: " + String(g_cfg->api_panic_stop ? "AKTIV" : "AV") + "</p>";
  b += "<form method='post' action='/admin/toggle_panic'><button type='submit'>Toggle API panic stop</button></form>";
  b += "<form method='post' action='/admin/reboot'><button type='submit'>Restart enhet</button></form>";
  b += "</div>";

  server.send(200, "text/html", html_page(b));
}

static void handle_save()
{
  if (!auth_admin()) return server.requestAuthentication();

  if (server.hasArg("ssid")) g_cfg->wifi_ssid = server.arg("ssid");
  if (server.hasArg("wpass")) g_cfg->wifi_pass = server.arg("wpass");
  if (server.hasArg("apass") && server.arg("apass").length() >= 6)
  {
    g_cfg->admin_pass = server.arg("apass");
    g_cfg->admin_pass_changed = true;
  }

  if (server.hasArg("disp")) g_cfg->display_enabled = parse_bool_arg(server.arg("disp"));
  if (server.hasArg("poll")) g_cfg->poll_interval_ms = max(180000UL, static_cast<uint32_t>(server.arg("poll").toInt()));
  if (server.hasArg("hanbaud")) g_cfg->han_baud = static_cast<uint32_t>(server.arg("hanbaud").toInt());
  if (server.hasArg("hanrx")) g_cfg->han_rx_pin = server.arg("hanrx").toInt();
  if (server.hasArg("hantx")) g_cfg->han_tx_pin = server.arg("hantx").toInt();

  if (server.hasArg("zone")) g_cfg->price_zone = server.arg("zone");
  if (server.hasArg("papi")) g_cfg->price_api_enabled = parse_bool_arg(server.arg("papi"));
  if (server.hasArg("mspot")) g_cfg->manual_spot_enabled = parse_bool_arg(server.arg("mspot"));
  if (server.hasArg("mspotv")) g_cfg->manual_spot_nok_kwh = server.arg("mspotv").toFloat();

  if (server.hasArg("tprof")) g_cfg->tariff_profile = server.arg("tprof");
  if (server.hasArg("tcap")) g_cfg->tariff_capacity_tiers = server.arg("tcap");
  if (server.hasArg("teday")) g_cfg->tariff_energy_day_ore = server.arg("teday").toFloat();
  if (server.hasArg("tenight")) g_cfg->tariff_energy_night_ore = server.arg("tenight").toFloat();
  if (server.hasArg("teweek")) g_cfg->tariff_energy_weekend_ore = server.arg("teweek").toFloat();
  if (server.hasArg("tdstart")) g_cfg->tariff_day_start_hour = server.arg("tdstart").toInt();
  if (server.hasArg("tdend")) g_cfg->tariff_day_end_hour = server.arg("tdend").toInt();
  if (server.hasArg("telavg")) g_cfg->tariff_elavgift_ore = server.arg("telavg").toFloat();
  if (server.hasArg("tenova")) g_cfg->tariff_enova_ore = server.arg("tenova").toFloat();
  if (server.hasArg("tfix")) g_cfg->tariff_fixed_monthly_nok = server.arg("tfix").toFloat();
  if (server.hasArg("texpm")) g_cfg->tariff_expected_monthly_kwh = server.arg("texpm").toFloat();
  if (server.hasArg("tvaton")) g_cfg->tariff_include_vat = parse_bool_arg(server.arg("tvaton"));
  if (server.hasArg("tvat")) g_cfg->tariff_vat_percent = server.arg("tvat").toFloat();

  config_apply_tariff_profile(*g_cfg, false);

  g_cfg->setup_completed = true;
  config_save(*g_cfg);
  g_refresh_requested = true;

  server.send(200, "text/html", html_page("<h1>Lagret</h1><p>Innstillinger lagret. <a href='/admin'>Tilbake</a></p>"));
}

static void handle_refresh_now()
{
  if (!auth_admin()) return server.requestAuthentication();
  g_refresh_requested = true;
  server.send(200, "text/html", html_page("<h1>Refresh trigget</h1><p><a href='/admin'>Tilbake</a></p>"));
}

static void handle_toggle_panic()
{
  if (!auth_admin()) return server.requestAuthentication();
  g_cfg->api_panic_stop = !g_cfg->api_panic_stop;
  config_save(*g_cfg);
  server.send(200, "text/html", html_page("<h1>Oppdatert</h1><p><a href='/admin'>Tilbake</a></p>"));
}

static void handle_reboot()
{
  if (!auth_admin()) return server.requestAuthentication();
  server.send(200, "text/html", html_page("<h1>Starter pa nytt</h1>"));
  delay(150);
  ESP.restart();
}

static void handle_not_found()
{
  server.send(404, "application/json", "{\"ok\":false,\"error\":\"not_found\"}");
}

void webportal_begin(DeviceConfig& cfg)
{
  g_cfg = &cfg;

  server.on("/", HTTP_GET, handle_public);
  server.on("/health", HTTP_GET, handle_health);
  server.on("/status", HTTP_GET, handle_status_main);
  server.on("/status/history", HTTP_GET, handle_history);
  server.on("/homey/status", HTTP_GET, handle_status_homey);
  server.on("/ha/status", HTTP_GET, handle_status_ha);

  server.on("/admin", HTTP_GET, handle_admin);
  server.on("/admin/save", HTTP_POST, handle_save);
  server.on("/admin/refresh_now", HTTP_POST, handle_refresh_now);
  server.on("/admin/reboot", HTTP_POST, handle_reboot);
  server.on("/admin/toggle_panic", HTTP_POST, handle_toggle_panic);

  server.onNotFound(handle_not_found);
  server.begin();
}

void webportal_loop()
{
  server.handleClient();
}

void webportal_set_data(const HanSnapshot& data, const HourBar bars[24], float top3HourlyKw)
{
  g_data = data;
  for (int i = 0; i < 24; ++i) g_bars[i] = bars[i];
  g_top3_kw = top3HourlyKw;
}

bool webportal_consume_refresh_request()
{
  bool r = g_refresh_requested;
  g_refresh_requested = false;
  return r;
}

bool webportal_sta_connected()
{
  return WiFi.status() == WL_CONNECTED;
}

bool webportal_ap_active()
{
  return WiFi.getMode() == WIFI_AP || WiFi.getMode() == WIFI_AP_STA;
}
