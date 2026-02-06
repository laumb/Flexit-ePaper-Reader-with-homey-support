#include "homey_http.h"

#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Update.h>

static WebServer server(80);

static DeviceConfig* g_cfg = nullptr;

// Wizard state (avoid BasicAuth re-login until Finish)
static String g_pending_admin_pass;
static bool   g_pending_admin_set = false;

// Deferred restart after HTTP response
static bool   g_restart_pending = false;
static uint32_t g_restart_at_ms = 0;

// Cached payload for /status
static FlexitData g_data;
static String g_mb = "MB OFF";


static String jsonEscape(const String& s)
{
  String out;
  out.reserve(s.length() + 8);
  for (size_t i = 0; i < s.length(); i++)
  {
    char c = s[i];
    if (c == '\\' || c == '"') out += '\\';
    out += c;
  }
  return out;
}

static String normModel(const String& in)
{
  if (in == "S4") return "S4";
  return "S3";
}

static String normTransport(const String& in)
{
  if (in == "MANUAL") return "MANUAL";
  return "AUTO";
}

static String normSerialFmt(const String& in)
{
  if (in == "8E1") return "8E1";
  if (in == "8O1") return "8O1";
  return "8N1";
}

static bool tokenOK()
{
  if (!server.hasArg("token")) return false;
  String t = server.arg("token");
  return (t.length() > 0 && t == g_cfg->api_token);
}

static bool checkAdminAuth()
{
  if (!server.authenticate(g_cfg->admin_user.c_str(), g_cfg->admin_pass.c_str()))
  {
    server.requestAuthentication();
    return false;
  }
  return true;
}

static void redirectTo(const String& path)
{
  server.sendHeader("Location", path, true);
  server.send(302, "text/plain", "Redirect");
}

static String buildStatusJson(bool pretty)
{
  String mode = jsonEscape(g_data.mode);
  String wifi = jsonEscape(g_data.wifi_status);
  String ip   = jsonEscape(g_data.ip);
  String time = jsonEscape(g_data.time);
  String mb   = jsonEscape(g_mb);
  String model = jsonEscape(g_data.device_model);

  auto fOrNull = [](float v) -> String {
    if (isnan(v)) return "null";
    char t[24];
    snprintf(t, sizeof(t), "%.1f", v);
    return String(t);
  };

  String ut = fOrNull(g_data.uteluft);
  String ti = fOrNull(g_data.tilluft);
  String av = fOrNull(g_data.avtrekk);
  String ak = fOrNull(g_data.avkast);

  char buf[700];

  if (!pretty)
  {
    snprintf(buf, sizeof(buf),
      "{"
        "\"time\":\"%s\","
        "\"mode\":\"%s\","
        "\"uteluft\":%s,"
        "\"tilluft\":%s,"
        "\"avtrekk\":%s,"
        "\"avkast\":%s,"
        "\"fan\":%d,"
        "\"heat\":%d,"
        "\"efficiency\":%d,"
        "\"model\":\"%s\","
        "\"wifi\":\"%s\","
        "\"ip\":\"%s\","
        "\"modbus\":\"%s\""
      "}",
      time.c_str(),
      mode.c_str(),
      ut.c_str(),
      ti.c_str(),
      av.c_str(),
      ak.c_str(),
      g_data.fan_percent,
      g_data.heat_element_percent,
      g_data.efficiency_percent,
      model.c_str(),
      wifi.c_str(),
      ip.c_str(),
      mb.c_str()
    );
    return String(buf);
  }

  snprintf(buf, sizeof(buf),
    "{\n"
    "  \"time\": \"%s\",\n"
    "  \"mode\": \"%s\",\n"
    "  \"uteluft\": %s,\n"
    "  \"tilluft\": %s,\n"
    "  \"avtrekk\": %s,\n"
    "  \"avkast\": %s,\n"
    "  \"fan\": %d,\n"
    "  \"heat\": %d,\n"
    "  \"efficiency\": %d,\n"
    "  \"model\": \"%s\",\n"
    "  \"wifi\": \"%s\",\n"
    "  \"ip\": \"%s\",\n"
    "  \"modbus\": \"%s\"\n"
    "}\n",
    time.c_str(),
    mode.c_str(),
    ut.c_str(),
    ti.c_str(),
    av.c_str(),
    ak.c_str(),
    g_data.fan_percent,
    g_data.heat_element_percent,
    g_data.efficiency_percent,
    model.c_str(),
    wifi.c_str(),
    ip.c_str(),
    mb.c_str()
  );
  return String(buf);
}

