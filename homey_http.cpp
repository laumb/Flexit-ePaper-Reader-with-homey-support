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
#include "flexit_web.h"

static WebServer server(80);

static DeviceConfig* g_cfg = nullptr;

// Wizard state (avoid BasicAuth re-login until Finish)
static String g_pending_admin_pass;
static bool   g_pending_admin_set = false;

// Deferred restart after HTTP response
static bool   g_restart_pending = false;
static uint32_t g_restart_at_ms = 0;
static bool   g_refresh_requested = false;

// Cached payload for /status
static FlexitData g_data;
static String g_mb = "MB OFF";

static bool checkAdminAuth();

static uint64_t nowEpochMs()
{
  struct timeval tv;
  if (gettimeofday(&tv, nullptr) != 0) return 0;
  return ((uint64_t)tv.tv_sec * 1000ULL) + ((uint64_t)tv.tv_usec / 1000ULL);
}

static String isoFromEpochMs(uint64_t ms)
{
  if (ms < 1000ULL) return "";
  time_t sec = (time_t)(ms / 1000ULL);
  struct tm tmv;
  localtime_r(&sec, &tmv);
  char b[40];
  strftime(b, sizeof(b), "%Y-%m-%dT%H:%M:%S%z", &tmv);
  return String(b);
}

struct StatusSnapshot
{
  uint64_t ts_epoch_ms = 0;
  float uteluft = NAN;
  float tilluft = NAN;
  float avtrekk = NAN;
  float avkast = NAN;
  int fan = 0;
  int heat = 0;
  int efficiency = 0;
  char mode[16] = {0};
  char modbus[32] = {0};
  bool stale = false;
};

static const size_t HISTORY_CAP = 720; // up to 12h at 60s polling, more at longer intervals
static StatusSnapshot g_hist[HISTORY_CAP];
static size_t g_hist_head = 0;
static size_t g_hist_count = 0;
static uint32_t g_diag_total_updates = 0;
static uint32_t g_diag_mb_ok = 0;
static uint32_t g_diag_mb_err = 0;
static uint32_t g_diag_mb_off = 0;
static uint32_t g_diag_stale = 0;
static String g_diag_last_mb = "MB OFF";
static uint64_t g_diag_last_sample_ms = 0;

static uint32_t historyMemoryBytes()
{
  return (uint32_t)(sizeof(StatusSnapshot) * HISTORY_CAP);
}

static void historyPush(const FlexitData& d, const String& mbStatus)
{
  StatusSnapshot s;
  s.ts_epoch_ms = nowEpochMs();
  s.uteluft = d.uteluft;
  s.tilluft = d.tilluft;
  s.avtrekk = d.avtrekk;
  s.avkast = d.avkast;
  s.fan = d.fan_percent;
  s.heat = d.heat_element_percent;
  s.efficiency = d.efficiency_percent;
  strncpy(s.mode, d.mode.c_str(), sizeof(s.mode) - 1);
  strncpy(s.modbus, mbStatus.c_str(), sizeof(s.modbus) - 1);
  s.stale = (mbStatus.indexOf("stale") >= 0);

  g_hist[g_hist_head] = s;
  g_hist_head = (g_hist_head + 1) % HISTORY_CAP;
  if (g_hist_count < HISTORY_CAP) g_hist_count++;
}


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

