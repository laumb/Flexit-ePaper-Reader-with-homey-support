#include "ui_display.h"

#include <GxEPD2_BW.h>
#include <Fonts/FreeMonoBold9pt7b.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>

#define PIN_CS   10
#define PIN_DC    9
#define PIN_RST   8
#define PIN_BUSY  7

static GxEPD2_BW<GxEPD2_420_GDEY042T81, GxEPD2_420_GDEY042T81::HEIGHT> display(
  GxEPD2_420_GDEY042T81(PIN_CS, PIN_DC, PIN_RST, PIN_BUSY)
);

static void draw_header(const HanSnapshot& s)
{
  display.fillRect(0, 0, display.width(), 32, GxEPD_BLACK);
  display.setTextColor(GxEPD_WHITE);
  display.setFont(&FreeMonoBold9pt7b);
  display.setCursor(8, 20);
  display.print("HAN Reader");

  char price[32];
  if (isnan(s.price_total_nok_kwh)) snprintf(price, sizeof(price), "%s --.-", s.zone.c_str());
  else snprintf(price, sizeof(price), "%s %.2f", s.zone.c_str(), s.price_total_nok_kwh);
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(price, 0, 0, &x1, &y1, &w, &h);
  display.setCursor(display.width() - static_cast<int>(w) - 8, 20);
  display.print(price);
  display.setTextColor(GxEPD_BLACK);
}

static void draw_phase_row(int y, int phase, float a, float w)
{
  display.setFont(&FreeSans9pt7b);
  display.setCursor(10, y);
  display.print("L");
  display.print(phase);

  display.setCursor(36, y);
  if (isnan(a)) display.print("--.- A");
  else
  {
    display.print(a, 1);
    display.print(" A");
  }

  display.setCursor(126, y);
  if (isnan(w)) display.print("---- W");
  else
  {
    display.print(static_cast<int>(w));
    display.print(" W");
  }
}

static void draw_energy_block(const HanSnapshot& s)
{
  const int x = 220;
  const int y = 42;
  const int w = 175;
  const int h = 122;

  display.drawRoundRect(x, y, w, h, 8, GxEPD_BLACK);
  display.setFont(&FreeSans9pt7b);
  display.setCursor(x + 10, y + 18);
  display.print("Energi");

  display.setCursor(x + 10, y + 44);
  display.print("Dag: ");
  display.print(s.day_energy_kwh, 2);
  display.print(" kWh");

  display.setCursor(x + 10, y + 68);
  display.print("Mnd: ");
  display.print(s.month_energy_kwh, 1);
  display.print(" kWh");

  display.setCursor(x + 10, y + 92);
  display.print("Ar: ");
  display.print(s.year_energy_kwh, 0);
  display.print(" kWh");

  display.setCursor(x + 10, y + 116);
  display.print("Data: ");
  display.print(s.data_time);
}

static void draw_bars(const HourBar bars[24])
{
  const int x = 10;
  const int y = 180;
  const int w = 386;
  const int h = 108;

  display.drawRect(x, y, w, h, GxEPD_BLACK);

  float maxW = 10.0f;
  for (int i = 0; i < 24; ++i)
  {
    if (bars[i].total_w > maxW) maxW = bars[i].total_w;
  }

  const int barW = 14;
  const int gap = 2;
  for (int i = 0; i < 24; ++i)
  {
    int bx = x + 3 + i * (barW + gap);
    int totalH = static_cast<int>((bars[i].total_w / maxW) * static_cast<float>(h - 16));
    if (totalH < 0) totalH = 0;
    if (totalH > h - 16) totalH = h - 16;

    float l1Part = (bars[i].total_w > 0.1f) ? (bars[i].l1_w / bars[i].total_w) : 0.0f;
    float l2Part = (bars[i].total_w > 0.1f) ? (bars[i].l2_w / bars[i].total_w) : 0.0f;
    int l1H = static_cast<int>(totalH * l1Part);
    int l2H = static_cast<int>(totalH * l2Part);
    int l3H = totalH - l1H - l2H;

    int by = y + h - 4;
    if (l1H > 0) display.fillRect(bx, by - l1H, barW, l1H, GxEPD_BLACK);
    if (l2H > 0) display.fillRect(bx + 1, by - l1H - l2H, barW - 2, l2H, GxEPD_WHITE);
    if (l3H > 0) display.fillRect(bx + 3, by - totalH, barW - 6, l3H, GxEPD_BLACK);
  }

  display.setFont(&FreeSans9pt7b);
  display.setCursor(x + 8, y + 14);
  display.print("Siste 24h effektfordeling");
}

void ui_init()
{
  display.init(115200, true, 2, false);
  display.setRotation(1);
  display.setFullWindow();
}

void ui_epaper_hard_clear()
{
  ui_init();
  display.firstPage();
  do { display.fillScreen(GxEPD_WHITE); } while (display.nextPage());
  display.hibernate();
}

void ui_render_onboarding(const String& ssid, const String& pass, const String& ip, const String& url)
{
  display.setFullWindow();
  display.firstPage();
  do
  {
    display.fillScreen(GxEPD_WHITE);
    display.setTextColor(GxEPD_BLACK);
    display.setFont(&FreeSansBold12pt7b);
    display.setCursor(20, 40);
    display.print("HAN Reader Setup");

    display.setFont(&FreeSans9pt7b);
    display.setCursor(20, 78);
    display.print("SSID: ");
    display.print(ssid);

    display.setCursor(20, 104);
    display.print("Passord: ");
    display.print(pass);

    display.setCursor(20, 130);
    display.print("IP: ");
    display.print(ip);

    display.setCursor(20, 162);
    display.print("Aapne: ");
    display.print(url);
  } while (display.nextPage());

  display.hibernate();
}

void ui_render(const HanSnapshot& s, const HourBar bars[24])
{
  display.setFullWindow();
  display.firstPage();
  do
  {
    display.fillScreen(GxEPD_WHITE);
    draw_header(s);

    draw_phase_row(64, 1, s.current_a[0], s.phase_power_w[0]);
    draw_phase_row(88, 2, s.current_a[1], s.phase_power_w[1]);
    draw_phase_row(112, 3, s.current_a[2], s.phase_power_w[2]);

    display.setFont(&FreeSans9pt7b);
    display.setCursor(10, 146);
    display.print("Import na: ");
    if (isnan(s.import_power_w)) display.print("--- W");
    else
    {
      display.print(static_cast<int>(s.import_power_w));
      display.print(" W");
    }

    display.setCursor(10, 168);
    display.print("Spot: ");
    if (isnan(s.price_spot_nok_kwh)) display.print("--.-");
    else display.print(s.price_spot_nok_kwh, 2);
    display.print("  Nett: ");
    if (isnan(s.price_grid_nok_kwh)) display.print("--.-");
    else display.print(s.price_grid_nok_kwh, 2);

    draw_energy_block(s);
    draw_bars(bars);
  } while (display.nextPage());

  display.hibernate();
}