static void handleHealth()
{
  server.send(200, "text/plain", "ok");
}

static void handleStatus()
{
  if (!tokenOK())
  {
    server.send(401, "text/plain", "missing/invalid token");
    return;
  }

  bool pretty = false;
  if (server.hasArg("pretty"))
  {
    String v = server.arg("pretty");
    pretty = (v == "1" || v == "true" || v == "yes");
  }

  server.send(200, "application/json", buildStatusJson(pretty));
}

static String pageHeader(const String& title, const String& subtitle = "")
    {
      String s;
      s += "<!doctype html><html><head><meta charset='utf-8'>";
      s += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
      s += "<title>" + title + "</title>";

      // Minimal-fuzz "micro framework" with CSS variables + responsive layout.
      // Theme: Light (default) and Dark (deep gray + gold #C2A17E)
      s += "<style>";
      s += "*,*::before,*::after{box-sizing:border-box;}";
      s += ":root{--bg:#f6f7f9;--card:#ffffff;--text:#111827;--muted:#6b7280;--border:#e5e7eb;--accent:#C2A17E;--btn:#111827;--btnText:#ffffff;--input:#ffffff;--shadow:0 10px 25px rgba(0,0,0,.06);}";

      s += "[data-theme='dark']{--bg:#111315;--card:#1a1d20;--text:#e5e7eb;--muted:#a1a1aa;--border:#2a2f35;--accent:#C2A17E;--btn:#C2A17E;--btnText:#111315;--input:#111315;--shadow:0 10px 25px rgba(0,0,0,.35);}";

      s += "html,body{height:100%;}";
      s += "body{margin:0;font-family:system-ui,-apple-system,Segoe UI,Roboto,Arial; background:var(--bg); color:var(--text);}";
      s += ".wrap{max-width:920px;margin:0 auto;padding:18px;}";
      s += ".topbar{display:flex;align-items:center;justify-content:space-between;gap:12px;margin-bottom:14px;}";
      s += ".brand{display:flex;flex-direction:column;line-height:1.15;}";
      s += ".brand h1{font-size:18px;margin:0;letter-spacing:.4px;}";
      s += ".brand small{color:var(--muted);}";
      s += ".pill{display:inline-flex;align-items:center;gap:8px;padding:8px 10px;border:1px solid var(--border);border-radius:999px;background:var(--card);box-shadow:var(--shadow);}";
      s += ".dot{width:10px;height:10px;border-radius:50%;background:var(--muted);}";
      s += ".dot.ok{background:#22c55e;}.dot.warn{background:#f59e0b;}.dot.bad{background:#ef4444;}";
      s += ".grid{display:grid;grid-template-columns:1fr;gap:14px;}";
      s += "@media(min-width:860px){.grid{grid-template-columns:1fr 1fr;}}";
      s += ".card{background:var(--card);border:1px solid var(--border);border-radius:16px;padding:14px;box-shadow:var(--shadow);}";
      s += ".card h2{font-size:14px;margin:0 0 10px 0;color:var(--muted);text-transform:uppercase;letter-spacing:.12em;}";
      s += "label{display:block;font-size:12px;color:var(--muted);margin-top:10px;}";
      s += "input,select{width:100%;padding:11px 12px;border-radius:12px;border:1px solid var(--border);background:var(--input);color:var(--text);outline:none;}";
      s += "input:focus,select:focus{border-color:var(--accent);box-shadow:0 0 0 3px rgba(194,161,126,.22);}";
      s += ".row{display:flex;gap:12px;flex-wrap:wrap;}";
      s += ".row > *{flex:1 1 220px;}";
      s += ".btn{display:inline-flex;align-items:center;justify-content:center;gap:8px;padding:11px 14px;border-radius:12px;border:1px solid var(--border);background:var(--btn);color:var(--btnText);font-weight:600;cursor:pointer;}";
      s += ".btn.secondary{background:transparent;color:var(--text);}";
      s += ".btn.danger{background:#ef4444;border-color:#ef4444;color:#fff;}";
      s += ".actions{display:flex;gap:10px;flex-wrap:wrap;margin-top:14px;}";
      s += ".help{font-size:12px;color:var(--muted);margin-top:8px;line-height:1.35;}";
      s += ".progress{display:flex;gap:8px;align-items:center;margin-top:10px;}";
      s += ".step{height:8px;flex:1;border-radius:999px;background:var(--border);overflow:hidden;}";
      s += ".step > i{display:block;height:100%;width:0;background:var(--accent);}";
      s += ".kpi{display:flex;gap:14px;flex-wrap:wrap;}";
      s += ".kpi .kv{min-width:140px;}";
      s += ".kpi .k{font-size:11px;color:var(--muted);text-transform:uppercase;letter-spacing:.12em;}";
      s += ".kpi .v{font-size:16px;font-weight:700;margin-top:4px;}";
      s += "a{color:var(--accent);text-decoration:none;}";
      s += "code{background:rgba(194,161,126,.18);padding:2px 6px;border-radius:8px;}";
      s += "</style>";

      s += "<script>";
      s += "(function(){var t=localStorage.getItem('theme');if(t){document.documentElement.setAttribute('data-theme',t);}else{var prefers=window.matchMedia&&window.matchMedia('(prefers-color-scheme: dark)').matches;document.documentElement.setAttribute('data-theme',prefers?'dark':'light');}})();";
      s += "function toggleTheme(){var cur=document.documentElement.getAttribute('data-theme')||'light';var nxt=(cur==='dark')?'light':'dark';document.documentElement.setAttribute('data-theme',nxt);localStorage.setItem('theme',nxt);}";
      s += "</script>";

      s += "</head><body><div class='wrap'>";

      s += "<div class='topbar'>";
      s += "<div class='brand'><h1>" + title + "</h1>";
      if (subtitle.length()) s += "<small>" + subtitle + "</small>";
      s += "</div>";
      s += "<button class='btn secondary' type='button' onclick='toggleTheme()'>☾ / ☀</button>";
      s += "</div>";
      return s;
    }