static String u64ToString(uint64_t v)
{
  char b[24];
  snprintf(b, sizeof(b), "%llu", (unsigned long long)v);
  return String(b);
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

static String normDataSource(const String& in)
{
  if (in == "FLEXITWEB") return "FLEXITWEB";
  return "MODBUS";
}

static String dataSourceLabel(const String& src)
{
  if (normDataSource(src) == "FLEXITWEB") return "FlexitWeb Cloud";
  return "Modbus (local)";
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
  if (strcmp(key, "data_source") == 0) return en ? "Data source" : no ? "Datakilde" : da ? "Datakilde" : sv ? "Datakälla" : fi ? "Tietolähde" : "Джерело даних";
  if (strcmp(key, "source_modbus") == 0) return en ? "Modbus (local)" : no ? "Modbus (lokal)" : da ? "Modbus (lokal)" : sv ? "Modbus (lokal)" : fi ? "Modbus (paikallinen)" : "Modbus (локально)";
  if (strcmp(key, "source_flexitweb") == 0) return en ? "FlexitWeb Cloud (read-only)" : no ? "FlexitWeb Cloud (kun lesing)" : da ? "FlexitWeb Cloud (kun laesning)" : sv ? "FlexitWeb Cloud (endast lasning)" : fi ? "FlexitWeb Cloud (vain luku)" : "FlexitWeb Cloud (лише читання)";
  if (strcmp(key, "source_flexitweb_help") == 0) return en ? "Uses Flexit cloud login and API. Modbus writes are disabled in this mode." : no ? "Bruker Flexit cloud-innlogging og API. Modbus-skriving er deaktivert i denne modusen." : da ? "Bruger Flexit cloud-login og API. Modbus-skrivning er deaktiveret i denne tilstand." : sv ? "Anvander Flexit cloud-inloggning och API. Modbus-skrivning ar av i detta lage." : fi ? "Kayttaa Flexit cloud -kirjautumista ja API:a. Modbus-kirjoitus ei ole kaytossa tassa tilassa." : "Використовує вхід Flexit cloud та API. Запис Modbus вимкнено в цьому режимі.";
  if (strcmp(key, "cloud_poll_min") == 0) return en ? "Cloud polling interval (minutes, 5-60)" : no ? "Cloud polling-intervall (minutter, 5-60)" : da ? "Cloud polling-interval (minutter, 5-60)" : sv ? "Cloud pollingintervall (minuter, 5-60)" : fi ? "Cloud-pollausvali (minuuttia, 5-60)" : "Інтервал опитування cloud (хвилини, 5-60)";
  if (strcmp(key, "cloud_user") == 0) return en ? "Flexit login (email/user)" : no ? "Flexit login (e-post/bruker)" : da ? "Flexit login (e-mail/bruger)" : sv ? "Flexit login (e-post/anvandare)" : fi ? "Flexit-kirjautuminen (sahkoposti/kayttaja)" : "Логін Flexit (email/користувач)";
  if (strcmp(key, "cloud_pass") == 0) return en ? "Flexit password" : no ? "Flexit passord" : da ? "Flexit adgangskode" : sv ? "Flexit losenord" : fi ? "Flexit salasana" : "Пароль Flexit";
  if (strcmp(key, "cloud_serial") == 0) return en ? "Device serial (required)" : no ? "Enhetsserienummer (obligatorisk)" : da ? "Enhedsserienummer (obligatorisk)" : sv ? "Enhetens serienummer (obligatoriskt)" : fi ? "Laitteen sarjanumero (pakollinen)" : "Серійний номер пристрою (обов'язково)";
  if (strcmp(key, "cloud_adv") == 0) return en ? "Cloud endpoint overrides (advanced)" : no ? "Cloud endpoint-overstyring (avansert)" : da ? "Cloud endpoint-overstyring (avanceret)" : sv ? "Cloud endpoint-override (avancerat)" : fi ? "Cloud endpoint -ylikirjoitus (edistynyt)" : "Перевизначення cloud endpoint (розширено)";
  if (strcmp(key, "cloud_auth_url") == 0) return en ? "Auth URL" : no ? "Auth URL" : da ? "Auth URL" : sv ? "Auth URL" : fi ? "Auth URL" : "Auth URL";
  if (strcmp(key, "cloud_dev_url") == 0) return en ? "Device list URL" : no ? "Enhetsliste URL" : da ? "Enhedsliste URL" : sv ? "Enhetslista URL" : fi ? "Laite-lista URL" : "URL списку пристроїв";
  if (strcmp(key, "cloud_data_url") == 0) return en ? "Datapoint URL ({serial})" : no ? "Datapoint URL ({serial})" : da ? "Datapoint URL ({serial})" : sv ? "Datapoint URL ({serial})" : fi ? "Datapoint URL ({serial})" : "Datapoint URL ({serial})";
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
  if (strcmp(key, "graphs") == 0) return en ? "Graphs" : no ? "Grafer" : da ? "Grafer" : sv ? "Grafer" : fi ? "Kaaviot" : "Графіки";
  if (strcmp(key, "quick_control") == 0) return en ? "Quick control" : no ? "Hurtigstyring" : da ? "Hurtigstyring" : sv ? "Snabbstyrning" : fi ? "Pikaohjaus" : "Швидке керування";
  if (strcmp(key, "quick_control_help") == 0) return en ? "Writes mode and setpoint directly over Modbus." : no ? "Skriver modus og settpunkt direkte over Modbus." : da ? "Skriver tilstand og setpunkt direkte over Modbus." : sv ? "Skriver lage och borvarde direkt over Modbus." : fi ? "Kirjoittaa tilan ja asetusarvon suoraan Modbusiin." : "Записує режим і уставку безпосередньо через Modbus.";
  if (strcmp(key, "enable_control_hint") == 0) return en ? "Enable both Modbus and remote control writes to use quick control." : no ? "Aktiver både Modbus og fjernstyring med skriv for å bruke hurtigstyring." : da ? "Aktiver bade Modbus og fjernstyring med skriv for at bruge hurtigstyring." : sv ? "Aktivera bade Modbus och fjarrstyrning med skrivning for att anvanda snabbstyrning." : fi ? "Ota kayttoon Modbus ja etakirjoitus kayttaaksesi pikaohjausta." : "Увімкніть Modbus і віддалений запис для швидкого керування.";
  if (strcmp(key, "mode_away") == 0) return en ? "Away" : no ? "Borte" : da ? "Ude" : sv ? "Borta" : fi ? "Poissa" : "Away";
  if (strcmp(key, "mode_home") == 0) return en ? "Home" : no ? "Hjem" : da ? "Hjemme" : sv ? "Hemma" : fi ? "Koti" : "Home";
  if (strcmp(key, "mode_high") == 0) return en ? "High" : no ? "Hoy" : da ? "Hoj" : sv ? "Hog" : fi ? "Teho" : "High";
  if (strcmp(key, "mode_fire") == 0) return en ? "Fireplace" : no ? "Peis" : da ? "Pejs" : sv ? "Kamin" : fi ? "Takka" : "Fireplace";
  if (strcmp(key, "profile") == 0) return en ? "Profile" : no ? "Profil" : da ? "Profil" : sv ? "Profil" : fi ? "Profiili" : "Профіль";
  if (strcmp(key, "setpoint") == 0) return en ? "Setpoint (10..30 C)" : no ? "Settpunkt (10..30 C)" : da ? "Setpunkt (10..30 C)" : sv ? "Borvarde (10..30 C)" : fi ? "Asetusarvo (10..30 C)" : "Уставка (10..30 C)";
  if (strcmp(key, "apply_setpoint") == 0) return en ? "Apply setpoint" : no ? "Bruk settpunkt" : da ? "Anvend setpunkt" : sv ? "Applicera borvarde" : fi ? "Aseta arvo" : "Застосувати уставку";
  if (strcmp(key, "history_graphs_title") == 0) return en ? "History graphs" : no ? "Historikk-grafer" : da ? "Historik-grafer" : sv ? "Historik-grafer" : fi ? "Historia-kaaviot" : "Графіки історії";
  if (strcmp(key, "history_graphs_subtitle") == 0) return en ? "Local trends from /status/history" : no ? "Lokale trender fra /status/history" : da ? "Lokale trends fra /status/history" : sv ? "Lokala trender fran /status/history" : fi ? "Paikalliset trendit /status/history:sta" : "Локальні тренди з /status/history";
  if (strcmp(key, "refresh") == 0) return en ? "Refresh" : no ? "Oppdater" : da ? "Opdater" : sv ? "Uppdatera" : fi ? "Paivita" : "Оновити";
  if (strcmp(key, "history_limit") == 0) return en ? "Points" : no ? "Punkter" : da ? "Punkter" : sv ? "Punkter" : fi ? "Pisteet" : "Точки";
  if (strcmp(key, "loading") == 0) return en ? "Loading..." : no ? "Laster..." : da ? "Indlaeser..." : sv ? "Laddar..." : fi ? "Ladataan..." : "Завантаження...";
  if (strcmp(key, "temp_graph") == 0) return en ? "Temperatures" : no ? "Temperaturer" : da ? "Temperaturer" : sv ? "Temperaturer" : fi ? "Lampotilat" : "Температури";
  if (strcmp(key, "perf_graph") == 0) return en ? "Performance" : no ? "Ytelse" : da ? "Ydelse" : sv ? "Prestanda" : fi ? "Suorituskyky" : "Продуктивність";
  if (strcmp(key, "export_csv") == 0) return en ? "Export CSV" : no ? "Eksporter CSV" : da ? "Eksporter CSV" : sv ? "Exportera CSV" : fi ? "Vie CSV" : "Експорт CSV";
  if (strcmp(key, "storage_status") == 0) return en ? "Storage status" : no ? "Lagringsstatus" : da ? "Lagerstatus" : sv ? "Lagringsstatus" : fi ? "Tallennustila" : "Стан сховища";
  if (strcmp(key, "history_ram_only") == 0) return en ? "History uses fixed RAM ring-buffer only (no flash writes)." : no ? "Historikk bruker kun fast RAM-ringbuffer (ingen flash-skriving)." : da ? "Historik bruger kun fast RAM-ringbuffer (ingen flash-skrivning)." : sv ? "Historik anvander endast fast RAM-ringbuffert (ingen flash-skrivning)." : fi ? "Historia kayttaa vain kiinteaa RAM-rengaspuskuria (ei flash-kirjoitusta)." : "Історія використовує лише фіксований RAM-буфер (без запису у flash).";
  if (strcmp(key, "history_points") == 0) return en ? "History points" : no ? "Historikkpunkter" : da ? "Historikpunkter" : sv ? "Historikpunkter" : fi ? "Historian pisteet" : "Точки історії";
  if (strcmp(key, "history_mem") == 0) return en ? "History memory" : no ? "Historikkminne" : da ? "Historikhukommelse" : sv ? "Historikminne" : fi ? "Historiamuisti" : "Пам'ять історії";
  if (strcmp(key, "heap_free") == 0) return en ? "Free heap" : no ? "Ledig heap" : da ? "Ledig heap" : sv ? "Ledigt heap" : fi ? "Vapaa heap" : "Вільна heap";
  if (strcmp(key, "heap_min") == 0) return en ? "Min free heap" : no ? "Min ledig heap" : da ? "Min ledig heap" : sv ? "Min ledigt heap" : fi ? "Min vapaa heap" : "Мін. вільна heap";
  if (strcmp(key, "legend") == 0) return en ? "Legend (click to toggle)" : no ? "Forklaring (klikk for av/på)" : da ? "Forklaring (klik for til/fra)" : sv ? "Forklaring (klicka for pa/av)" : fi ? "Selite (napsauta paalle/pois)" : "Легенда (натисніть для вкл/викл)";
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

static void applyPostedFlexitWebSettings()
{
  if (server.hasArg("fwuser")) g_cfg->flexitweb_user = server.arg("fwuser");
  if (server.hasArg("fwpass"))
  {
    String p = server.arg("fwpass");
    if (p.length() > 0) g_cfg->flexitweb_pass = p;
  }
  if (server.hasArg("fwserial"))
  {
    String s = server.arg("fwserial");
    s.trim();
    s.toUpperCase();
    g_cfg->flexitweb_serial = s;
  }
  if (server.hasArg("fwauth")) g_cfg->flexitweb_auth_url = server.arg("fwauth");
  if (server.hasArg("fwdev")) g_cfg->flexitweb_device_url = server.arg("fwdev");
  if (server.hasArg("fwdata")) g_cfg->flexitweb_datapoint_url = server.arg("fwdata");
  if (server.hasArg("fwpoll"))
  {
    int m = server.arg("fwpoll").toInt();
    if (m < 5) m = 5;
    if (m > 60) m = 60;
    g_cfg->flexitweb_poll_minutes = (uint8_t)m;
  }
}

static bool flexitWebSerialValid(const String& serialIn, String* why = nullptr)
{
  String serial = serialIn;
  serial.trim();
  if (serial.length() < 6 || serial.length() > 32)
  {
    if (why) *why = "Serienummer m&aring; v&aelig;re 6-32 tegn.";
    return false;
  }
  for (size_t i = 0; i < serial.length(); i++)
  {
    char c = serial[i];
    const bool ok = ((c >= '0' && c <= '9') ||
                     (c >= 'A' && c <= 'Z') ||
                     (c >= 'a' && c <= 'z') ||
                     c == '-' || c == '_' || c == '.');
    if (!ok)
    {
      if (why) *why = "Serienummer kan kun inneholde A-Z, 0-9, -, _ og .";
      return false;
    }
  }
  return true;
}

static bool runFlexitWebPreflight(const DeviceConfig& cfg, FlexitData* outData = nullptr, String* why = nullptr)
{
  if (cfg.flexitweb_user.length() == 0 || cfg.flexitweb_pass.length() == 0)
  {
    if (why) *why = "Mangler FlexitWeb bruker/passord.";
    return false;
  }
  if (!flexitWebSerialValid(cfg.flexitweb_serial, why))
    return false;

  flexit_web_set_runtime_config(cfg);
  FlexitData t = g_data;
  if (!flexit_web_poll(t))
  {
    if (why) *why = String(flexit_web_last_error());
    return false;
  }
  if (outData) *outData = t;
  return true;
}

static String fOrNullCompact(float v)
{
  if (isnan(v)) return "null";
  char t[24];
  snprintf(t, sizeof(t), "%.1f", v);
  return String(t);
}

static void handleAdminFlexitWebTest()
{
  if (!checkAdminAuth()) return;

  DeviceConfig tmp = *g_cfg;
  tmp.data_source = "FLEXITWEB";
  if (server.hasArg("fwuser")) tmp.flexitweb_user = server.arg("fwuser");
  if (server.hasArg("fwpass"))
  {
    String p = server.arg("fwpass");
    if (p.length() > 0) tmp.flexitweb_pass = p;
  }
  if (server.hasArg("fwserial")) tmp.flexitweb_serial = server.arg("fwserial");
  tmp.flexitweb_serial.trim();
  tmp.flexitweb_serial.toUpperCase();
  if (server.hasArg("fwauth")) tmp.flexitweb_auth_url = server.arg("fwauth");
  if (server.hasArg("fwdev")) tmp.flexitweb_device_url = server.arg("fwdev");
  if (server.hasArg("fwdata")) tmp.flexitweb_datapoint_url = server.arg("fwdata");
  if (server.hasArg("fwpoll"))
  {
    int m = server.arg("fwpoll").toInt();
    if (m < 5) m = 5;
    if (m > 60) m = 60;
    tmp.flexitweb_poll_minutes = (uint8_t)m;
  }

  String why;
  FlexitData t = g_data;
  if (!runFlexitWebPreflight(tmp, &t, &why))
  {
    server.send(200, "application/json", String("{\"ok\":false,\"error\":\"") + jsonEscape(why) + "\"}");
    return;
  }

  String mb = "WEB OK (test)";
  String out;
  out.reserve(360);
  out += "{\"ok\":true,\"source_status\":\"WEB OK (test)\",\"data\":{";
  out += "\"mode\":\"" + jsonEscape(t.mode) + "\",";
  out += "\"uteluft\":" + fOrNullCompact(t.uteluft) + ",";
  out += "\"tilluft\":" + fOrNullCompact(t.tilluft) + ",";
  out += "\"avtrekk\":" + fOrNullCompact(t.avtrekk) + ",";
  out += "\"avkast\":" + fOrNullCompact(t.avkast) + ",";
  out += "\"fan\":" + String(t.fan_percent) + ",";
  out += "\"heat\":" + String(t.heat_element_percent) + ",";
  out += "\"efficiency\":" + String(t.efficiency_percent);
  out += "}}";
  server.send(200, "application/json", out);

  // keep live dashboard fresh after successful test
  g_data = t;
  g_mb = mb;
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
  String src  = jsonEscape(normDataSource(g_cfg->data_source));
  String model = jsonEscape(g_data.device_model);
  String fw = jsonEscape(String(FW_VERSION));

  const uint64_t tsEpochMs = nowEpochMs();
  String tsIso = jsonEscape(isoFromEpochMs(tsEpochMs));
  const bool stale = (g_mb.indexOf("stale") >= 0);

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

  char buf[1024];

  if (!pretty)
  {
    snprintf(buf, sizeof(buf),
      "{"
        "\"ts_epoch_ms\":%llu,"
        "\"ts_iso\":\"%s\","
        "\"stale\":%s,"
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
        "\"modbus\":\"%s\","
        "\"data_source\":\"%s\""
      "}",
      (unsigned long long)tsEpochMs,
      tsIso.c_str(),
      stale ? "true" : "false",
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
      mb.c_str(),
      src.c_str()
    );
    return String(buf);
  }

  snprintf(buf, sizeof(buf),
    "{\n"
    "  \"ts_epoch_ms\": %llu,\n"
    "  \"ts_iso\": \"%s\",\n"
    "  \"stale\": %s,\n"
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
    "  \"modbus\": \"%s\",\n"
    "  \"data_source\": \"%s\"\n"
    "}\n",
    (unsigned long long)tsEpochMs,
    tsIso.c_str(),
    stale ? "true" : "false",
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
    mb.c_str(),
    src.c_str()
  );
  return String(buf);
}

static String boolLabel(bool v)
{
  return v ? "ON" : "OFF";
}

static String currentBaseUrl()
{
  if (WiFi.status() == WL_CONNECTED) return String("http://") + WiFi.localIP().toString();
  if (g_data.ip.length() > 0) return String("http://") + g_data.ip;
  return "http://ventreader.local";
}

static String buildHomeyExportJson()
{
  const String base = currentBaseUrl();
  const String statusUrl = base + "/status?token=" + g_cfg->api_token;
  const bool controlActive = (normDataSource(g_cfg->data_source) == "MODBUS" && g_cfg->modbus_enabled && g_cfg->control_enabled);
  const uint64_t ts = nowEpochMs();
  const String tsIso = isoFromEpochMs(ts);
  const String tsStr = u64ToString(ts);
  const String ctrlModeUrl = base + "/api/control/mode?token=" + g_cfg->api_token + "&mode=HOME";
  const String ctrlSetpointUrl = base + "/api/control/setpoint?token=" + g_cfg->api_token + "&profile=home&value=20.5";

  String script;
  script.reserve(2600);
  script += "// VentReader -> Homey virtual devices\n";
  script += "const VENTREADER_URL = '";
  script += statusUrl;
  script += "';\n\n";
  script += "const MAP = {\n";
  script += "  'VentReader - Uteluft': { field: 'uteluft', cap: 'measure_temperature' },\n";
  script += "  'VentReader - Tilluft': { field: 'tilluft', cap: 'measure_temperature' },\n";
  script += "  'VentReader - Avtrekk': { field: 'avtrekk', cap: 'measure_temperature' },\n";
  script += "  'VentReader - Avkast':  { field: 'avkast',  cap: 'measure_temperature' },\n";
  script += "  'VentReader - Fan %':   { field: 'fan', cap: 'measure_percentage' },\n";
  script += "  'VentReader - Heat %':  { field: 'heat', cap: 'measure_percentage' },\n";
  script += "  'VentReader - Gjenvinning %': { field: 'efficiency', cap: 'measure_percentage' }\n";
  script += "};\n\n";
  script += "const ALARM_DEVICE = 'VentReader - Modbus Alarm'; // optional\n";
  script += "const ALARM_CAP = 'alarm_generic';\n";
  script += "const STATUS_DEVICE = 'VentReader - Status tekst'; // optional\n";
  script += "const STATUS_CAP = 'measure_text';\n\n";
  script += "function num(v){ if(v===null||v===undefined) return null; const n=Number(v); return Number.isFinite(n)?n:null; }\n";
  script += "async function setByName(devices,name,capability,value){\n";
  script += "  const d=Object.values(devices).find(x=>x.name===name);\n";
  script += "  if(!d||!d.capabilitiesObj||!d.capabilitiesObj[capability]) return;\n";
  script += "  await d.setCapabilityValue(capability,value);\n";
  script += "}\n\n";
  script += "const res = await fetch(VENTREADER_URL);\n";
  script += "if(!res.ok) throw new Error(`HTTP ${res.status}`);\n";
  script += "const s = await res.json();\n";
  script += "const devices = await Homey.devices.getDevices();\n\n";
  script += "for (const [name,cfg] of Object.entries(MAP)) {\n";
  script += "  const v=num(s[cfg.field]); if(v===null) continue;\n";
  script += "  await setByName(devices,name,cfg.cap,v);\n";
  script += "}\n\n";
  script += "if (ALARM_DEVICE) {\n";
  script += "  const st = String(s.modbus || '');\n";
  script += "  const bad = !(st.startsWith('MB OK') || st.startsWith('WEB OK'));\n";
  script += "  await setByName(devices,ALARM_DEVICE,ALARM_CAP,bad);\n";
  script += "}\n\n";
  script += "if (STATUS_DEVICE) {\n";
  script += "  const status = `${s.model||'N/A'} | ${s.mode||'N/A'} | ${s.modbus||'N/A'} | ${s.time||'--:--'}`;\n";
  script += "  await setByName(devices,STATUS_DEVICE,STATUS_CAP,status);\n";
  script += "}\n\n";
  script += "return `VentReader OK: ${s.time||'--:--'} ${s.modbus||''}`;\n";

  String out;
  out.reserve(7800);
  out += "{";
  out += "\"export_version\":\"1\",";
  out += "\"generated_at_epoch_ms\":";
  out += tsStr;
  out += ",";
  out += "\"generated_at_iso\":\"";
  out += jsonEscape(tsIso);
  out += "\",";
  out += "\"device\":{";
  out += "\"model\":\"" + jsonEscape(g_cfg->model) + "\",";
  out += "\"fw\":\"" + jsonEscape(String(FW_VERSION)) + "\",";
  out += "\"base_url\":\"" + jsonEscape(base) + "\",";
  out += "\"api_token\":\"" + jsonEscape(g_cfg->api_token) + "\"";
  out += "},";
  out += "\"modules\":{";
  out += "\"homey_api\":" + String(g_cfg->homey_enabled ? "true" : "false") + ",";
  out += "\"home_assistant_api\":" + String(g_cfg->ha_enabled ? "true" : "false") + ",";
  out += "\"modbus\":" + String(g_cfg->modbus_enabled ? "true" : "false") + ",";
  out += "\"control_writes\":" + String(g_cfg->control_enabled ? "true" : "false");
  out += "},";
  out += "\"endpoints\":{";
  out += "\"status\":\"" + jsonEscape(statusUrl) + "\",";
  out += "\"mode_write_example\":\"" + jsonEscape(ctrlModeUrl) + "\",";
  out += "\"setpoint_write_example\":\"" + jsonEscape(ctrlSetpointUrl) + "\"";
  out += "},";
  out += "\"recommended_poll_interval_seconds\":60,";
  out += "\"flow_notes\":[";
  out += "\"Trigger: every 1-2 minutes\",";
  out += "\"Action: run HomeyScript VentReader Poll\",";
  out += "\"Optional: alarm/push notification on script failure\"";
  out += "],";
  out += "\"recommended_virtual_devices\":[";
  out += "{\"name\":\"VentReader - Uteluft\",\"capability\":\"measure_temperature\"},";
  out += "{\"name\":\"VentReader - Tilluft\",\"capability\":\"measure_temperature\"},";
  out += "{\"name\":\"VentReader - Avtrekk\",\"capability\":\"measure_temperature\"},";
  out += "{\"name\":\"VentReader - Avkast\",\"capability\":\"measure_temperature\"},";
  out += "{\"name\":\"VentReader - Fan %\",\"capability\":\"measure_percentage\"},";
  out += "{\"name\":\"VentReader - Heat %\",\"capability\":\"measure_percentage\"},";
  out += "{\"name\":\"VentReader - Gjenvinning %\",\"capability\":\"measure_percentage\"},";
  out += "{\"name\":\"VentReader - Modbus Alarm\",\"capability\":\"alarm_generic\",\"optional\":true},";
  out += "{\"name\":\"VentReader - Status tekst\",\"capability\":\"measure_text\",\"optional\":true}";
  out += "],";
  out += "\"control_ready\":";
  out += (controlActive ? "true" : "false");
  out += ",";
  out += "\"homey_script_js\":\"";
  out += jsonEscape(script);
  out += "\"";
  out += "}";
  return out;
}

static String buildHomeyExportText()
{
  const String base = currentBaseUrl();
  const String token = g_cfg->api_token;
  const String statusUrl = base + "/status?token=" + token;

  String out;
  out.reserve(3600);
  out += "VentReader Homey setup\n";
  out += "======================\n\n";
  out += "Generated: " + isoFromEpochMs(nowEpochMs()) + "\n";
  out += "Model: " + g_cfg->model + "\n";
  out += "Firmware: " + String(FW_VERSION) + "\n";
  out += "Base URL: " + base + "\n";
  out += "Token: " + token + "\n\n";
  out += "Enable in VentReader admin\n";
  out += "- Homey/API: " + String(g_cfg->homey_enabled ? "ON" : "OFF") + "\n";
  out += "- Modbus: " + String(g_cfg->modbus_enabled ? "ON" : "OFF") + "\n";
  out += "- Control writes: " + String(g_cfg->control_enabled ? "ON" : "OFF") + " (optional)\n\n";
  out += "Status endpoint\n";
  out += "- " + statusUrl + "\n\n";
  out += "Recommended virtual devices\n";
  out += "- VentReader - Uteluft (measure_temperature)\n";
  out += "- VentReader - Tilluft (measure_temperature)\n";
  out += "- VentReader - Avtrekk (measure_temperature)\n";
  out += "- VentReader - Avkast (measure_temperature)\n";
  out += "- VentReader - Fan % (measure_percentage)\n";
  out += "- VentReader - Heat % (measure_percentage)\n";
  out += "- VentReader - Gjenvinning % (measure_percentage)\n\n";
  out += "HomeyScript (VentReader Poll)\n";
  out += "-----------------------------\n";
  out += "const VENTREADER_URL = '" + statusUrl + "';\n";
  out += "const MAP = {\n";
  out += "  'VentReader - Uteluft': { field: 'uteluft', cap: 'measure_temperature' },\n";
  out += "  'VentReader - Tilluft': { field: 'tilluft', cap: 'measure_temperature' },\n";
  out += "  'VentReader - Avtrekk': { field: 'avtrekk', cap: 'measure_temperature' },\n";
  out += "  'VentReader - Avkast':  { field: 'avkast',  cap: 'measure_temperature' },\n";
  out += "  'VentReader - Fan %':   { field: 'fan', cap: 'measure_percentage' },\n";
  out += "  'VentReader - Heat %':  { field: 'heat', cap: 'measure_percentage' },\n";
  out += "  'VentReader - Gjenvinning %': { field: 'efficiency', cap: 'measure_percentage' }\n";
  out += "};\n";
  out += "function num(v){ const n=Number(v); return Number.isFinite(n)?n:null; }\n";
  out += "async function setByName(devices,name,capability,value){\n";
  out += "  const d=Object.values(devices).find(x=>x.name===name);\n";
  out += "  if(!d||!d.capabilitiesObj||!d.capabilitiesObj[capability]) return;\n";
  out += "  await d.setCapabilityValue(capability,value);\n";
  out += "}\n";
  out += "const res = await fetch(VENTREADER_URL); if(!res.ok) throw new Error(`HTTP ${res.status}`);\n";
  out += "const s = await res.json(); const devices = await Homey.devices.getDevices();\n";
  out += "for (const [name,cfg] of Object.entries(MAP)) { const v=num(s[cfg.field]); if(v===null) continue; await setByName(devices,name,cfg.cap,v); }\n";
  out += "return `VentReader OK: ${s.time||'--:--'} ${s.modbus||''}`;\n";
  return out;
}

static void handleHealth()
{
  server.send(200, "text/plain", "ok");
}

static void handleAdminHomeyExportText()
{
  if (!checkAdminAuth()) return;
  if (!g_cfg->setup_completed) { redirectTo("/admin/setup?step=1"); return; }
  const String out = buildHomeyExportText();
  server.sendHeader("Cache-Control", "no-store");
  server.sendHeader("Content-Disposition", "attachment; filename=ventreader_homey_setup.txt");
  server.send(200, "text/plain; charset=utf-8", out);
}

static void handleAdminHomeyExport()
{
  if (!checkAdminAuth()) return;
  if (!g_cfg->setup_completed)
  {
    redirectTo("/admin/setup?step=1");
    return;
  }

  String out = buildHomeyExportJson();
  server.sendHeader("Cache-Control", "no-store");
  server.sendHeader("Content-Disposition", "attachment; filename=ventreader_homey_setup.json");
  server.send(200, "application/json", out);
}

static String buildAdminManualText(bool noLang)
{
  String out;
  out.reserve(4600);
  if (noLang)
  {
    out += "VentReader Manual (kort)\n";
    out += "========================\n\n";
    out += "Quick start\n";
    out += "1) Logg inn pa /admin.\n";
    out += "2) Fullfor setup wizard.\n";
    out += "3) Verifiser /status?token=... svar.\n";
    out += "4) For Homey: bruk Eksporter Homey-oppsett.\n";
    out += "5) For HA: konfigurer REST sensor mot /ha/status.\n";
    out += "6) Datakilde: velg Modbus (lokal) eller FlexitWeb Cloud (kun lesing).\n\n";
    out += "Modbus skriv (valgfritt)\n";
    out += "- Aktiver Modbus\n";
    out += "- Aktiver Enable remote control writes (experimental)\n";
    out += "- API mode: POST /api/control/mode?token=<TOKEN>&mode=AWAY|HOME|HIGH|FIRE\n";
    out += "- API setpoint: POST /api/control/setpoint?token=<TOKEN>&profile=home|away&value=18.5\n\n";
    out += "Feilsoking\n";
    out += "- 401: ugyldig eller manglende token\n";
    out += "- 403 api disabled: modul er av\n";
    out += "- 403 control disabled: control writes er av\n";
    out += "- 409 modbus disabled: Modbus er av ved skrivekall\n";
    out += "- 500 write failed: Modbus-transport eller fysisk bus-feil\n\n";
    out += "Sikkerhet\n";
    out += "- Endre fabrikkpassord\n";
    out += "- Del token kun med lokale, betrodde integrasjoner\n";
    out += "- Hold skrivestyring avskrudd nar den ikke trengs\n";
  }
  else
  {
    out += "VentReader Manual (short)\n";
    out += "========================\n\n";
    out += "Quick start\n";
    out += "1) Log in at /admin.\n";
    out += "2) Complete setup wizard.\n";
    out += "3) Verify /status?token=... returns data.\n";
    out += "4) For Homey: use Export Homey setup.\n";
    out += "5) For HA: configure REST sensor to /ha/status.\n";
    out += "6) Data source: choose Modbus (local) or FlexitWeb Cloud (read-only).\n\n";
    out += "Modbus writes (optional)\n";
    out += "- Enable Modbus\n";
    out += "- Enable remote control writes (experimental)\n";
    out += "- API mode: POST /api/control/mode?token=<TOKEN>&mode=AWAY|HOME|HIGH|FIRE\n";
    out += "- API setpoint: POST /api/control/setpoint?token=<TOKEN>&profile=home|away&value=18.5\n\n";
    out += "Troubleshooting\n";
    out += "- 401: invalid/missing token\n";
    out += "- 403 api disabled: module is disabled\n";
    out += "- 403 control disabled: control writes disabled\n";
    out += "- 409 modbus disabled: Modbus disabled for write call\n";
    out += "- 500 write failed: Modbus transport or physical bus issue\n\n";
    out += "Security\n";
    out += "- Change default admin password\n";
    out += "- Share token only with trusted local integrations\n";
    out += "- Keep writes disabled unless needed\n";
  }
  return out;
}

static void handleAdminManualText()
{
  if (!checkAdminAuth()) return;
  const bool noLang = (lang() == "no");
  String txt = buildAdminManualText(noLang);
  server.sendHeader("Cache-Control", "no-store");
  server.sendHeader("Content-Disposition", "attachment; filename=ventreader_manual.txt");
  server.send(200, "text/plain; charset=utf-8", txt);
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

static void handleStatusHistory()
{
  if (!g_cfg->homey_enabled && !g_cfg->ha_enabled)
  {
    server.send(403, "text/plain", "api disabled");
    return;
  }
  if (!tokenOK())
  {
    server.send(401, "text/plain", "missing/invalid token");
    return;
  }

  int limit = 120;
  if (server.hasArg("limit")) limit = server.arg("limit").toInt();
  if (limit < 1) limit = 1;
  if (limit > (int)HISTORY_CAP) limit = (int)HISTORY_CAP;
  if (limit > (int)g_hist_count) limit = (int)g_hist_count;

  String out;
  out.reserve((size_t)limit * 180 + 256);
  out += "{\"count\":";
  out += String(limit);
  out += ",\"items\":[";

  const size_t start = g_hist_count - (size_t)limit;
  for (size_t i = 0; i < (size_t)limit; i++)
  {
    const size_t logical = start + i;
    const size_t idx = (g_hist_head + HISTORY_CAP - g_hist_count + logical) % HISTORY_CAP;
    const StatusSnapshot& s = g_hist[idx];

    auto fOrNull = [](float v) -> String {
      if (isnan(v)) return "null";
      char t[24];
      snprintf(t, sizeof(t), "%.1f", v);
      return String(t);
    };

    if (i) out += ",";
    out += "{";
    out += "\"ts_epoch_ms\":";
    out += u64ToString(s.ts_epoch_ms);
    out += ",\"ts_iso\":\"";
    out += jsonEscape(isoFromEpochMs(s.ts_epoch_ms));
    out += "\",\"mode\":\"";
    out += jsonEscape(String(s.mode));
    out += "\",\"uteluft\":";
    out += fOrNull(s.uteluft);
    out += ",\"tilluft\":";
    out += fOrNull(s.tilluft);
    out += ",\"avtrekk\":";
    out += fOrNull(s.avtrekk);
    out += ",\"avkast\":";
    out += fOrNull(s.avkast);
    out += ",\"fan\":";
    out += String(s.fan);
    out += ",\"heat\":";
    out += String(s.heat);
    out += ",\"efficiency\":";
    out += String(s.efficiency);
    out += ",\"modbus\":\"";
    out += jsonEscape(String(s.modbus));
    out += "\",\"stale\":";
    out += (s.stale ? "true" : "false");
    out += "}";
  }

  out += "]}";
  server.send(200, "application/json", out);
}

static void handleStatusDiag()
{
  if (!g_cfg->homey_enabled && !g_cfg->ha_enabled)
  {
    server.send(403, "text/plain", "api disabled");
    return;
  }
  if (!tokenOK())
  {
    server.send(401, "text/plain", "missing/invalid token");
    return;
  }

  String out;
  out.reserve(420);
  out += "{";
  out += "\"fw\":\"" + jsonEscape(String(FW_VERSION)) + "\",";
  out += "\"total_updates\":" + String(g_diag_total_updates) + ",";
  out += "\"mb_ok\":" + String(g_diag_mb_ok) + ",";
  out += "\"mb_err\":" + String(g_diag_mb_err) + ",";
  out += "\"mb_off\":" + String(g_diag_mb_off) + ",";
  out += "\"stale\":" + String(g_diag_stale) + ",";
  out += "\"last_mb\":\"" + jsonEscape(g_diag_last_mb) + "\",";
  out += "\"last_sample_epoch_ms\":" + u64ToString(g_diag_last_sample_ms);
  out += "}";
  server.send(200, "application/json", out);
}

static void handleStatusStorage()
{
  if (!g_cfg->homey_enabled && !g_cfg->ha_enabled)
  {
    server.send(403, "text/plain", "api disabled");
    return;
  }
  if (!tokenOK())
  {
    server.send(401, "text/plain", "missing/invalid token");
    return;
  }

  String out;
  out.reserve(360);
  out += "{";
  out += "\"history_cap\":" + String((unsigned long)HISTORY_CAP) + ",";
  out += "\"history_count\":" + String((unsigned long)g_hist_count) + ",";
  out += "\"history_memory_bytes\":" + String(historyMemoryBytes()) + ",";
  out += "\"history_storage\":\"ram_only\",";
  out += "\"free_heap_bytes\":" + String(ESP.getFreeHeap()) + ",";
  out += "\"min_free_heap_bytes\":" + String(ESP.getMinFreeHeap()) + ",";
  out += "\"free_psram_bytes\":" + String(ESP.getFreePsram());
  out += "}";
  server.send(200, "application/json", out);
}

static void handleStatusHistoryCsv()
{
  if (!g_cfg->homey_enabled && !g_cfg->ha_enabled)
  {
    server.send(403, "text/plain", "api disabled");
    return;
  }
  if (!tokenOK())
  {
    server.send(401, "text/plain", "missing/invalid token");
    return;
  }

  int limit = 240;
  if (server.hasArg("limit")) limit = server.arg("limit").toInt();
  if (limit < 1) limit = 1;
  if (limit > (int)HISTORY_CAP) limit = (int)HISTORY_CAP;
  if (limit > (int)g_hist_count) limit = (int)g_hist_count;

  String out;
  out.reserve((size_t)limit * 110 + 200);
  out += "ts_epoch_ms,ts_iso,mode,uteluft,tilluft,avtrekk,avkast,fan,heat,efficiency,modbus,stale\n";

  const size_t start = g_hist_count - (size_t)limit;
  for (size_t i = 0; i < (size_t)limit; i++)
  {
    const size_t logical = start + i;
    const size_t idx = (g_hist_head + HISTORY_CAP - g_hist_count + logical) % HISTORY_CAP;
    const StatusSnapshot& s = g_hist[idx];

    auto fCsv = [](float v) -> String {
      if (isnan(v)) return "";
      char t[24];
      snprintf(t, sizeof(t), "%.1f", v);
      return String(t);
    };

    out += u64ToString(s.ts_epoch_ms) + ",";
    out += isoFromEpochMs(s.ts_epoch_ms) + ",";
    out += String(s.mode) + ",";
    out += fCsv(s.uteluft) + ",";
    out += fCsv(s.tilluft) + ",";
    out += fCsv(s.avtrekk) + ",";
    out += fCsv(s.avkast) + ",";
    out += String(s.fan) + ",";
    out += String(s.heat) + ",";
    out += String(s.efficiency) + ",";
    out += String(s.modbus) + ",";
    out += (s.stale ? "1" : "0");
    out += "\n";
  }

  server.sendHeader("Content-Disposition", "attachment; filename=ventreader_history.csv");
  server.send(200, "text/csv", out);
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

static void handleHaHistory()
{
  if (!g_cfg->ha_enabled)
  {
    server.send(403, "text/plain", "home assistant/api disabled");
    return;
  }
  handleStatusHistory();
}

static void handleHaHistoryCsv()
{
  if (!g_cfg->ha_enabled)
  {
    server.send(403, "text/plain", "home assistant/api disabled");
    return;
  }
  handleStatusHistoryCsv();
}

static void handleControlMode()
{
  if (!tokenOK()) { server.send(401, "text/plain", "missing/invalid token"); return; }
  if (normDataSource(g_cfg->data_source) != "MODBUS") { server.send(403, "text/plain", "control disabled for selected data source"); return; }
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
  if (normDataSource(g_cfg->data_source) != "MODBUS") { server.send(403, "text/plain", "control disabled for selected data source"); return; }
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

static void handleAdminControlMode()
{
  if (!checkAdminAuth()) return;
  if (!g_cfg->control_enabled || !g_cfg->modbus_enabled || normDataSource(g_cfg->data_source) != "MODBUS" || !server.hasArg("mode"))
  {
    redirectTo("/admin");
    return;
  }

  applyModbusApiRuntime();
  if (!flexit_modbus_write_mode(server.arg("mode")))
  {
    server.send(500, "text/plain", String("control mode failed: ") + flexit_modbus_last_error());
    return;
  }
  redirectTo("/admin");
}

static void handleAdminControlSetpoint()
{
  if (!checkAdminAuth()) return;
  if (!g_cfg->control_enabled || !g_cfg->modbus_enabled || normDataSource(g_cfg->data_source) != "MODBUS" ||
      !server.hasArg("profile") || !server.hasArg("value"))
  {
    redirectTo("/admin");
    return;
  }

  applyModbusApiRuntime();
  if (!flexit_modbus_write_setpoint(server.arg("profile"), server.arg("value").toFloat()))
  {
    server.send(500, "text/plain", String("control setpoint failed: ") + flexit_modbus_last_error());
    return;
  }
  redirectTo("/admin");
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
      s += ".action-grid{display:grid;grid-template-columns:1fr;gap:10px;}";
      s += "@media(min-width:620px){.action-grid{grid-template-columns:1fr 1fr;}}";
      s += ".muted-title{font-size:12px;color:var(--muted);text-transform:uppercase;letter-spacing:.10em;margin:0 0 6px 0;}";
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

static String pageFooter(){ return "</div></body></html>"; }

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
  s += "<div class='kv'><div class='k'>Firmware</div><div class='v'>" + String(FW_VERSION) + "</div></div>";
  s += "</div>";
  s += "<div class='help'>API: <code>/status?token=...</code> (Homey polling). Debug: <code>&pretty=1</code></div>";
  s += "</div>";

  auto fOrDash = [](float v) -> String {
    if (isnan(v)) return "-";
    char t[16];
    snprintf(t, sizeof(t), "%.1f", v);
    return String(t);
  };

  s += "<div class='card'><h2>Moduler (offentlig)</h2>";
  s += "<div class='kpi'>";
  s += "<div class='kv'><div class='k'>Datakilde</div><div class='v'>" + dataSourceLabel(g_cfg->data_source) + "</div></div>";
  s += "<div class='kv'><div class='k'>Homey/API</div><div class='v'>" + boolLabel(g_cfg->homey_enabled) + "</div></div>";
  s += "<div class='kv'><div class='k'>Home Assistant/API</div><div class='v'>" + boolLabel(g_cfg->ha_enabled) + "</div></div>";
  s += "<div class='kv'><div class='k'>Modbus</div><div class='v'>" + boolLabel(g_cfg->modbus_enabled) + "</div></div>";
  s += "<div class='kv'><div class='k'>FlexitWeb Cloud</div><div class='v'>" + boolLabel(normDataSource(g_cfg->data_source) == "FLEXITWEB") + "</div></div>";
  const bool ctrlActive = (g_cfg->control_enabled && normDataSource(g_cfg->data_source) == "MODBUS");
  s += "<div class='kv'><div class='k'>Control writes</div><div class='v'>" + boolLabel(ctrlActive) + "</div></div>";
  s += "</div>";
  s += "<div class='help'>Dette er lesbar oversikt uten innlogging. Konfigurasjon krever admin-login.</div>";
  s += "</div>";

  s += "<div class='card'><h2>Live data (offentlig)</h2>";
  s += "<div class='kpi'>";
  s += "<div class='kv'><div class='k'>Model</div><div class='v'>" + jsonEscape(g_data.device_model) + "</div></div>";
  s += "<div class='kv'><div class='k'>Mode</div><div class='v'>" + jsonEscape(g_data.mode) + "</div></div>";
  s += "<div class='kv'><div class='k'>Source status</div><div class='v'>" + jsonEscape(g_mb) + "</div></div>";
  s += "<div class='kv'><div class='k'>Ute / Tilluft</div><div class='v'>" + fOrDash(g_data.uteluft) + " / " + fOrDash(g_data.tilluft) + " C</div></div>";
  s += "</div>";
  s += "<div class='help'>Siste sample fra enheten. Full JSON krever token.</div>";
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
    const bool forceApiDecision = !g_cfg->setup_completed;
    const bool apiChoiceError = server.hasArg("api_choice_error");
    s += "<div class='card'><h2>Token + moduler</h2>";
    s += "<form id='setup_form' method='POST' action='/admin/setup_save?step=3'>";
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
    s += "<div class='help'>Velg eksplisitt om Homey/API og Home Assistant/API skal v&aelig;re aktivert eller deaktivert.</div>";
    if (apiChoiceError)
      s += "<div class='help' style='color:#b91c1c'>Du m&aring; velge enten Aktiver eller Deaktiver for b&aring;de Homey/API og Home Assistant/API.</div>";
    s += "<div><strong>" + tr("homey_api") + "</strong></div>";
    s += "<label><input type='radio' name='homey_mode' value='enable'"
         + String((!forceApiDecision && g_cfg->homey_enabled) ? " checked" : "")
         + String(forceApiDecision ? " required" : "")
         + "> Aktiver</label>";
    s += "<label><input type='radio' name='homey_mode' value='disable'"
         + String((!forceApiDecision && !g_cfg->homey_enabled) ? " checked" : "")
         + "> Deaktiver</label>";
    s += "<div class='sep-gold'></div>";
    s += "<div><strong>" + tr("ha_api") + "</strong></div>";
    s += "<label><input type='radio' name='ha_mode' value='enable'"
         + String((!forceApiDecision && g_cfg->ha_enabled) ? " checked" : "")
         + String(forceApiDecision ? " required" : "")
         + "> Aktiver</label>";
    s += "<label><input type='radio' name='ha_mode' value='disable'"
         + String((!forceApiDecision && !g_cfg->ha_enabled) ? " checked" : "")
         + "> Deaktiver</label>";
    s += "<div class='sep-gold'></div>";
    const bool srcWeb = (normDataSource(g_cfg->data_source) == "FLEXITWEB");
    s += "<div><strong>" + tr("data_source") + "</strong></div>";
    s += "<label><input type='radio' name='src' value='MODBUS'" + String(!srcWeb ? " checked" : "") + "> " + tr("source_modbus") + "</label>";
    s += "<label><input type='radio' name='src' value='FLEXITWEB'" + String(srcWeb ? " checked" : "") + "> " + tr("source_flexitweb") + "</label>";
    s += "<div class='help'>" + tr("source_flexitweb_help") + "</div>";
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
    s += "<div id='fw_adv_setup' style='display:" + String(srcWeb ? "block" : "none") + ";'>";
    s += "<div class='help'>" + tr("source_flexitweb_help") + "</div>";
    s += "<label>" + tr("cloud_user") + "</label><input name='fwuser' value='" + jsonEscape(g_cfg->flexitweb_user) + "'>";
    s += "<label>" + tr("cloud_pass") + "</label><input name='fwpass' type='password' value='' placeholder='" + String(g_cfg->flexitweb_pass.length() ? "********" : "") + "'>";
    s += "<label>" + tr("cloud_serial") + "</label><input name='fwserial' required pattern='[A-Za-z0-9._-]{6,32}' value='" + jsonEscape(g_cfg->flexitweb_serial) + "'>";
    s += "<div class='help'>Format: 6-32 tegn, kun A-Z, 0-9, -, _ og .</div>";
    s += "<label>" + tr("cloud_poll_min") + "</label><input name='fwpoll' type='number' min='5' max='60' value='" + String((int)g_cfg->flexitweb_poll_minutes) + "'>";
    s += "<details style='margin-top:8px'><summary>" + tr("cloud_adv") + "</summary>";
    s += "<label>" + tr("cloud_auth_url") + "</label><input class='mono' name='fwauth' value='" + jsonEscape(g_cfg->flexitweb_auth_url) + "'>";
    s += "<label>" + tr("cloud_dev_url") + "</label><input class='mono' name='fwdev' value='" + jsonEscape(g_cfg->flexitweb_device_url) + "'>";
    s += "<label>" + tr("cloud_data_url") + "</label><input class='mono' name='fwdata' value='" + jsonEscape(g_cfg->flexitweb_datapoint_url) + "'>";
    s += "</details>";
    s += "<div class='actions' style='margin-top:10px'><button class='btn secondary' type='button' onclick='testFlexitWeb(\"setup_form\")'>Test FlexitWeb</button></div>";
    s += "<div id='fw_test_result_setup' class='help'></div>";
    s += "</div>";
    s += "<script>(function(){"
         "var t=document.getElementById('mb_toggle_setup');var a=document.getElementById('mb_adv_setup');"
         "var fw=document.getElementById('fw_adv_setup');"
         "var src=document.querySelectorAll('input[name=\"src\"]');"
         "var m=document.getElementById('model_setup');var tr=document.getElementById('mbtr_setup');"
         "var sf=document.getElementById('mbser_setup');var bd=document.getElementById('mbbaud_setup');"
         "var id=document.getElementById('mbid_setup');var of=document.getElementById('mboff_setup');"
         "if(!t||!a)return;"
         "function srcVal(){for(var i=0;i<src.length;i++){if(src[i].checked)return src[i].value;}return 'MODBUS';}"
         "function u(){var useMb=(srcVal()==='MODBUS');a.style.display=(useMb&&t.checked)?'block':'none';if(fw)fw.style.display=(srcVal()==='FLEXITWEB')?'block':'none';}"
         "function p(model){tr.value='AUTO';sf.value='8N1';bd.value='19200';id.value='1';of.value='0';}"
         "t.addEventListener('change',u);"
         "for(var i=0;i<src.length;i++){src[i].addEventListener('change',u);}"
         "if(m){m.addEventListener('change',function(){if(t.checked){p(m.value);}});}"
         "u();"
         "window.testFlexitWeb=async function(formId){"
         "var form=document.getElementById(formId);if(!form)return;"
         "var target=document.getElementById('fw_test_result_setup')||document.getElementById('fw_test_result_admin');"
         "if(target){target.style.color='var(--muted)';target.textContent='Tester FlexitWeb...';}"
         "try{"
         "var fd=new FormData(form);"
         "var body=new URLSearchParams();"
         "fd.forEach(function(v,k){if(typeof v==='string')body.append(k,v);});"
         "var r=await fetch('/admin/test_flexitweb',{method:'POST',credentials:'same-origin',body:body});"
         "var j=await r.json();"
         "if(!j.ok){if(target){target.style.color='#b91c1c';target.textContent='FlexitWeb test feilet: '+(j.error||'ukjent feil');}return;}"
         "if(target){target.style.color='#166534';target.textContent='FlexitWeb OK. Modus '+(j.data&&j.data.mode?j.data.mode:'N/A')+', tilluft '+(j.data&&j.data.tilluft!==null?j.data.tilluft:'-')+' C.';}"
         "}catch(e){if(target){target.style.color='#b91c1c';target.textContent='FlexitWeb test feilet: '+e.message;}}"
         "};"
         "})();</script>";
    s += "<div class='sep-gold'></div>";
    s += "<label>" + tr("poll_sec") + "</label><input name='poll' type='number' min='30' max='3600' value='" + String(g_cfg->poll_interval_ms/1000) + "'>";
    s += "<div class='help'>Gjelder visningsoppdatering. Cloud-frekvens styres av eget minuttfelt.</div>";
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
  String homeyMode = server.arg("homey_mode");
  String haMode = server.arg("ha_mode");
  bool homeyModeValid = (homeyMode == "enable" || homeyMode == "disable");
  bool haModeValid = (haMode == "enable" || haMode == "disable");
  if (!homeyModeValid || !haModeValid)
  {
    redirectTo("/admin/setup?step=3&api_choice_error=1");
    return;
  }
  g_cfg->homey_enabled  = (homeyMode == "enable");
  g_cfg->ha_enabled     = (haMode == "enable");
  g_cfg->data_source = normDataSource(server.arg("src"));
  g_cfg->control_enabled = (g_cfg->data_source == "MODBUS") ? server.hasArg("ctrl") : false;
  if (server.hasArg("lang")) g_cfg->ui_language = normLang(server.arg("lang"));
  applyPostedModbusSettings();
  applyPostedFlexitWebSettings();
  if (g_cfg->data_source == "FLEXITWEB")
  {
    String why;
    FlexitData verified;
    if (!runFlexitWebPreflight(*g_cfg, &verified, &why))
    {
      server.send(400, "text/plain", String("FlexitWeb test must pass before save: ") + why);
      return;
    }
    g_data = verified;
    g_mb = "WEB OK (verified)";
  }

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
  g_refresh_requested = true;

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
  s += "<form id='admin_form' method='POST' action='/admin/save'>";
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
  s += "<div class='actions'>";
  s += "<a class='btn secondary' href='/admin/export/homey.txt'>Eksporter Homey-oppsett (.txt)</a>";
  s += "<button class='btn secondary' type='button' onclick='emailHomeySetup()'>Send til e-post (mobil)</button>";
  s += "</div>";
  s += "<div class='help'>Mobilknappen åpner e-postklient med oppsetttekst i ny e-post.</div>";
  s += "<div class='sep-gold'></div>";
  s += "<label><input type='checkbox' name='homey' " + String(g_cfg->homey_enabled ? "checked" : "") + "> " + tr("homey_api") + "</label>";
  s += "<label><input type='checkbox' name='ha' " + String(g_cfg->ha_enabled ? "checked" : "") + "> " + tr("ha_api") + "</label>";
  s += "<div class='sep-gold'></div>";
  const bool srcWeb = (normDataSource(g_cfg->data_source) == "FLEXITWEB");
  s += "<div><strong>" + tr("data_source") + "</strong></div>";
  s += "<label><input type='radio' name='src' value='MODBUS'" + String(!srcWeb ? " checked" : "") + "> " + tr("source_modbus") + "</label>";
  s += "<label><input type='radio' name='src' value='FLEXITWEB'" + String(srcWeb ? " checked" : "") + "> " + tr("source_flexitweb") + "</label>";
  s += "<div class='help'>" + tr("source_flexitweb_help") + "</div>";
  s += "<div class='sep-gold'></div>";
  s += "<label><input id='mb_toggle_admin' type='checkbox' name='modbus' " + String(g_cfg->modbus_enabled ? "checked" : "") + "> Modbus</label>";
  s += "<div id='mb_adv_admin' style='display:" + String((g_cfg->modbus_enabled && !srcWeb) ? "block" : "none") + ";'>";
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
  s += "<div id='fw_adv_admin' style='display:" + String(srcWeb ? "block" : "none") + ";'>";
  s += "<div class='help'>" + tr("source_flexitweb_help") + "</div>";
  s += "<label>" + tr("cloud_user") + "</label><input name='fwuser' value='" + jsonEscape(g_cfg->flexitweb_user) + "'>";
  s += "<label>" + tr("cloud_pass") + "</label><input name='fwpass' type='password' value='' placeholder='" + String(g_cfg->flexitweb_pass.length() ? "********" : "") + "'>";
  s += "<label>" + tr("cloud_serial") + "</label><input name='fwserial' required pattern='[A-Za-z0-9._-]{6,32}' value='" + jsonEscape(g_cfg->flexitweb_serial) + "'>";
  s += "<div class='help'>Format: 6-32 tegn, kun A-Z, 0-9, -, _ og .</div>";
  s += "<label>" + tr("cloud_poll_min") + "</label><input name='fwpoll' type='number' min='5' max='60' value='" + String((int)g_cfg->flexitweb_poll_minutes) + "'>";
  s += "<details style='margin-top:8px'><summary>" + tr("cloud_adv") + "</summary>";
  s += "<label>" + tr("cloud_auth_url") + "</label><input class='mono' name='fwauth' value='" + jsonEscape(g_cfg->flexitweb_auth_url) + "'>";
  s += "<label>" + tr("cloud_dev_url") + "</label><input class='mono' name='fwdev' value='" + jsonEscape(g_cfg->flexitweb_device_url) + "'>";
  s += "<label>" + tr("cloud_data_url") + "</label><input class='mono' name='fwdata' value='" + jsonEscape(g_cfg->flexitweb_datapoint_url) + "'>";
  s += "</details>";
  s += "<div class='actions' style='margin-top:10px'><button class='btn secondary' type='button' onclick='testFlexitWeb(\"admin_form\")'>Test FlexitWeb</button></div>";
  s += "<div id='fw_test_result_admin' class='help'></div>";
  s += "</div>";
  s += "<script>(function(){"
       "var t=document.getElementById('mb_toggle_admin');var a=document.getElementById('mb_adv_admin');"
       "var fw=document.getElementById('fw_adv_admin');"
       "var src=document.querySelectorAll('input[name=\"src\"]');"
       "var m=document.getElementById('model_admin');var tr=document.getElementById('mbtr_admin');"
       "var sf=document.getElementById('mbser_admin');var bd=document.getElementById('mbbaud_admin');"
       "var id=document.getElementById('mbid_admin');var of=document.getElementById('mboff_admin');"
       "if(!t||!a)return;"
       "function srcVal(){for(var i=0;i<src.length;i++){if(src[i].checked)return src[i].value;}return 'MODBUS';}"
       "function u(){var useMb=(srcVal()==='MODBUS');a.style.display=(useMb&&t.checked)?'block':'none';if(fw)fw.style.display=(srcVal()==='FLEXITWEB')?'block':'none';}"
       "function p(model){tr.value='AUTO';sf.value='8N1';bd.value='19200';id.value='1';of.value='0';}"
       "t.addEventListener('change',u);"
       "for(var i=0;i<src.length;i++){src[i].addEventListener('change',u);}"
       "if(m){m.addEventListener('change',function(){if(t.checked){p(m.value);}});}"
       "u();"
       "window.testFlexitWeb=async function(formId){"
       "var form=document.getElementById(formId);if(!form)return;"
       "var target=document.getElementById('fw_test_result_admin')||document.getElementById('fw_test_result_setup');"
       "if(target){target.style.color='var(--muted)';target.textContent='Tester FlexitWeb...';}"
       "try{"
       "var fd=new FormData(form);"
       "var body=new URLSearchParams();"
       "fd.forEach(function(v,k){if(typeof v==='string')body.append(k,v);});"
       "var r=await fetch('/admin/test_flexitweb',{method:'POST',credentials:'same-origin',body:body});"
       "var j=await r.json();"
       "if(!j.ok){if(target){target.style.color='#b91c1c';target.textContent='FlexitWeb test feilet: '+(j.error||'ukjent feil');}return;}"
       "if(target){target.style.color='#166534';target.textContent='FlexitWeb OK. Modus '+(j.data&&j.data.mode?j.data.mode:'N/A')+', tilluft '+(j.data&&j.data.tilluft!==null?j.data.tilluft:'-')+' C.';}"
       "}catch(e){if(target){target.style.color='#b91c1c';target.textContent='FlexitWeb test feilet: '+e.message;}}"
       "};"
       "})();</script>";
  s += "<div class='sep-gold'></div>";
  s += "<label>" + tr("poll_sec") + "</label><input name='poll' type='number' min='30' max='3600' value='" + String(g_cfg->poll_interval_ms/1000) + "'>";
  s += "<div class='help'>Skjermen oppdateres ved samme intervall (partial refresh).</div>";
  s += "</div>";

  // Homey export helper
  s += "<div class='card'><h2>Homey eksport</h2>";
  s += "<div class='help'>Eksporter ferdig oppsettfil med script, mapping og endpoint-data.</div>";
  s += "<div class='actions'>";
  s += "<a class='btn secondary' href='/admin/export/homey'>Eksporter Homey-oppsett (.json)</a>";
  s += "<a class='btn secondary' href='/admin/export/homey.txt'>Eksporter Homey-oppsett (.txt)</a>";
  s += "<button class='btn secondary' type='button' onclick='emailHomeySetup()'>Send til e-post (mobil)</button>";
  s += "</div>";
  s += "<div class='help'>Mobilknappen åpner e-postklient med oppsetttekst i ny e-post.</div>";
  s += "</div>";

  // Quick control panel (next-level UX)
  s += "<div class='card'><h2>" + tr("quick_control") + "</h2>";
  if (g_cfg->modbus_enabled && g_cfg->control_enabled && normDataSource(g_cfg->data_source) == "MODBUS")
  {
    s += "<div class='help'>" + tr("quick_control_help") + "</div>";
    s += "<div class='actions'>";
    s += "<form method='POST' action='/admin/control/mode' style='margin:0'><input type='hidden' name='mode' value='AWAY'><button class='btn secondary' type='submit'>" + tr("mode_away") + "</button></form>";
    s += "<form method='POST' action='/admin/control/mode' style='margin:0'><input type='hidden' name='mode' value='HOME'><button class='btn secondary' type='submit'>" + tr("mode_home") + "</button></form>";
    s += "<form method='POST' action='/admin/control/mode' style='margin:0'><input type='hidden' name='mode' value='HIGH'><button class='btn secondary' type='submit'>" + tr("mode_high") + "</button></form>";
    s += "<form method='POST' action='/admin/control/mode' style='margin:0'><input type='hidden' name='mode' value='FIRE'><button class='btn secondary' type='submit'>" + tr("mode_fire") + "</button></form>";
    s += "</div>";
    s += "<div class='sep-gold'></div>";
    s += "<form method='POST' action='/admin/control/setpoint'>";
    s += "<div class='row'>";
    s += "<div><label>" + tr("profile") + "</label><select name='profile'><option value='home'>home</option><option value='away'>away</option></select></div>";
    s += "<div><label>" + tr("setpoint") + "</label><input name='value' type='number' min='10' max='30' step='0.5' value='20.0'></div>";
    s += "</div>";
    s += "<div class='actions'><button class='btn secondary' type='submit'>" + tr("apply_setpoint") + "</button></div>";
    s += "</form>";
  }
  else
  {
    s += "<div class='help'>" + tr("enable_control_hint") + "</div>";
  }
  s += "</div>";

  // Admin password
  s += "<div class='card'><h2>Sikkerhet</h2>";
  s += "<label>Nytt admin-passord</label><input name='np1' type='password'>";
  s += "<label>Gjenta nytt passord</label><input name='np2' type='password'>";
  s += "<div class='help'>Min 8 tegn. La tomt for å ikke endre.</div>";
  s += "</div>";

  // Actions
  s += "<div class='card'><h2>Lagre / handlinger</h2>";
  s += "<p class='muted-title'>Lagre</p>";
  s += "<div class='actions' style='margin-top:8px'>";
  s += "<button class='btn' type='submit'>Lagre</button>";
  s += "</div>";
  s += "<div class='sep-gold'></div>";
  s += "<p class='muted-title'>Navigasjon</p>";
  s += "<div class='action-grid'>";
  s += "<a class='btn secondary' href='/admin/manual'>" + tr("manual") + "</a>";
  s += "<a class='btn secondary' href='/admin/graphs'>" + tr("graphs") + "</a>";
  s += "<a class='btn secondary' href='/admin/ota'>" + tr("ota") + "</a>";
  s += "<a class='btn secondary' href='/'>" + tr("back_home") + "</a>";
  s += "</div>";
  s += "<div class='help'>Tips: bruk eksportknappen over for HomeyScript og mapping i stedet for manuell kopiering.</div>";
  s += "</form>";
  s += "<div class='sep-gold'></div>";
  s += "<p class='muted-title'>System</p>";
  s += "<div class='actions'>";
  s += "<form method='POST' action='/admin/reboot' style='margin:0'><button class='btn secondary' type='submit'>Restart</button></form>";
  s += "<form method='POST' action='/admin/factory_reset' style='margin:0' onsubmit='return confirm(\"Fabrikkreset? Dette sletter alt.\");'>"
       "<button class='btn danger' type='submit'>Fabrikkreset</button></form>";
  s += "</div>";
  s += "<div class='help'>Fabrikkreset kan også trigges ved å holde BOOT (GPIO0) ~6s ved oppstart.</div>";
  s += "</div>";

  s += "<script>(function(){"
       "window.emailHomeySetup=async function(){"
       "try{"
       "const r=await fetch('/admin/export/homey.txt',{credentials:'same-origin'});"
       "if(!r.ok) throw new Error('HTTP '+r.status);"
       "const txt=await r.text();"
       "const subject='VentReader Homey setup';"
       "window.location.href='mailto:?subject='+encodeURIComponent(subject)+'&body='+encodeURIComponent(txt);"
       "}catch(e){alert('Homey eksport feilet: '+e.message);}"
       "};"
       "})();</script>";

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
  s += "<div><strong>v4.0.0</strong></div>";
  s += "<div class='help'>";
  if (noLang)
    s += "Ny datakilde: FlexitWeb Cloud (kun lesing) med egen polling (5-60 min), wizard/admin-oppsett og tydelig source-status.";
  else
    s += "New data source: FlexitWeb Cloud (read-only) with dedicated polling (5-60 min), wizard/admin setup and clear source status.";
  s += "</div>";
  s += "<div class='sep-gold'></div>";
  s += "<div><strong>v3.7.0</strong></div>";
  s += "<div class='help'>";
  if (noLang)
    s += "Setup-wizard krever n&aring; eksplisitt valg (aktiver/deaktiver) for Homey/API og Home Assistant/API.";
  else
    s += "Setup wizard now requires an explicit enable/disable choice for Homey/API and Home Assistant/API.";
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
  s += "<div><strong>3) " + String(noLang ? "Datakilde" : "Data source") + "</strong></div>";
  s += "<div class='help'>";
  if (noLang)
    s += "Velg enten <code>Modbus (lokal)</code> eller <code>FlexitWeb Cloud (kun lesing)</code>. Cloud krever Flexit-bruker og passord.";
  else
    s += "Choose either <code>Modbus (local)</code> or <code>FlexitWeb Cloud (read-only)</code>. Cloud requires Flexit username and password.";
  s += "</div>";
  s += "<div class='sep-gold'></div>";
  s += "<div><strong>4) Modbus</strong></div>";
  s += "<div class='help'>";
  if (noLang)
    s += "Modbus er AV som standard. N&aring;r Modbus aktiveres, vises avanserte innstillinger automatisk.";
  else
    s += "Modbus is OFF by default. When enabled, advanced settings appear automatically.";
  s += "</div>";
  s += "<div class='sep-gold'></div>";
  s += "<div><strong>5) Homey / Home Assistant</strong></div>";
  s += "<div class='help'>";
  if (noLang)
    s += "Aktiver Homey/API eller Home Assistant/API i admin. Bruk token-beskyttet API lokalt.";
  else
    s += "Enable Homey/API or Home Assistant/API in admin. Use token-protected local API.";
  s += "</div>";
  s += "<div class='sep-gold'></div>";
  s += "<div><strong>6) " + String(noLang ? "Fjernstyring (eksperimentell)" : "Remote control (experimental)") + "</strong></div>";
  s += "<div class='help'>";
  if (noLang)
    s += "Kun aktiv n&aring;r <code>Datakilde=Modbus</code>, <code>Modbus</code> og <code>Enable remote control writes</code> er p&aring;.";
  else
    s += "Only active when <code>Data source=Modbus</code>, <code>Modbus</code> and <code>Enable remote control writes</code> are enabled.";
  s += " API: <code>POST /api/control/mode</code>, <code>POST /api/control/setpoint</code>.";
  s += "</div>";
  s += "<div class='sep-gold'></div>";
  s += "<div><strong>7) " + String(noLang ? "OTA-oppdatering" : "OTA update") + "</strong></div>";
  s += "<div class='help'>";
  if (noLang)
    s += "G&aring; til <code>/admin/ota</code>, last opp firmwarefil (.bin), enheten restarter automatisk.";
  else
    s += "Go to <code>/admin/ota</code>, upload firmware (.bin), device restarts automatically.";
  s += "</div>";
  s += "<div class='sep-gold'></div>";
  s += "<div><strong>8) " + String(noLang ? "Feilsoking" : "Troubleshooting") + "</strong></div>";
  s += "<div class='help'>";
  if (noLang)
    s += "Sjekk <code>/health</code> og <code>/status?token=...&pretty=1</code>. Ved Modbus-feil brukes siste gyldige data.";
  else
    s += "Check <code>/health</code> and <code>/status?token=...&pretty=1</code>. On Modbus errors, last good data is used.";
  s += "</div>";
  s += "<div class='sep-gold'></div>";
  s += "<div><strong>" + String(noLang ? "Del manual" : "Share manual") + "</strong></div>";
  s += "<div class='help'>" + String(noLang ? "Du kan sende manualteksten til egen e-post eller laste den ned som tekstfil." : "You can send the manual text to your own email or download it as a text file.") + "</div>";
  s += "<div class='actions'>";
  s += "<a class='btn secondary' href='/admin/manual.txt'>" + String(noLang ? "Last ned manual (.txt)" : "Download manual (.txt)") + "</a>";
  s += "<button class='btn secondary' type='button' onclick='emailManualText()'>" + String(noLang ? "Send til e-post" : "Send via email") + "</button>";
  s += "</div>";
  s += "<div class='actions' style='margin-top:16px'><a class='btn' href='/admin'>" + tr("to_admin_page") + "</a></div>";
  s += "</div>";

  s += "<script>(function(){"
       "window.emailManualText=async function(){"
       "try{"
       "const r=await fetch('/admin/manual.txt',{credentials:'same-origin'});"
       "if(!r.ok) throw new Error('HTTP '+r.status);"
       "const txt=await r.text();"
       "const subject='VentReader manual';"
       "const body=encodeURIComponent(txt);"
       "if(body.length<14000){window.location.href='mailto:?subject='+encodeURIComponent(subject)+'&body='+body;return;}"
       "const blob=new Blob([txt],{type:'text/plain;charset=utf-8'});"
       "const url=URL.createObjectURL(blob);"
       "const a=document.createElement('a');a.href=url;a.download='ventreader_manual.txt';document.body.appendChild(a);a.click();"
       "setTimeout(function(){URL.revokeObjectURL(url);a.remove();},800);"
       "alert('" + String(noLang ? "Manualen er lang; tekstfil ble lastet ned for videresending på e-post." : "Manual is long; a text file was downloaded for email forwarding.") + "');"
       "}catch(e){alert('Manual export failed: '+e.message);}"
       "};"
       "})();</script>";

  s += "</div>";
  s += pageFooter();
  server.send(200, "text/html", s);
}

static void handleAdminGraphs()
{
  if (!checkAdminAuth()) return;
  if (!g_cfg->setup_completed)
  {
    redirectTo("/admin/setup?step=1");
    return;
  }

  String s = pageHeader(tr("graphs"), tr("history_graphs_subtitle"));
  s += "<div class='grid'>";
  s += "<div class='card'><h2>" + tr("history_graphs_title") + "</h2>";
  s += "<div class='row'>";
  s += "<div><label>" + tr("history_limit") + "</label><input id='limit' type='number' min='20' max='720' value='240'></div>";
  s += "<div style='display:flex;align-items:end'><button class='btn secondary' type='button' onclick='loadHist()'>" + tr("refresh") + "</button></div>";
  s += "<div style='display:flex;align-items:end'><button class='btn secondary' type='button' onclick='downloadCsv()'>" + tr("export_csv") + "</button></div>";
  s += "</div>";
  s += "<div class='help' id='state'>" + tr("loading") + "</div>";
  s += "<div class='sep-gold'></div>";
  s += "<div class='help'><strong>" + tr("legend") + "</strong></div>";
  s += "<div id='legend' class='actions' style='margin-top:8px'></div>";
  s += "<div class='sep-gold'></div>";
  s += "<div><div class='help'><strong>" + tr("temp_graph") + "</strong></div><canvas id='temp' width='860' height='260' style='width:100%;border:1px solid var(--border);border-radius:14px;background:linear-gradient(180deg, rgba(194,161,126,.10), rgba(194,161,126,.02));box-shadow:inset 0 0 0 1px rgba(194,161,126,.18)'></canvas></div>";
  s += "<div class='sep-gold'></div>";
  s += "<div><div class='help'><strong>" + tr("perf_graph") + "</strong></div><canvas id='perf' width='860' height='220' style='width:100%;border:1px solid var(--border);border-radius:14px;background:linear-gradient(180deg, rgba(194,161,126,.10), rgba(194,161,126,.02));box-shadow:inset 0 0 0 1px rgba(194,161,126,.18)'></canvas></div>";
  s += "</div>";

  s += "<div class='card'><h2>" + tr("storage_status") + "</h2>";
  s += "<div class='kpi'>";
  s += "<div class='kv'><div class='k'>" + tr("history_points") + "</div><div id='st_points' class='v'>-</div></div>";
  s += "<div class='kv'><div class='k'>" + tr("history_mem") + "</div><div id='st_mem' class='v'>-</div></div>";
  s += "<div class='kv'><div class='k'>" + tr("heap_free") + "</div><div id='st_heap' class='v'>-</div></div>";
  s += "<div class='kv'><div class='k'>" + tr("heap_min") + "</div><div id='st_heap_min' class='v'>-</div></div>";
  s += "</div>";
  s += "<div class='help'>" + tr("history_ram_only") + "</div>";
  s += "<div class='actions'><a class='btn' href='/admin'>" + tr("to_admin_page") + "</a></div>";
  s += "</div>";
  s += "</div>";

  const String token = jsonEscape(g_cfg->api_token);
  s += "<script>";
  s += "const API_TOKEN='" + token + "';";
  s += "const SERIES=["
       "{k:'uteluft',c:'#0ea5e9',g:'temp',n:'UTELUFT',on:true},"
       "{k:'tilluft',c:'#22c55e',g:'temp',n:'TILLUFT',on:true},"
       "{k:'avtrekk',c:'#f59e0b',g:'temp',n:'AVTREKK',on:true},"
       "{k:'avkast',c:'#6366f1',g:'temp',n:'AVKAST',on:true},"
       "{k:'fan',c:'#111827',g:'perf',n:'FAN %',on:true},"
       "{k:'heat',c:'#ef4444',g:'perf',n:'HEAT %',on:true},"
       "{k:'efficiency',c:'#16a34a',g:'perf',n:'EFF %',on:true}"
       "];";
  s += "let LAST=[];";
  s += "function drawLine(ctx,pts,color){if(pts.length<2)return;ctx.strokeStyle=color;ctx.lineWidth=2.25;ctx.beginPath();ctx.moveTo(pts[0][0],pts[0][1]);for(let i=1;i<pts.length;i++)ctx.lineTo(pts[i][0],pts[i][1]);ctx.stroke();}";
  s += "function scale(v,min,max,h,pad){if(v===null||isNaN(v))return null; if(max<=min) return h/2; return pad + (h-pad*2) * (1 - ((v-min)/(max-min)));}";
  s += "function paintGrid(ctx,w,h,p){ctx.strokeStyle='rgba(107,114,128,.35)';ctx.lineWidth=1;for(let i=0;i<5;i++){const y=p + (h-p*2)*i/4;ctx.beginPath();ctx.moveTo(p,y);ctx.lineTo(w-p,y);ctx.stroke();}}";
  s += "function drawSeries(canvasId,items,series){const c=document.getElementById(canvasId);const ctx=c.getContext('2d');const w=c.width,h=c.height,p=20;ctx.clearRect(0,0,w,h);ctx.fillStyle='rgba(255,255,255,0.86)';ctx.fillRect(0,0,w,h);paintGrid(ctx,w,h,p);const active=series.filter(s=>s.on);if(!items.length||!active.length)return;let mn=Infinity,mx=-Infinity;for(const it of items){for(const s of active){const v=it[s.k];if(v!==null&&Number.isFinite(v)){mn=Math.min(mn,v);mx=Math.max(mx,v);}}}if(!Number.isFinite(mn)||!Number.isFinite(mx)){mn=0;mx=1;}if(mx-mn<1)mx=mn+1;for(const s of active){const pts=[];for(let n=0;n<items.length;n++){const x=p + (items.length===1?0:((w-p*2)*n/(items.length-1)));const y=scale(items[n][s.k],mn,mx,h,p);if(y!==null)pts.push([x,y]);}drawLine(ctx,pts,s.c);}ctx.fillStyle='#52525b';ctx.font='12px sans-serif';ctx.fillText(mn.toFixed(1),4,h-6);ctx.fillText(mx.toFixed(1),4,14);}";
  s += "function renderLegend(){const host=document.getElementById('legend');host.innerHTML='';for(const s of SERIES){const b=document.createElement('button');b.type='button';b.className='btn secondary';b.style.borderColor=s.c;b.style.color=s.on?s.c:'#6b7280';b.style.background=s.on?'rgba(194,161,126,.12)':'transparent';b.style.padding='7px 10px';b.style.fontSize='12px';b.textContent=s.n;b.onclick=()=>{s.on=!s.on;renderLegend();renderCharts();};host.appendChild(b);}}";
  s += "function renderCharts(){drawSeries('temp',LAST,SERIES.filter(s=>s.g==='temp'));drawSeries('perf',LAST,SERIES.filter(s=>s.g==='perf'));}";
  s += "function fmtBytes(v){if(!Number.isFinite(v))return '-'; if(v>1024*1024)return (v/1048576).toFixed(2)+' MB'; if(v>1024)return (v/1024).toFixed(1)+' KB'; return v+' B';}";
  s += "async function loadStorage(){try{const r=await fetch('/status/storage?token='+encodeURIComponent(API_TOKEN));if(!r.ok)throw new Error('HTTP '+r.status);const j=await r.json();document.getElementById('st_points').textContent=(j.history_count||0)+' / '+(j.history_cap||0);document.getElementById('st_mem').textContent=fmtBytes(j.history_memory_bytes||0);document.getElementById('st_heap').textContent=fmtBytes(j.free_heap_bytes||0);document.getElementById('st_heap_min').textContent=fmtBytes(j.min_free_heap_bytes||0);}catch(e){document.getElementById('st_points').textContent='ERR';}}";
  s += "function downloadCsv(){const lim=document.getElementById('limit').value||240;window.location='/status/history.csv?token='+encodeURIComponent(API_TOKEN)+'&limit='+encodeURIComponent(lim);}";
  s += "async function loadHist(){const state=document.getElementById('state');const lim=document.getElementById('limit').value||240;state.textContent='" + tr("loading") + "';try{const r=await fetch('/status/history?token='+encodeURIComponent(API_TOKEN)+'&limit='+encodeURIComponent(lim));if(!r.ok)throw new Error('HTTP '+r.status);const j=await r.json();LAST=j.items||[];renderCharts();state.textContent='OK: '+LAST.length+' pts';}catch(e){state.textContent='ERR: '+e.message;}}";
  s += "renderLegend();";
  s += "loadStorage();";
  s += "loadHist();";
  s += "</script>";
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
  g_cfg->data_source = normDataSource(server.arg("src"));
  g_cfg->control_enabled = (g_cfg->data_source == "MODBUS") ? server.hasArg("ctrl") : false;
  if (server.hasArg("lang")) g_cfg->ui_language = normLang(server.arg("lang"));
  applyPostedModbusSettings();
  applyPostedFlexitWebSettings();
  if (g_cfg->data_source == "FLEXITWEB")
  {
    String why;
    FlexitData verified;
    if (!runFlexitWebPreflight(*g_cfg, &verified, &why))
    {
      server.send(400, "text/plain", String("FlexitWeb test must pass before save: ") + why);
      return;
    }
    g_data = verified;
    g_mb = "WEB OK (verified)";
  }

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
    g_refresh_requested = true;
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
  g_refresh_requested = true;
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
  s += "<div class='card'><h2>" + tr("restart") + "</h2><p>" + tr("restarting_now") + "</p>";
  s += "<div class='help'>Vent 10-20 sekunder, og g&aring; deretter tilbake til admin.</div>";
  s += "<div class='actions'><a class='btn' href='/admin'>Tilbake til admin</a></div>";
  s += "</div>";
  s += "<script>setTimeout(function(){window.location.href='/admin';},12000);</script>";
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

static void handleRefreshNow()
{
  if (!checkAdminAuth()) return;
  g_refresh_requested = true;
  server.send(200, "application/json", "{\"ok\":true}");
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
  historyPush(data, mbStatus);

  g_diag_total_updates++;
  g_diag_last_mb = mbStatus;
  g_diag_last_sample_ms = nowEpochMs();
  if (mbStatus.endsWith("OFF")) g_diag_mb_off++;
  else if (mbStatus.startsWith("MB OK") || mbStatus.startsWith("WEB OK")) g_diag_mb_ok++;
  else g_diag_mb_err++;
  if (mbStatus.indexOf("stale") >= 0) g_diag_stale++;
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
  server.on("/status/history", HTTP_GET, handleStatusHistory);
  server.on("/status/history.csv", HTTP_GET, handleStatusHistoryCsv);
  server.on("/status/diag", HTTP_GET, handleStatusDiag);
  server.on("/status/storage", HTTP_GET, handleStatusStorage);
  server.on("/ha/status", HTTP_GET, handleHaStatus);
  server.on("/ha/history", HTTP_GET, handleHaHistory);
  server.on("/ha/history.csv", HTTP_GET, handleHaHistoryCsv);
  server.on("/api/control/mode", HTTP_POST, handleControlMode);
  server.on("/api/control/setpoint", HTTP_POST, handleControlSetpoint);

  // Admin
  server.on("/admin", HTTP_GET, handleAdmin);
  server.on("/admin/manual", HTTP_GET, handleAdminManual);
  server.on("/admin/manual.txt", HTTP_GET, handleAdminManualText);
  server.on("/admin/graphs", HTTP_GET, handleAdminGraphs);
  server.on("/admin/export/homey", HTTP_GET, handleAdminHomeyExport);
  server.on("/admin/export/homey.txt", HTTP_GET, handleAdminHomeyExportText);
  server.on("/admin/lang", HTTP_POST, handleAdminLang);
  server.on("/admin/test_flexitweb", HTTP_POST, handleAdminFlexitWebTest);
  server.on("/admin/refresh_now", HTTP_POST, handleRefreshNow);
  server.on("/admin/save", HTTP_POST, handleAdminSave);
  server.on("/admin/control/mode", HTTP_POST, handleAdminControlMode);
  server.on("/admin/control/setpoint", HTTP_POST, handleAdminControlSetpoint);
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

bool webportal_consume_refresh_request()
{
  const bool v = g_refresh_requested;
  g_refresh_requested = false;
  return v;
}
