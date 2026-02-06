#include "homey_http.h"

#include <cstring>
#include <time.h>
#include <sys/time.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Update.h>
#include "version.h"
#include "flexit_modbus.h"

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
  if (in == "S2_EXP") return "S2_EXP";
  if (in == "S4") return "S4";
  if (in == "S7_EXP") return "S7_EXP";
  if (in == "CL3_EXP") return "CL3_EXP";
  if (in == "CL4_EXP") return "CL4_EXP";
  return "S3";
}

static String normLang(const String& in)
{
  if (in == "en" || in == "no" || in == "da" || in == "sv" || in == "fi" || in == "uk") return in;
  return "no";
}

static String lang()
{
  if (!g_cfg) return "no";
  return normLang(g_cfg->ui_language);
}

static String tr(const char* key)
{
  const String l = lang();
  const bool en = (l == "en");
  const bool no = (l == "no");
  const bool da = (l == "da");
  const bool sv = (l == "sv");
  const bool fi = (l == "fi");
  const bool uk = (l == "uk");

  if (strcmp(key, "settings") == 0) return en ? "Settings" : no ? "Innstillinger" : da ? "Indstillinger" : sv ? "Inställningar" : fi ? "Asetukset" : "Налаштування";
  if (strcmp(key, "wifi") == 0) return en ? "WiFi" : no ? "WiFi" : da ? "WiFi" : sv ? "WiFi" : fi ? "WiFi" : "WiFi";
  if (strcmp(key, "model") == 0) return en ? "Device model" : no ? "Enhetsmodell" : da ? "Enhedsmodel" : sv ? "Enhetsmodell" : fi ? "Laitemalli" : "Модель пристрою";
  if (strcmp(key, "homey_api") == 0) return en ? "Homey/API" : no ? "Homey/API" : da ? "Homey/API" : sv ? "Homey/API" : fi ? "Homey/API" : "Homey/API";
  if (strcmp(key, "ha_api") == 0) return en ? "Home Assistant/API" : no ? "Home Assistant/API" : da ? "Home Assistant/API" : sv ? "Home Assistant/API" : fi ? "Home Assistant/API" : "Home Assistant/API";
  if (strcmp(key, "modbus") == 0) return en ? "Modbus" : no ? "Modbus" : da ? "Modbus" : sv ? "Modbus" : fi ? "Modbus" : "Modbus";
  if (strcmp(key, "advanced_modbus") == 0) return en ? "Advanced Modbus settings" : no ? "Avanserte Modbus-innstillinger" : da ? "Avancerede Modbus-indstillinger" : sv ? "Avancerade Modbus-inställningar" : fi ? "Edistyneet Modbus-asetukset" : "Розширені налаштування Modbus";
  if (strcmp(key, "save") == 0) return en ? "Save" : no ? "Lagre" : da ? "Gem" : sv ? "Spara" : fi ? "Tallenna" : "Зберегти";
  if (strcmp(key, "back") == 0) return en ? "Back" : no ? "Tilbake" : da ? "Tilbage" : sv ? "Tillbaka" : fi ? "Takaisin" : "Назад";
  if (strcmp(key, "setup") == 0) return en ? "Setup" : no ? "Oppsett" : da ? "Opsætning" : sv ? "Installation" : fi ? "Asennus" : "Налаштування";
  if (strcmp(key, "next") == 0) return en ? "Next" : no ? "Neste" : da ? "Næste" : sv ? "Nästa" : fi ? "Seuraava" : "Далі";
  if (strcmp(key, "complete_restart") == 0) return en ? "Complete & restart" : no ? "Fullfør & restart" : da ? "Fuldfør & genstart" : sv ? "Slutför & starta om" : fi ? "Valmis & käynnistä uudelleen" : "Завершити та перезапустити";
  if (strcmp(key, "poll_sec") == 0) return en ? "Update interval (sec)" : no ? "Oppdateringsintervall (sek)" : da ? "Opdateringsinterval (sek)" : sv ? "Uppdateringsintervall (sek)" : fi ? "Päivitysväli (s)" : "Інтервал оновлення (с)";
  if (strcmp(key, "language") == 0) return en ? "Language" : no ? "Språk" : da ? "Sprog" : sv ? "Språk" : fi ? "Kieli" : "Мова";
  if (strcmp(key, "control_enable") == 0) return en ? "Enable remote control writes (experimental)" : no ? "Aktiver fjernstyring med skriv (experimental)" : da ? "Aktivér fjernstyring med skriv (experimental)" : sv ? "Aktivera fjärrstyrning med skrivning (experimental)" : fi ? "Salli etäohjaus kirjoituksilla (experimental)" : "Увімкнути віддалене керування записом (experimental)";
  if (strcmp(key, "admin") == 0) return en ? "Admin" : no ? "Admin" : da ? "Admin" : sv ? "Admin" : fi ? "Admin" : "Адмін";
  if (strcmp(key, "restart") == 0) return en ? "Restart" : no ? "Restart" : da ? "Genstart" : sv ? "Starta om" : fi ? "Kaynnista uudelleen" : "Перезапуск";
  if (strcmp(key, "factory_reset") == 0) return en ? "Factory reset" : no ? "Factory reset" : da ? "Fabriksnulstilling" : sv ? "Fabriksaterstallning" : fi ? "Tehdasasetusten palautus" : "Скидання до заводських";
  if (strcmp(key, "restart_now") == 0) return en ? "Restart device now" : no ? "Restart enheten nå" : da ? "Genstart enheden nu" : sv ? "Starta om enheten nu" : fi ? "Kaynnista laite uudelleen nyt" : "Перезапустити пристрій зараз";
  if (strcmp(key, "admin_portal") == 0) return en ? "Admin portal" : no ? "Admin portal" : da ? "Admin portal" : sv ? "Adminportal" : fi ? "Yllapitoportaali" : "Адмін портал";
  if (strcmp(key, "status") == 0) return en ? "Status" : no ? "Status" : da ? "Status" : sv ? "Status" : fi ? "Tila" : "Статус";
  if (strcmp(key, "actions") == 0) return en ? "Actions" : no ? "Handlinger" : da ? "Handlinger" : sv ? "Atgarder" : fi ? "Toiminnot" : "Дії";
  if (strcmp(key, "open_admin") == 0) return en ? "Open admin" : no ? "Åpne admin" : da ? "Aben admin" : sv ? "Oppna admin" : fi ? "Avaa admin" : "Відкрити адмін";
  if (strcmp(key, "admin_protected") == 0) return en ? "Admin is password protected. On first login you must change password." : no ? "Admin er beskyttet med passord. Ved første innlogging må du endre passord." : da ? "Admin er adgangskodebeskyttet. Ved første login skal du ændre adgangskode." : sv ? "Admin ar losenskyddad. Vid forsta inloggning maste du byta losenord." : fi ? "Admin on suojattu salasanalla. Vaihda salasana ensimmaisella kirjautumisella." : "Адмін захищений паролем. Під час першого входу змініть пароль.";
  if (strcmp(key, "manual") == 0) return en ? "Manual" : no ? "Brukermanual" : da ? "Brugermanual" : sv ? "Manual" : fi ? "Kayttoopas" : "Інструкція";
  if (strcmp(key, "back_home") == 0) return en ? "Home" : no ? "Til start" : da ? "Til start" : sv ? "Till startsida" : fi ? "Etusivu" : "На головну";
  if (strcmp(key, "saved") == 0) return en ? "Saved" : no ? "Lagret" : da ? "Gemt" : sv ? "Sparat" : fi ? "Tallennettu" : "Збережено";
  if (strcmp(key, "password_updated") == 0) return en ? "Password updated. Log in again with your new password." : no ? "Passord er oppdatert. Logg inn på nytt med nytt passord." : da ? "Adgangskode opdateret. Log ind igen med ny adgangskode." : sv ? "Losenord uppdaterat. Logga in igen med nytt losenord." : fi ? "Salasana paivitetty. Kirjaudu uudelleen uudella salasanalla." : "Пароль оновлено. Увійдіть з новим паролем.";
  if (strcmp(key, "settings_saved_restart_if_needed") == 0) return en ? "Settings saved. Restart the device if WiFi/network settings were changed." : no ? "Innstillinger er lagret. Restart enheten hvis WiFi eller nettverksvalg er endret." : da ? "Indstillinger gemt. Genstart enheden hvis WiFi/netværk er ændret." : sv ? "Installningar sparade. Starta om enheten om WiFi/natverk andrats." : fi ? "Asetukset tallennettu. Kaynnista laite uudelleen jos WiFi/verkko muuttui." : "Налаштування збережено. Перезапустіть пристрій, якщо змінено WiFi/мережу.";
  if (strcmp(key, "to_admin") == 0) return en ? "Back to admin" : no ? "Til admin" : da ? "Til admin" : sv ? "Till admin" : fi ? "Takaisin adminiin" : "До адмін";
  if (strcmp(key, "restarting_now") == 0) return en ? "Device is restarting now." : no ? "Enheten restarter nå." : da ? "Enheden genstarter nu." : sv ? "Enheten startar om nu." : fi ? "Laite kaynnistyy uudelleen nyt." : "Пристрій перезапускається.";
  if (strcmp(key, "factory_reset_now") == 0) return en ? "Configuration is being erased. Device will restart now." : no ? "Konfigurasjon slettes. Enheten restarter nå." : da ? "Konfiguration slettes. Enheden genstarter nu." : sv ? "Konfiguration raderas. Enheten startar om nu." : fi ? "Asetukset poistetaan. Laite kaynnistyy uudelleen nyt." : "Конфігурацію видаляють. Пристрій перезапускається.";
  if (strcmp(key, "ota") == 0) return en ? "OTA" : no ? "OTA" : da ? "OTA" : sv ? "OTA" : fi ? "OTA" : "OTA";
  if (strcmp(key, "ota_upload_title") == 0) return en ? "OTA via file upload (.bin)" : no ? "OTA via filopplasting (.bin)" : da ? "OTA via filupload (.bin)" : sv ? "OTA via filuppladdning (.bin)" : fi ? "OTA tiedoston latauksella (.bin)" : "OTA через завантаження файлу (.bin)";
  if (strcmp(key, "ota_upload_desc") == 0) return en ? "Upload ESP32 firmware file directly from browser." : no ? "Last opp firmwarefil for ESP32 direkte fra nettleser." : da ? "Upload firmwarefil til ESP32 direkte fra browser." : sv ? "Ladda upp firmwarefil for ESP32 direkt fran webblasare." : fi ? "Lataa ESP32-laiteohjelma selaimesta." : "Завантажте прошивку ESP32 з браузера.";
  if (strcmp(key, "firmware_file") == 0) return en ? "Firmware file (.bin)" : no ? "Firmware-fil (.bin)" : da ? "Firmware-fil (.bin)" : sv ? "Firmware-fil (.bin)" : fi ? "Laiteohjelmatiedosto (.bin)" : "Файл прошивки (.bin)";
  if (strcmp(key, "start_update") == 0) return en ? "Start update" : no ? "Start oppdatering" : da ? "Start opdatering" : sv ? "Starta uppdatering" : fi ? "Aloita paivitys" : "Почати оновлення";
  if (strcmp(key, "ota_restart_done") == 0) return en ? "Device restarts automatically when update is done." : no ? "Enheten restarter automatisk når oppdateringen er ferdig." : da ? "Enheden genstarter automatisk nar opdatering er faerdig." : sv ? "Enheten startar om automatiskt nar uppdateringen ar klar." : fi ? "Laite kaynnistyy automaattisesti paivityksen jalkeen." : "Пристрій автоматично перезапуститься після оновлення.";
  if (strcmp(key, "ota_alt_title") == 0) return en ? "Alternative: Arduino OTA" : no ? "Alternativ: Arduino OTA" : da ? "Alternativ: Arduino OTA" : sv ? "Alternativ: Arduino OTA" : fi ? "Vaihtoehto: Arduino OTA" : "Альтернатива: Arduino OTA";
  if (strcmp(key, "ota_alt_desc") == 0) return en ? "You can still use Arduino IDE network port if preferred." : no ? "Du kan fortsatt bruke Arduino IDE nettverksport dersom ønskelig." : da ? "Du kan stadig bruge Arduino IDE netvaerksport, hvis onsket." : sv ? "Du kan fortfarande anvanda Arduino IDE natverksport om du vill." : fi ? "Voit edelleen kayttaa Arduino IDE -verkkoporttia halutessasi." : "За бажанням можна використовувати мережевий порт Arduino IDE.";
  if (strcmp(key, "ota_failed") == 0) return en ? "OTA failed" : no ? "OTA feil" : da ? "OTA fejl" : sv ? "OTA fel" : fi ? "OTA virhe" : "Помилка OTA";
  if (strcmp(key, "back_to_ota") == 0) return en ? "Back to OTA" : no ? "Tilbake til OTA" : da ? "Tilbage til OTA" : sv ? "Tillbaka till OTA" : fi ? "Takaisin OTA:han" : "Назад до OTA";
  if (strcmp(key, "ota_ok") == 0) return en ? "OTA OK" : no ? "OTA OK" : da ? "OTA OK" : sv ? "OTA OK" : fi ? "OTA OK" : "OTA OK";
  if (strcmp(key, "ota_ok_restart") == 0) return en ? "Firmware updated. Device is restarting now." : no ? "Firmware er oppdatert. Enheten restarter nå." : da ? "Firmware er opdateret. Enheden genstarter nu." : sv ? "Firmware ar uppdaterad. Enheten startar om nu." : fi ? "Laiteohjelma paivitetty. Laite kaynnistyy uudelleen nyt." : "Прошивку оновлено. Пристрій перезапускається.";
  if (strcmp(key, "manual_subtitle") == 0) return en ? "Short changelog + simple guide" : no ? "Kort changelog + enkel veiledning" : da ? "Kort changelog + enkel guide" : sv ? "Kort changelog + enkel guide" : fi ? "Lyhyt changelog + yksinkertainen opas" : "Короткий changelog + проста інструкція";
  if (strcmp(key, "changelog_short") == 0) return en ? "Changelog (short)" : no ? "Changelog (kort)" : da ? "Changelog (kort)" : sv ? "Changelog (kort)" : fi ? "Changelog (lyhyt)" : "Changelog (коротко)";
  if (strcmp(key, "manual_simple") == 0) return en ? "Manual (simple)" : no ? "Brukermanual (forenklet)" : da ? "Brugermanual (forenklet)" : sv ? "Manual (forenklad)" : fi ? "Kayttoopas (yksinkertainen)" : "Інструкція (спрощено)";
  if (strcmp(key, "to_admin_page") == 0) return en ? "Back to admin" : no ? "Tilbake til admin" : da ? "Tilbage til admin" : sv ? "Tillbaka till admin" : fi ? "Takaisin adminiin" : "До адмін";
  return String(key);
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

static void applyPostedModbusSettings()
{
  if (server.hasArg("mbtr")) g_cfg->modbus_transport_mode = normTransport(server.arg("mbtr"));
  if (server.hasArg("mbser")) g_cfg->modbus_serial_format = normSerialFmt(server.arg("mbser"));

  if (server.hasArg("mbbaud"))
  {
    uint32_t mbBaud = (uint32_t)server.arg("mbbaud").toInt();
    if (mbBaud >= 1200 && mbBaud <= 115200) g_cfg->modbus_baud = mbBaud;
  }

  if (server.hasArg("mbid"))
  {
    int mbId = server.arg("mbid").toInt();
    if (mbId >= 1 && mbId <= 247) g_cfg->modbus_slave_id = (uint8_t)mbId;
  }

  if (server.hasArg("mboff"))
  {
    int mbOff = server.arg("mboff").toInt();
    if (mbOff < -5) mbOff = -5;
    if (mbOff > 5) mbOff = 5;
    g_cfg->modbus_addr_offset = (int8_t)mbOff;
  }
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
  String fw = jsonEscape(String(FW_VERSION));

  uint64_t tsEpochMs = 0;
  {
    struct timeval tv;
    if (gettimeofday(&tv, nullptr) == 0)
    {
      tsEpochMs = ((uint64_t)tv.tv_sec * 1000ULL) + ((uint64_t)tv.tv_usec / 1000ULL);
    }
  }

  String tsIso = "";
  {
    time_t now = time(nullptr);
    if (now > 100000)
    {
      struct tm tmv;
      localtime_r(&now, &tmv);
      char b[40];
      strftime(b, sizeof(b), "%Y-%m-%dT%H:%M:%S%z", &tmv);
      tsIso = b;
    }
  }
  tsIso = jsonEscape(tsIso);

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

  char buf[900];

  if (!pretty)
  {
    snprintf(buf, sizeof(buf),
      "{"
        "\"ts_epoch_ms\":%llu,"
        "\"ts_iso\":\"%s\","
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
        "\"fw\":\"%s\","
        "\"wifi\":\"%s\","
        "\"ip\":\"%s\","
        "\"modbus\":\"%s\""
      "}",
      (unsigned long long)tsEpochMs,
      tsIso.c_str(),
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
      fw.c_str(),
      wifi.c_str(),
      ip.c_str(),
      mb.c_str()
    );
    return String(buf);
  }

  snprintf(buf, sizeof(buf),
    "{\n"
    "  \"ts_epoch_ms\": %llu,\n"
    "  \"ts_iso\": \"%s\",\n"
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
    "  \"fw\": \"%s\",\n"
    "  \"wifi\": \"%s\",\n"
    "  \"ip\": \"%s\",\n"
    "  \"modbus\": \"%s\"\n"
    "}\n",
    (unsigned long long)tsEpochMs,
    tsIso.c_str(),
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
    fw.c_str(),
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

static void applyModbusApiRuntime()
{
  FlexitModbusRuntimeConfig mcfg;
  mcfg.model = g_cfg->model;
  mcfg.baud = g_cfg->modbus_baud;
  mcfg.slave_id = g_cfg->modbus_slave_id;
  mcfg.addr_offset = g_cfg->modbus_addr_offset;
  mcfg.serial_format = g_cfg->modbus_serial_format;
  mcfg.transport_mode = g_cfg->modbus_transport_mode;
  flexit_modbus_set_runtime_config(mcfg);
  flexit_modbus_set_enabled(g_cfg->modbus_enabled);
}

static void handleHaStatus()
{
  if (!g_cfg->ha_enabled)
  {
    server.send(403, "text/plain", "home assistant/api disabled");
    return;
  }
  handleStatus();
}

static void handleControlMode()
{
  if (!tokenOK()) { server.send(401, "text/plain", "missing/invalid token"); return; }
  if (!g_cfg->control_enabled) { server.send(403, "text/plain", "control disabled"); return; }
  if (!g_cfg->modbus_enabled) { server.send(409, "text/plain", "modbus disabled"); return; }
  if (!server.hasArg("mode")) { server.send(400, "text/plain", "missing mode"); return; }

  applyModbusApiRuntime();
  if (!flexit_modbus_write_mode(server.arg("mode")))
  {
    server.send(500, "text/plain", String("write mode failed: ") + flexit_modbus_last_error());
    return;
  }

  server.send(200, "application/json", "{\"ok\":true}");
}

static void handleControlSetpoint()
{
  if (!tokenOK()) { server.send(401, "text/plain", "missing/invalid token"); return; }
  if (!g_cfg->control_enabled) { server.send(403, "text/plain", "control disabled"); return; }
  if (!g_cfg->modbus_enabled) { server.send(409, "text/plain", "modbus disabled"); return; }
  if (!server.hasArg("profile") || !server.hasArg("value"))
  {
    server.send(400, "text/plain", "missing profile/value");
    return;
  }

  const String profile = server.arg("profile");
  const float value = server.arg("value").toFloat();
  applyModbusApiRuntime();
  if (!flexit_modbus_write_setpoint(profile, value))
  {
    server.send(500, "text/plain", String("write setpoint failed: ") + flexit_modbus_last_error());
    return;
  }

  server.send(200, "application/json", "{\"ok\":true}");
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
      s += ".sep-gold{height:1px;background:var(--accent);opacity:.65;margin:12px 0;}";
      s += ".lang{padding:8px 10px;border-radius:10px;border:1px solid var(--border);background:var(--card);color:var(--text);}";
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
      s += "<div style='display:flex;gap:8px;align-items:center;'>";
      if (g_cfg)
      {
        s += "<form method='POST' action='/admin/lang' style='margin:0'>";
        s += "<label style='display:none'>" + tr("language") + "</label>";
        s += "<select class='lang' name='lang' onchange='this.form.submit()'>";
        const String cl = lang();
        s += "<option value='no'" + String(cl == "no" ? " selected" : "") + ">NO</option>";
        s += "<option value='da'" + String(cl == "da" ? " selected" : "") + ">DA</option>";
        s += "<option value='sv'" + String(cl == "sv" ? " selected" : "") + ">SV</option>";
        s += "<option value='fi'" + String(cl == "fi" ? " selected" : "") + ">FI</option>";
        s += "<option value='en'" + String(cl == "en" ? " selected" : "") + ">EN</option>";
        s += "<option value='uk'" + String(cl == "uk" ? " selected" : "") + ">UKR</option>";
        s += "</select></form>";
      }
      s += "<button class='btn secondary' type='button' onclick='toggleTheme()'>☾ / ☀</button>";
      s += "</div>";
      s += "</div>";
      return s;
    }

static String pageFooter(){ return "</div><div style='margin-top:20px'><form method='POST' action='/admin/reboot'><button class=\"btn\">" + tr("restart_now") + "</button></form></div></body></html>"; }

static void handleRoot()
{
  String s = pageHeader("Flexit-reader", tr("admin_portal"));

  bool apOn  = (WiFi.getMode() & WIFI_AP);
  bool staOn = (WiFi.status() == WL_CONNECTED);

  s += "<div class='grid'>";
  s += "<div class='card'><h2>" + tr("status") + "</h2>";
  s += "<div class='kpi'>";
  s += "<div class='kv'><div class='k'>STA WiFi</div><div class='v'>" + String(staOn ? "Connected" : "Offline") + "</div></div>";
  s += "<div class='kv'><div class='k'>Fallback AP</div><div class='v'>" + String(apOn ? "ON" : "OFF") + "</div></div>";
  s += "<div class='kv'><div class='k'>mDNS</div><div class='v'>ventreader.local</div></div>";
  s += "</div>";
  s += "<div class='help'>API: <code>/status?token=...</code> (Homey polling). Debug: <code>&pretty=1</code></div>";
  s += "</div>";

  s += "<div class='card'><h2>" + tr("actions") + "</h2>";
  s += "<div class='actions'>";
  s += "<a class='btn' href='/admin'>" + tr("open_admin") + "</a>";
  s += "<a class='btn secondary' href='/health'>/health</a>";
  s += "</div>";
  s += "<div class='help'>" + tr("admin_protected") + "</div>";
  s += "</div>";

  s += "</div>"; // grid
  s += pageFooter();
  server.send(200, "text/html", s);
}

static void handleAdminLang()
{
  if (!checkAdminAuth()) return;
  if (server.hasArg("lang"))
  {
    g_cfg->ui_language = normLang(server.arg("lang"));
    config_save(*g_cfg);
  }
  redirectTo("/admin");
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
    s += "<select id='model_setup' name='model' class='input'>";
    s += "<option value='S3'" + String(g_cfg->model == "S3" ? " selected" : "") + ">Nordic S3</option>";
    s += "<option value='S4'" + String(g_cfg->model == "S4" ? " selected" : "") + ">Nordic S4</option>";
    s += "<option value='S2_EXP'" + String(g_cfg->model == "S2_EXP" ? " selected" : "") + ">Nordic S2 (Experimental)</option>";
    s += "<option value='S7_EXP'" + String(g_cfg->model == "S7_EXP" ? " selected" : "") + ">Nordic S7 (Experimental)</option>";
    s += "<option value='CL3_EXP'" + String(g_cfg->model == "CL3_EXP" ? " selected" : "") + ">Nordic CL3 (Experimental)</option>";
    s += "<option value='CL4_EXP'" + String(g_cfg->model == "CL4_EXP" ? " selected" : "") + ">Nordic CL4 (Experimental)</option>";
    s += "</select>";
    s += "<div class='sep-gold'></div>";
    s += "<label>API-token (for /status)</label><input class='mono' name='token' value='" + jsonEscape(g_cfg->api_token) + "' required>";
    s += "<div class='sep-gold'></div>";
    s += "<label><input type='checkbox' name='homey' " + String(g_cfg->homey_enabled ? "checked" : "") + "> " + tr("homey_api") + "</label>";
    s += "<label><input type='checkbox' name='ha' " + String(g_cfg->ha_enabled ? "checked" : "") + "> " + tr("ha_api") + "</label>";
    s += "<div class='sep-gold'></div>";
    s += "<label><input id='mb_toggle_setup' type='checkbox' name='modbus' " + String(g_cfg->modbus_enabled ? "checked" : "") + "> Modbus</label>";
    s += "<div id='mb_adv_setup' style='display:" + String(g_cfg->modbus_enabled ? "block" : "none") + ";'>";
    s += "<div class='help'>Avanserte Modbus-innstillinger</div>";
    s += "<label><input type='checkbox' name='ctrl' " + String(g_cfg->control_enabled ? "checked" : "") + "> " + tr("control_enable") + "</label>";
    s += "<select id='mbtr_setup' name='mbtr' class='input'>";
    s += "<option value='AUTO'" + String(g_cfg->modbus_transport_mode == "AUTO" ? " selected" : "") + ">AUTO (anbefalt for auto-dir modul)</option>";
    s += "<option value='MANUAL'" + String(g_cfg->modbus_transport_mode == "MANUAL" ? " selected" : "") + ">MANUAL (DE/RE styres av GPIO)</option>";
    s += "</select>";
    s += "<div class='row'>";
    s += "<div><label>Modbus baud</label><input id='mbbaud_setup' name='mbbaud' type='number' min='1200' max='115200' value='" + String(g_cfg->modbus_baud) + "'></div>";
    s += "<div><label>Slave ID</label><input id='mbid_setup' name='mbid' type='number' min='1' max='247' value='" + String((int)g_cfg->modbus_slave_id) + "'></div>";
    s += "<div><label>Addr offset</label><input id='mboff_setup' name='mboff' type='number' min='-5' max='5' value='" + String((int)g_cfg->modbus_addr_offset) + "'></div>";
    s += "</div>";
    s += "<label>Serial format</label>";
    s += "<select id='mbser_setup' name='mbser' class='input'>";
    s += "<option value='8N1'" + String(g_cfg->modbus_serial_format == "8N1" ? " selected" : "") + ">8N1</option>";
    s += "<option value='8E1'" + String(g_cfg->modbus_serial_format == "8E1" ? " selected" : "") + ">8E1</option>";
    s += "<option value='8O1'" + String(g_cfg->modbus_serial_format == "8O1" ? " selected" : "") + ">8O1</option>";
    s += "</select>";
    s += "</div>";
    s += "<script>(function(){"
         "var t=document.getElementById('mb_toggle_setup');var a=document.getElementById('mb_adv_setup');"
         "var m=document.getElementById('model_setup');var tr=document.getElementById('mbtr_setup');"
         "var sf=document.getElementById('mbser_setup');var bd=document.getElementById('mbbaud_setup');"
         "var id=document.getElementById('mbid_setup');var of=document.getElementById('mboff_setup');"
         "if(!t||!a)return;"
         "function u(){a.style.display=t.checked?'block':'none';}"
         "function p(model){tr.value='AUTO';sf.value='8N1';bd.value='19200';id.value='1';of.value='0';}"
         "t.addEventListener('change',u);"
         "if(m){m.addEventListener('change',function(){if(t.checked){p(m.value);}});}"
         "u();})();</script>";
    s += "<div class='sep-gold'></div>";
    s += "<label>" + tr("poll_sec") + "</label><input name='poll' type='number' min='30' max='3600' value='" + String(g_cfg->poll_interval_ms/1000) + "'>";
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
  bool modelChanged = false;
  if (server.hasArg("model"))
  {
    String nm = normModel(server.arg("model"));
    modelChanged = (nm != g_cfg->model);
    g_cfg->model = nm;
  }
  if (modelChanged) config_apply_model_modbus_defaults(*g_cfg, true);

  g_cfg->api_token = server.arg("token");
  g_cfg->modbus_enabled = server.hasArg("modbus");
  g_cfg->homey_enabled  = server.hasArg("homey");
  g_cfg->ha_enabled     = server.hasArg("ha");
  g_cfg->control_enabled = server.hasArg("ctrl");
  if (server.hasArg("lang")) g_cfg->ui_language = normLang(server.arg("lang"));
  applyPostedModbusSettings();

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
  s += "<select id='model_admin' name='model' class='input'>";
  s += "<option value='S3'" + String(g_cfg->model == "S3" ? " selected" : "") + ">Nordic S3</option>";
  s += "<option value='S4'" + String(g_cfg->model == "S4" ? " selected" : "") + ">Nordic S4</option>";
  s += "<option value='S2_EXP'" + String(g_cfg->model == "S2_EXP" ? " selected" : "") + ">Nordic S2 (Experimental)</option>";
  s += "<option value='S7_EXP'" + String(g_cfg->model == "S7_EXP" ? " selected" : "") + ">Nordic S7 (Experimental)</option>";
  s += "<option value='CL3_EXP'" + String(g_cfg->model == "CL3_EXP" ? " selected" : "") + ">Nordic CL3 (Experimental)</option>";
  s += "<option value='CL4_EXP'" + String(g_cfg->model == "CL4_EXP" ? " selected" : "") + ">Nordic CL4 (Experimental)</option>";
  s += "</select>";
  s += "<div class='sep-gold'></div>";
  s += "<label>API-token (for /status)</label><input class='mono' name='token' value='" + jsonEscape(g_cfg->api_token) + "' required>";
  s += "<div class='sep-gold'></div>";
  s += "<label><input type='checkbox' name='homey' " + String(g_cfg->homey_enabled ? "checked" : "") + "> " + tr("homey_api") + "</label>";
  s += "<label><input type='checkbox' name='ha' " + String(g_cfg->ha_enabled ? "checked" : "") + "> " + tr("ha_api") + "</label>";
  s += "<div class='sep-gold'></div>";
  s += "<label><input id='mb_toggle_admin' type='checkbox' name='modbus' " + String(g_cfg->modbus_enabled ? "checked" : "") + "> Modbus</label>";
  s += "<div id='mb_adv_admin' style='display:" + String(g_cfg->modbus_enabled ? "block" : "none") + ";'>";
  s += "<div class='help'>Avanserte Modbus-innstillinger</div>";
  s += "<label><input type='checkbox' name='ctrl' " + String(g_cfg->control_enabled ? "checked" : "") + "> " + tr("control_enable") + "</label>";
  s += "<label>Modbus transport</label>";
  s += "<select id='mbtr_admin' name='mbtr' class='input'>";
  s += "<option value='AUTO'" + String(g_cfg->modbus_transport_mode == "AUTO" ? " selected" : "") + ">AUTO (anbefalt for auto-dir modul)</option>";
  s += "<option value='MANUAL'" + String(g_cfg->modbus_transport_mode == "MANUAL" ? " selected" : "") + ">MANUAL (DE/RE styres av GPIO)</option>";
  s += "</select>";
  s += "<div class='row'>";
  s += "<div><label>Modbus baud</label><input id='mbbaud_admin' name='mbbaud' type='number' min='1200' max='115200' value='" + String(g_cfg->modbus_baud) + "'></div>";
  s += "<div><label>Slave ID</label><input id='mbid_admin' name='mbid' type='number' min='1' max='247' value='" + String((int)g_cfg->modbus_slave_id) + "'></div>";
  s += "<div><label>Addr offset</label><input id='mboff_admin' name='mboff' type='number' min='-5' max='5' value='" + String((int)g_cfg->modbus_addr_offset) + "'></div>";
  s += "</div>";
  s += "<label>Serial format</label>";
  s += "<select id='mbser_admin' name='mbser' class='input'>";
  s += "<option value='8N1'" + String(g_cfg->modbus_serial_format == "8N1" ? " selected" : "") + ">8N1</option>";
  s += "<option value='8E1'" + String(g_cfg->modbus_serial_format == "8E1" ? " selected" : "") + ">8E1</option>";
  s += "<option value='8O1'" + String(g_cfg->modbus_serial_format == "8O1" ? " selected" : "") + ">8O1</option>";
  s += "</select>";
  s += "</div>";
  s += "<script>(function(){"
       "var t=document.getElementById('mb_toggle_admin');var a=document.getElementById('mb_adv_admin');"
       "var m=document.getElementById('model_admin');var tr=document.getElementById('mbtr_admin');"
       "var sf=document.getElementById('mbser_admin');var bd=document.getElementById('mbbaud_admin');"
       "var id=document.getElementById('mbid_admin');var of=document.getElementById('mboff_admin');"
       "if(!t||!a)return;"
       "function u(){a.style.display=t.checked?'block':'none';}"
       "function p(model){tr.value='AUTO';sf.value='8N1';bd.value='19200';id.value='1';of.value='0';}"
       "t.addEventListener('change',u);"
       "if(m){m.addEventListener('change',function(){if(t.checked){p(m.value);}});}"
       "u();})();</script>";
  s += "<div class='sep-gold'></div>";
  s += "<label>" + tr("poll_sec") + "</label><input name='poll' type='number' min='30' max='3600' value='" + String(g_cfg->poll_interval_ms/1000) + "'>";
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
  s += "<a class='btn secondary' href='/admin/manual'>" + tr("manual") + "</a>";
  s += "<a class='btn secondary' href='/admin/ota'>" + tr("ota") + "</a>";
  s += "<a class='btn secondary' href='/'>" + tr("back_home") + "</a>";
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

static void handleAdminManual()
{
  if (!checkAdminAuth()) return;
  if (!g_cfg->setup_completed)
  {
    redirectTo("/admin/setup?step=1");
    return;
  }

  const bool noLang = (lang() == "no");
  String s = pageHeader(tr("manual"), tr("manual_subtitle"));
  s += "<div class='grid'>";

  s += "<div class='card'><h2>" + tr("changelog_short") + "</h2>";
  s += "<div><strong>v3.5.0</strong></div>";
  s += "<div class='help'>";
  if (noLang)
    s += "Status-JSON har nå ts_epoch_ms/ts_iso for grafer, samt forbedret språkstyring i admin og ePaper.";
  else
    s += "Status JSON now includes ts_epoch_ms/ts_iso for graphing, plus improved language coverage in admin and ePaper.";
  s += "</div>";
  s += "<div class='sep-gold'></div>";
  s += "<div><strong>v3.1.0</strong></div>";
  s += "<div class='help'>";
  if (noLang)
    s += "Språkvalg i admin/setup, Home Assistant-endepunkt, eksperimentell styring, ekstra modellvalg og dokumentasjon.";
  else
    s += "Language selector in admin/setup, Home Assistant endpoint, experimental control, extra model options and docs.";
  s += "</div>";
  s += "</div>";

  s += "<div class='card'><h2>" + tr("manual_simple") + "</h2>";
  s += "<div><strong>1) " + String(noLang ? "Forstegangsoppsett" : "First setup") + "</strong></div>";
  s += "<div class='help'>";
  if (noLang)
    s += "Logg inn p&aring; <code>/admin</code> med fabrikkpassord, fullf&oslash;r wizard og restart enheten.";
  else
    s += "Log in at <code>/admin</code> with factory password, complete wizard, then restart the device.";
  s += "</div>";
  s += "<div class='sep-gold'></div>";
  s += "<div><strong>2) " + String(noLang ? "Daglig bruk" : "Daily use") + "</strong></div>";
  s += "<div class='help'>";
  if (noLang)
    s += "Skjermen viser live verdier. API-status leses fra <code>/status?token=...</code> (eller <code>/ha/status?token=...</code>).";
  else
    s += "Display shows live values. Read API status from <code>/status?token=...</code> (or <code>/ha/status?token=...</code>).";
  s += "</div>";
  s += "<div class='sep-gold'></div>";
  s += "<div><strong>3) Modbus</strong></div>";
  s += "<div class='help'>";
  if (noLang)
    s += "Modbus er AV som standard. N&aring;r Modbus aktiveres, vises avanserte innstillinger automatisk.";
  else
    s += "Modbus is OFF by default. When enabled, advanced settings appear automatically.";
  s += "</div>";
  s += "<div class='sep-gold'></div>";
  s += "<div><strong>4) Homey / Home Assistant</strong></div>";
  s += "<div class='help'>";
  if (noLang)
    s += "Aktiver Homey/API eller Home Assistant/API i admin. Bruk token-beskyttet API lokalt.";
  else
    s += "Enable Homey/API or Home Assistant/API in admin. Use token-protected local API.";
  s += "</div>";
  s += "<div class='sep-gold'></div>";
  s += "<div><strong>5) " + String(noLang ? "Fjernstyring (eksperimentell)" : "Remote control (experimental)") + "</strong></div>";
  s += "<div class='help'>";
  if (noLang)
    s += "Kun aktiv n&aring;r <code>Modbus</code> og <code>Enable remote control writes</code> er p&aring;.";
  else
    s += "Only active when <code>Modbus</code> and <code>Enable remote control writes</code> are enabled.";
  s += " API: <code>POST /api/control/mode</code>, <code>POST /api/control/setpoint</code>.";
  s += "</div>";
  s += "<div class='sep-gold'></div>";
  s += "<div><strong>6) " + String(noLang ? "OTA-oppdatering" : "OTA update") + "</strong></div>";
  s += "<div class='help'>";
  if (noLang)
    s += "G&aring; til <code>/admin/ota</code>, last opp firmwarefil (.bin), enheten restarter automatisk.";
  else
    s += "Go to <code>/admin/ota</code>, upload firmware (.bin), device restarts automatically.";
  s += "</div>";
  s += "<div class='sep-gold'></div>";
  s += "<div><strong>7) " + String(noLang ? "Feilsoking" : "Troubleshooting") + "</strong></div>";
  s += "<div class='help'>";
  if (noLang)
    s += "Sjekk <code>/health</code> og <code>/status?token=...&pretty=1</code>. Ved Modbus-feil brukes siste gyldige data.";
  else
    s += "Check <code>/health</code> and <code>/status?token=...&pretty=1</code>. On Modbus errors, last good data is used.";
  s += "</div>";
  s += "<div class='actions' style='margin-top:16px'><a class='btn' href='/admin'>" + tr("to_admin_page") + "</a></div>";
  s += "</div>";

  s += "</div>";
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
  bool modelChanged = false;
  if (server.hasArg("model"))
  {
    String nm = normModel(server.arg("model"));
    modelChanged = (nm != g_cfg->model);
    g_cfg->model = nm;
  }
  if (modelChanged) config_apply_model_modbus_defaults(*g_cfg, true);

  // Modules
  g_cfg->modbus_enabled = server.hasArg("modbus");
  g_cfg->homey_enabled  = server.hasArg("homey");
  g_cfg->ha_enabled     = server.hasArg("ha");
  g_cfg->control_enabled = server.hasArg("ctrl");
  if (server.hasArg("lang")) g_cfg->ui_language = normLang(server.arg("lang"));
  applyPostedModbusSettings();

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
    String s = pageHeader(tr("admin"), tr("saved"));
    s += "<div class='card'><h2>" + tr("saved") + "</h2>";
    s += "<p>" + tr("password_updated") + "</p>";
    s += "<div class='actions'><a class='btn' href='/admin'>" + tr("to_admin") + "</a></div>";
    s += "</div>";
    s += pageFooter();
    server.send(200, "text/html", s);
    return;
  }

  config_save(*g_cfg);
  String s = pageHeader(tr("admin"), tr("saved"));
  s += "<div class='card'><h2>" + tr("saved") + "</h2>";
  s += "<p>" + tr("settings_saved_restart_if_needed") + "</p>";
  s += "<div class='actions'><a class='btn' href='/admin'>" + tr("to_admin") + "</a></div>";
  s += "</div>";
  s += pageFooter();
  server.send(200, "text/html", s);
}

static void handleReboot()
{
  if (!checkAdminAuth()) return;
  String s = pageHeader(tr("admin"), tr("restart"));
  s += "<div class='card'><h2>" + tr("restart") + "</h2><p>" + tr("restarting_now") + "</p></div>";
  s += pageFooter();
  server.send(200, "text/html", s);
  delay(300);
  ESP.restart();
}

static void handleFactoryReset()
{
  if (!checkAdminAuth()) return;
  String s = pageHeader(tr("admin"), tr("factory_reset"));
  s += "<div class='card'><h2>" + tr("factory_reset") + "</h2><p>" + tr("factory_reset_now") + "</p></div>";
  s += pageFooter();
  server.send(200, "text/html", s);
  delay(300);

  config_factory_reset(); // wipes NVS keys
  delay(200);
  ESP.restart();
}

static void handleAdminOta()
{
  if (!checkAdminAuth()) return;

  String s = pageHeader(tr("ota"));
  s += "<div class='card'><h2>" + tr("ota_upload_title") + "</h2>";
  s += "<p>" + tr("ota_upload_desc") + "</p>";
  s += "<form method='POST' action='/admin/ota_upload' enctype='multipart/form-data'>";
  s += "<label>" + tr("firmware_file") + "</label>";
  s += "<input type='file' name='firmware' accept='.bin,application/octet-stream' required>";
  s += "<div class='actions'><button class='btn' type='submit'>" + tr("start_update") + "</button></div>";
  s += "</form>";
  s += "<div class='help'>" + tr("ota_restart_done") + "</div>";
  s += "</div>";
  s += "<div class='card'><h2>" + tr("ota_alt_title") + "</h2>";
  s += "<p>" + tr("ota_alt_desc") + "</p>";
  s += "</div>";
  s += "<div class='card'><a class='btn' href='/admin'>" + tr("back") + "</a></div>";
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
    String s = pageHeader(tr("admin"), tr("ota_failed"));
    s += "<div class='card'><h2>" + tr("ota_failed") + "</h2><p>";
    s += String(b);
    s += "</p><div class='actions'><a class='btn' href='/admin/ota'>" + tr("back_to_ota") + "</a></div></div>";
    s += pageFooter();
    server.send(500, "text/html", s);
    return;
  }

  String s = pageHeader(tr("admin"), tr("ota"));
  s += "<div class='card'><h2>" + tr("ota_ok") + "</h2><p>" + tr("ota_ok_restart") + "</p></div>";
  s += pageFooter();
  server.send(200, "text/html", s);
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
    if (!g_cfg->homey_enabled && !g_cfg->ha_enabled)
    {
      server.send(403, "text/plain", "api disabled");
      return;
    }
    handleStatus();
  });
  server.on("/ha/status", HTTP_GET, handleHaStatus);
  server.on("/api/control/mode", HTTP_POST, handleControlMode);
  server.on("/api/control/setpoint", HTTP_POST, handleControlSetpoint);

  // Admin
  server.on("/admin", HTTP_GET, handleAdmin);
  server.on("/admin/manual", HTTP_GET, handleAdminManual);
  server.on("/admin/lang", HTTP_POST, handleAdminLang);
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