static String pageFooter(){ return "</div><div style='margin-top:20px'><form method='POST' action='/admin/reboot'><button class=\"btn\">Restart enheten nå</button></form></div></body></html>"; }

static void handleRoot()
{
  String s = pageHeader("Flexit-reader", "Admin portal");

  bool apOn  = (WiFi.getMode() & WIFI_AP);
  bool staOn = (WiFi.status() == WL_CONNECTED);

  s += "<div class='grid'>";
  s += "<div class='card'><h2>Status</h2>";
  s += "<div class='kpi'>";
  s += "<div class='kv'><div class='k'>STA WiFi</div><div class='v'>" + String(staOn ? "Connected" : "Offline") + "</div></div>";
  s += "<div class='kv'><div class='k'>Fallback AP</div><div class='v'>" + String(apOn ? "ON" : "OFF") + "</div></div>";
  s += "<div class='kv'><div class='k'>mDNS</div><div class='v'>ventreader.local</div></div>";
  s += "</div>";
  s += "<div class='help'>API: <code>/status?token=...</code> (Homey polling). Debug: <code>&pretty=1</code></div>";
  s += "</div>";

  s += "<div class='card'><h2>Handlinger</h2>";
  s += "<div class='actions'>";
  s += "<a class='btn' href='/admin'>Åpne admin</a>";
  s += "<a class='btn secondary' href='/health'>/health</a>";
  s += "</div>";
  s += "<div class='help'>Admin er beskyttet med passord. Ved første innlogging må du endre passord.</div>";
  s += "</div>";

  s += "</div>"; // grid
  s += pageFooter();
  server.send(200, "text/html", s);
}

// Forced setup page (change admin pass on first login)
static void wizardProgress(String& s, int step)
{
  s += "<div class='progress'>";
  for (int i = 1; i <= 3; i++)
  {
    s += "<div class='step'><i style='width:" + String(i <= step ? 100 : 0) + "%'></i></div>";
  }
  s += "</div>";
  s += "<div class='help'>Steg " + String(step) + " av 3</div>";
}

