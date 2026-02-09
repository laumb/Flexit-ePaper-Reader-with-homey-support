#include "tariff_engine.h"
#include <math.h>

static float pick_energy_ore(const DeviceConfig& cfg, bool weekend, int hour)
{
  if (weekend) return cfg.tariff_energy_weekend_ore;
  if (hour >= cfg.tariff_day_start_hour && hour < cfg.tariff_day_end_hour) return cfg.tariff_energy_day_ore;
  return cfg.tariff_energy_night_ore;
}

float tariff_select_capacity_monthly_nok(const DeviceConfig& cfg, float top3_hourly_kw)
{
  String tiers = cfg.tariff_capacity_tiers;
  tiers.trim();
  if (tiers.length() == 0) return 0.0f;

  float selected = 0.0f;
  float smallestMatchingLimit = INFINITY;
  float fallbackLargestLimit = -1.0f;
  float fallbackLargestMonthly = 0.0f;

  int start = 0;
  while (start < tiers.length())
  {
    int comma = tiers.indexOf(',', start);
    if (comma < 0) comma = tiers.length();
    String token = tiers.substring(start, comma);
    token.trim();

    int colon = token.indexOf(':');
    if (colon > 0)
    {
      float limit = token.substring(0, colon).toFloat();
      float monthly = token.substring(colon + 1).toFloat();
      if (limit > fallbackLargestLimit)
      {
        fallbackLargestLimit = limit;
        fallbackLargestMonthly = monthly;
      }
      if (top3_hourly_kw <= limit && limit < smallestMatchingLimit)
      {
        smallestMatchingLimit = limit;
        selected = monthly;
      }
    }

    start = comma + 1;
  }

  if (isinf(smallestMatchingLimit)) return fallbackLargestMonthly;
  return selected;
}

TariffResult tariff_compute_now(const DeviceConfig& cfg,
                                float spot_nok_kwh,
                                float top3_hourly_kw,
                                bool isWeekend,
                                int hourLocal)
{
  TariffResult result;

  float energy_ore = pick_energy_ore(cfg, isWeekend, hourLocal);
  float variable_grid_nok = (energy_ore + cfg.tariff_elavgift_ore + cfg.tariff_enova_ore) / 100.0f;

  float capacity_monthly = tariff_select_capacity_monthly_nok(cfg, top3_hourly_kw);
  float fixed_monthly = cfg.tariff_fixed_monthly_nok;

  float expected_monthly_kwh = cfg.tariff_expected_monthly_kwh;
  if (expected_monthly_kwh < 1.0f) expected_monthly_kwh = 1.0f;

  float fixed_share_nok = (capacity_monthly + fixed_monthly) / expected_monthly_kwh;
  float grid_total = variable_grid_nok + fixed_share_nok;
  float total = spot_nok_kwh + grid_total;

  if (cfg.tariff_include_vat)
  {
    float vatFactor = 1.0f + (cfg.tariff_vat_percent / 100.0f);
    grid_total *= vatFactor;
    total *= vatFactor;
  }

  result.grid_nok_kwh = grid_total;
  result.total_nok_kwh = total;
  result.selected_capacity_kw = top3_hourly_kw;
  result.selected_capacity_nok_month = capacity_monthly;
  return result;
}