static void handleAdminSetup()
{
  if (!checkAdminAuth()) return;

  int step = 1;
  if (server.hasArg("step")) step = server.arg("step").toInt();
  if (step < 1) step = 1;
  if (step > 3) step = 3;

  String s = pageHeader("Oppsett", "Stegvis konfigurasjon (første login)");
  wizardProgress(s, step);

  if (step == 1)
  {
    s += "<div class='card'><h2>Admin passord</h2>";
    s += "<form method='POST' action='/admin/setup_save?step=1'>";
    s += "<label>Nytt passord (min 8 tegn)</label><input name='p1' type='password' required>";
    s += "<label>Gjenta passord</label><input name='p2' type='password' required>";
    s += "<div class='actions'><button class='btn' type='submit'>Neste</button></div>";
    s += "</form>";
    s += "<div class='help'>Du kan ikke bruke admin-siden før passordet er endret.</div>";
    s += "</div>";
  }
  else if (step == 2)
  {
    s += "<div class='card'><h2>WiFi</h2>";
    s += "<form method='POST' action='/admin/setup_save?step=2'>";
    s += "<label>SSID</label><input name='ssid' value='" + jsonEscape(g_cfg->wifi_ssid) + "' required>";
    s += "<label>Passord</label><input name='wpass' type='password' value=''>";
    s += "<div class='actions'><a class='btn secondary' href='/admin/setup?step=1'>Tilbake</a><button class='btn' type='submit'>Neste</button></div>";
    s += "</form>";
    s += "<div class='help'>Passord kan stå tomt dersom nettverket er åpent.</div>";
    s += "</div>";
  }
  else // step 3
  {
    s += "<div class='card'><h2>Token + moduler</h2>";
    s += "<form method='POST' action='/admin/setup_save?step=3'>";
    s += "<label>Enhetsmodell</label>";
    s += "<select name='model' class='input'>";
    s += "<option value='S3'" + String(g_cfg->model == "S3" ? " selected" : "") + ">Nordic S3</option>";
    s += "<option value='S4'" + String(g_cfg->model == "S4" ? " selected" : "") + ">Nordic S4</option>";
    s += "</select>";

    s += "<label>API-token (for /status)</label><input class='mono' name='token' value='" + jsonEscape(g_cfg->api_token) + "' required>";
    s += "<label><input type='checkbox' name='modbus' " + String(g_cfg->modbus_enabled ? "checked" : "") + "> Modbus</label>";
    s += "<label><input type='checkbox' name='homey' " + String(g_cfg->homey_enabled ? "checked" : "") + "> Homey/API</label>";
    s += "<label>Modbus transport</label>";
    s += "<select name='mbtr' class='input'>";
    s += "<option value='AUTO'" + String(g_cfg->modbus_transport_mode == "AUTO" ? " selected" : "") + ">AUTO (anbefalt for auto-dir modul)</option>";
    s += "<option value='MANUAL'" + String(g_cfg->modbus_transport_mode == "MANUAL" ? " selected" : "") + ">MANUAL (DE/RE styres av GPIO)</option>";
    s += "</select>";
    s += "<div class='row'>";
    s += "<div><label>Modbus baud</label><input name='mbbaud' type='number' min='1200' max='115200' value='" + String(g_cfg->modbus_baud) + "'></div>";
    s += "<div><label>Slave ID</label><input name='mbid' type='number' min='1' max='247' value='" + String((int)g_cfg->modbus_slave_id) + "'></div>";
    s += "<div><label>Addr offset</label><input name='mboff' type='number' min='-5' max='5' value='" + String((int)g_cfg->modbus_addr_offset) + "'></div>";
    s += "</div>";
    s += "<label>Serial format</label>";
    s += "<select name='mbser' class='input'>";
    s += "<option value='8N1'" + String(g_cfg->modbus_serial_format == "8N1" ? " selected" : "") + ">8N1</option>";
    s += "<option value='8E1'" + String(g_cfg->modbus_serial_format == "8E1" ? " selected" : "") + ">8E1</option>";
    s += "<option value='8O1'" + String(g_cfg->modbus_serial_format == "8O1" ? " selected" : "") + ">8O1</option>";
    s += "</select>";
    s += "<label>Oppdateringsintervall (sek)</label><input name='poll' type='number' min='30' max='3600' value='" + String(g_cfg->poll_interval_ms/1000) + "'>";
    s += "<div class='actions'><a class='btn secondary' href='/admin/setup?step=2'>Tilbake</a><button class='btn' type='submit'>Fullfør &amp; restart</button></div>";
    s += "</form>";
    s += "<div class='help'>Når du fullfører, blir oppsettet lagret og du kan gå til admin.</div>";
    s += "</div>";
  }

  s += pageFooter();
  server.send(200, "text/html", s);
}

static void handleAdminSetupSave()
{
  if (!checkAdminAuth()) return;

  int step = 1;
  if (server.hasArg("step")) step = server.arg("step").toInt();
  if (step < 1) step = 1;
  if (step > 3) step = 3;

  if (step == 1)
  {
    String p1 = server.arg("p1");
    String p2 = server.arg("p2");
    if (p1.length() < 8 || p1 != p2)
    {
      server.send(400, "text/plain", "Passord matcher ikke eller er for kort (min 8 tegn).");
      return;
    }

    // Do NOT apply password yet. We keep current BasicAuth creds until Finish,
    // otherwise the browser will require re-login mid-wizard.
    g_pending_admin_pass = p1;
    g_pending_admin_set  = true;

    redirectTo("/admin/setup?step=2");
    return;
  }
  

  if (!g_pending_admin_set)
  {
    redirectTo("/admin/setup?step=1");
    return;
  }

  if (step == 2)
  {
    g_cfg->wifi_ssid = server.arg("ssid");
    g_cfg->wifi_pass = server.arg("wpass");
    config_save(*g_cfg);
    redirectTo("/admin/setup?step=3");
    return;
  }

  // step 3
  if (server.hasArg("model"))
  {
    g_cfg->model = normModel(server.arg("model"));
  }

  g_cfg->api_token = server.arg("token");
  g_cfg->modbus_enabled = server.hasArg("modbus");
  g_cfg->homey_enabled  = server.hasArg("homey");
  g_cfg->modbus_transport_mode = normTransport(server.arg("mbtr"));
  g_cfg->modbus_serial_format = normSerialFmt(server.arg("mbser"));

  uint32_t mbBaud = (uint32_t)server.arg("mbbaud").toInt();
  if (mbBaud < 1200 || mbBaud > 115200) mbBaud = 19200;
  g_cfg->modbus_baud = mbBaud;

  int mbId = server.arg("mbid").toInt();
  if (mbId < 1 || mbId > 247) mbId = 1;
  g_cfg->modbus_slave_id = (uint8_t)mbId;

  int mbOff = server.arg("mboff").toInt();
  if (mbOff < -5) mbOff = -5;
  if (mbOff > 5) mbOff = 5;
  g_cfg->modbus_addr_offset = (int8_t)mbOff;

  uint32_t pollSec = (uint32_t) server.arg("poll").toInt();
  if (pollSec < 30) pollSec = 30;
  if (pollSec > 3600) pollSec = 3600;
  g_cfg->poll_interval_ms = pollSec * 1000UL;

  // Apply pending admin password *now* (finish event)
  if (g_pending_admin_set)
  {
    g_cfg->admin_pass = g_pending_admin_pass;
    g_cfg->admin_pass_changed = true;
  }

  // Wizard is now complete
  g_cfg->setup_completed = true;

  config_save(*g_cfg);

  // clear pending (avoid re-applying)
  g_pending_admin_pass = "";
  g_pending_admin_set = false;

  String s = pageHeader("Oppsett", "Fullført");
  s += "<div class='card'><h2>Oppsett fullført</h2>";
  s += "<p>Innstillinger er lagret. Enheten restarter nå for å aktivere WiFi og sikkerhetsinnstillinger.</p>";
  s += "<div class='help'>Hvis siden ikke oppdaterer seg automatisk innen 10 sekunder, koble deg til enhetens nye IP/hostname og prøv igjen.</div>";
  s += "</div>";
  s += "<div class='card'><h2>Handling</h2>";
  s += "<form method='POST' action='/admin/reboot'><button class='btn' type='submit'>Restart enheten nå</button></form>";
  s += "</div>";
  s += pageFooter();

  server.send(200, "text/html", s);

  // Auto-restart shortly after sending the response
  g_restart_pending = true;
  g_restart_at_ms = millis() + 1500;
}
static void handleAdmin()
{
  if (!checkAdminAuth()) return;

  // Force setup until wizard completed
  if (!g_cfg->setup_completed)
  {
    redirectTo("/admin/setup?step=1");
    return;
  }

  String s = pageHeader("Admin", "Innstillinger");
  s += "<div class='grid'>";

  // WiFi
  s += "<div class='card'><h2>WiFi</h2>";
  s += "<form method='POST' action='/admin/save'>";
  s += "<label>SSID</label><input name='ssid' value='" + jsonEscape(g_cfg->wifi_ssid) + "'>";
  s += "<label>Passord (la tomt for å beholde)</label><input name='wpass' type='password' value=''>";
  s += "<div class='help'>Hvis du endrer WiFi må enheten restartes etterpå.</div>";
  s += "</div>";

  // API + modules
  s += "<div class='card'><h2>API + moduler</h2>";
  s += "<label>Enhetsmodell</label>";
  s += "<select name='model' class='input'>";
  s += "<option value='S3'" + String(g_cfg->model == "S3" ? " selected" : "") + ">Nordic S3</option>";
  s += "<option value='S4'" + String(g_cfg->model == "S4" ? " selected" : "") + ">Nordic S4</option>";
  s += "</select>";
  s += "<label>API-token (for /status)</label><input class='mono' name='token' value='" + jsonEscape(g_cfg->api_token) + "' required>";
  s += "<div class='row'>";
  s += "<label style='flex:1 1 220px'><input type='checkbox' name='modbus' " + String(g_cfg->modbus_enabled ? "checked" : "") + "> Modbus</label>";
  s += "<label style='flex:1 1 220px'><input type='checkbox' name='homey' " + String(g_cfg->homey_enabled ? "checked" : "") + "> Homey/API</label>";
  s += "</div>";
  s += "<label>Modbus transport</label>";
  s += "<select name='mbtr' class='input'>";
  s += "<option value='AUTO'" + String(g_cfg->modbus_transport_mode == "AUTO" ? " selected" : "") + ">AUTO (anbefalt for auto-dir modul)</option>";
  s += "<option value='MANUAL'" + String(g_cfg->modbus_transport_mode == "MANUAL" ? " selected" : "") + ">MANUAL (DE/RE styres av GPIO)</option>";
  s += "</select>";
  s += "<div class='row'>";
  s += "<div><label>Modbus baud</label><input name='mbbaud' type='number' min='1200' max='115200' value='" + String(g_cfg->modbus_baud) + "'></div>";
  s += "<div><label>Slave ID</label><input name='mbid' type='number' min='1' max='247' value='" + String((int)g_cfg->modbus_slave_id) + "'></div>";
  s += "<div><label>Addr offset</label><input name='mboff' type='number' min='-5' max='5' value='" + String((int)g_cfg->modbus_addr_offset) + "'></div>";
  s += "</div>";
  s += "<label>Serial format</label>";
  s += "<select name='mbser' class='input'>";
  s += "<option value='8N1'" + String(g_cfg->modbus_serial_format == "8N1" ? " selected" : "") + ">8N1</option>";
  s += "<option value='8E1'" + String(g_cfg->modbus_serial_format == "8E1" ? " selected" : "") + ">8E1</option>";
  s += "<option value='8O1'" + String(g_cfg->modbus_serial_format == "8O1" ? " selected" : "") + ">8O1</option>";
  s += "</select>";
  s += "<label>Oppdateringsintervall (sek)</label><input name='poll' type='number' min='30' max='3600' value='" + String(g_cfg->poll_interval_ms/1000) + "'>";
  s += "<div class='help'>Skjermen oppdateres ved samme intervall (partial refresh).</div>";
  s += "</div>";

  // Admin password
  s += "<div class='card'><h2>Sikkerhet</h2>";
  s += "<label>Nytt admin-passord</label><input name='np1' type='password'>";
  s += "<label>Gjenta nytt passord</label><input name='np2' type='password'>";
  s += "<div class='help'>Min 8 tegn. La tomt for å ikke endre.</div>";
  s += "</div>";

  // Actions
  s += "<div class='card'><h2>Lagre / handlinger</h2>";
  s += "<div class='actions'>";
  s += "<button class='btn' type='submit'>Lagre</button>";
  s += "<a class='btn secondary' href='/admin/ota'>OTA</a>";
  s += "<a class='btn secondary' href='/'>Til start</a>";
  s += "</div>";
  s += "</form>";
  s += "<hr style='border:none;border-top:1px solid var(--border);margin:14px 0'>";
  s += "<div class='actions'>";
  s += "<form method='POST' action='/admin/reboot' style='margin:0'><button class='btn secondary' type='submit'>Restart</button></form>";
  s += "<form method='POST' action='/admin/factory_reset' style='margin:0' onsubmit='return confirm(\"Fabrikkreset? Dette sletter alt.\");'>"
       "<button class='btn danger' type='submit'>Fabrikkreset</button></form>";
  s += "</div>";
  s += "<div class='help'>Fabrikkreset kan også trigges ved å holde BOOT (GPIO0) ~6s ved oppstart.</div>";
  s += "</div>";

  s += "</div>"; // grid
  s += pageFooter();
  server.send(200, "text/html", s);
}

static void handleAdminSave()
{
  if (!checkAdminAuth()) return;
  if (!g_cfg->admin_pass_changed) { redirectTo("/admin/setup?step=1"); return; }

  // WiFi
  g_cfg->wifi_ssid = server.arg("ssid");
  String newWifiPass = server.arg("wpass");
  if (newWifiPass.length() > 0) g_cfg->wifi_pass = newWifiPass;

  // Token
  g_cfg->api_token = server.arg("token");

  // Model
  if (server.hasArg("model"))
  {
    g_cfg->model = normModel(server.arg("model"));
  }

  // Modules
  g_cfg->modbus_enabled = server.hasArg("modbus");
  g_cfg->homey_enabled  = server.hasArg("homey");
  g_cfg->modbus_transport_mode = normTransport(server.arg("mbtr"));
  g_cfg->modbus_serial_format = normSerialFmt(server.arg("mbser"));

  uint32_t mbBaud = (uint32_t)server.arg("mbbaud").toInt();
  if (mbBaud < 1200 || mbBaud > 115200) mbBaud = 19200;
  g_cfg->modbus_baud = mbBaud;

  int mbId = server.arg("mbid").toInt();
  if (mbId < 1 || mbId > 247) mbId = 1;
  g_cfg->modbus_slave_id = (uint8_t)mbId;

  int mbOff = server.arg("mboff").toInt();
  if (mbOff < -5) mbOff = -5;
  if (mbOff > 5) mbOff = 5;
  g_cfg->modbus_addr_offset = (int8_t)mbOff;

  // Poll interval
  uint32_t pollSec = (uint32_t) server.arg("poll").toInt();
  if (pollSec < 30) pollSec = 30;
  if (pollSec > 3600) pollSec = 3600;
  g_cfg->poll_interval_ms = pollSec * 1000UL;

  // Admin pass change (optional)
  String np1 = server.arg("np1");
  String np2 = server.arg("np2");
  if (np1.length() > 0 || np2.length() > 0)
  {
    if (np1.length() < 8 || np1 != np2)
    {
      server.send(400, "text/plain", "Admin pass: må matche og være minst 8 tegn.");
      return;
    }
    g_cfg->admin_pass = np1;
    g_cfg->admin_pass_changed = true;
    config_save(*g_cfg);
    server.send(200, "text/html", "<html><body><h3>Lagret.</h3><p>Logg inn på nytt med nytt passord.</p><a href='/admin'>Admin</a><div style='margin-top:20px'><form method='POST' action='/admin/reboot'><button class=\"btn\">Restart enheten nå</button></form></div></body></html>");
    return;
  }

  config_save(*g_cfg);
  server.send(200, "text/html", "<html><body><h3>Lagret.</h3><p>Restart for å bruke ny WiFi hvis endret.</p><a href='/admin'>Tilbake</a><div style='margin-top:20px'><form method='POST' action='/admin/reboot'><button class=\"btn\">Restart enheten nå</button></form></div></body></html>");
}

static void handleReboot()
{
  if (!checkAdminAuth()) return;
  server.send(200, "text/plain", "rebooting...");
  delay(300);
  ESP.restart();
}

static void handleFactoryReset()
{
  if (!checkAdminAuth()) return;
  server.send(200, "text/plain", "factory reset...");
  delay(300);

  config_factory_reset(); // wipes NVS keys
  delay(200);
  ESP.restart();
}

static void handleAdminOta()
{
  if (!checkAdminAuth()) return;

  String s = pageHeader("OTA");
  s += "<div class='card'><h2>OTA via filopplasting (.bin)</h2>";
  s += "<p>Last opp firmwarefil for ESP32 direkte fra nettleser.</p>";
  s += "<form method='POST' action='/admin/ota_upload' enctype='multipart/form-data'>";
  s += "<label>Firmware-fil (.bin)</label>";
  s += "<input type='file' name='firmware' accept='.bin,application/octet-stream' required>";
  s += "<div class='actions'><button class='btn' type='submit'>Start oppdatering</button></div>";
  s += "</form>";
  s += "<div class='help'>Enheten restarter automatisk når oppdateringen er ferdig.</div>";
  s += "</div>";
  s += "<div class='card'><h2>Alternativ: Arduino OTA</h2>";
  s += "<p>Du kan fortsatt bruke Arduino IDE nettverksport dersom ønskelig.</p>";
  s += "</div>";
  s += "<div class='card'><a class='btn' href='/admin'>Tilbake</a></div>";
  s += pageFooter();
  server.send(200, "text/html", s);
}

static void handleAdminOtaUploadDone()
{
  if (!checkAdminAuth()) return;
  if (!g_cfg->setup_completed)
  {
    server.send(403, "text/plain", "setup not completed");
    return;
  }

  if (Update.hasError())
  {
    char b[96];
    snprintf(b, sizeof(b), "OTA failed. Update error code: %u", Update.getError());
    server.send(500, "text/plain", b);
    return;
  }

  server.send(200, "text/html",
    "<html><body><h3>OTA OK</h3><p>Firmware er oppdatert. Enheten restarter...</p></body></html>");
  delay(300);
  ESP.restart();
}

static void handleAdminOtaUploadStream()
{
  if (!checkAdminAuth()) return;
  if (!g_cfg->setup_completed) return;

  HTTPUpload& upload = server.upload();

  if (upload.status == UPLOAD_FILE_START)
  {
    if (!g_cfg->setup_completed)
    {
      return;
    }

    if (!Update.begin(UPDATE_SIZE_UNKNOWN))
    {
      Update.printError(Serial);
    }
  }
  else if (upload.status == UPLOAD_FILE_WRITE)
  {
    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize)
    {
      Update.printError(Serial);
    }
  }
  else if (upload.status == UPLOAD_FILE_END)
  {
    if (!Update.end(true))
    {
      Update.printError(Serial);
    }
  }
  else if (upload.status == UPLOAD_FILE_ABORTED)
  {
    Update.abort();
  }
}

void webportal_set_data(const FlexitData& data, const String& mbStatus)
{
  g_data = data;
  g_mb = mbStatus;
}

bool webportal_sta_connected() { return (WiFi.status() == WL_CONNECTED); }
bool webportal_ap_active() { return (WiFi.getMode() & WIFI_AP); }

void webportal_begin(DeviceConfig& cfg)
{
  g_cfg = &cfg;

  // mDNS (STA mode only)
  if (WiFi.status() == WL_CONNECTED)
  {    if (MDNS.begin("ventreader"))
    {
      MDNS.addService("http", "tcp", 80);
    }
  }
  else
  {  }

  server.on("/", HTTP_GET, handleRoot);
  server.on("/health", HTTP_GET, handleHealth);
  server.on("/status", HTTP_GET, [](){
    if (!g_cfg->homey_enabled)
    {
      server.send(403, "text/plain", "homey/api disabled");
      return;
    }
    handleStatus();
  });

  // Admin
  server.on("/admin", HTTP_GET, handleAdmin);
  server.on("/admin/save", HTTP_POST, handleAdminSave);
  server.on("/admin/ota", HTTP_GET, handleAdminOta);
  server.on("/admin/ota_upload", HTTP_POST, handleAdminOtaUploadDone, handleAdminOtaUploadStream);
  server.on("/admin/reboot", HTTP_POST, handleReboot);
  server.on("/admin/factory_reset", HTTP_POST, handleFactoryReset);

  // Forced setup
  server.on("/admin/setup", HTTP_GET, handleAdminSetup);
  server.on("/admin/setup_save", HTTP_POST, handleAdminSetupSave);

  server.begin();
}

void webportal_loop()
{
  server.handleClient();

  if (g_restart_pending && (int32_t)(millis() - g_restart_at_ms) >= 0)
  {
    delay(50);
    ESP.restart();
  }
}
