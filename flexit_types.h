#pragma once
#include <Arduino.h>

// Shared data model used by UI + Modbus + Homey
struct FlexitData {
  float uteluft = NAN;
  float tilluft = NAN;
  float avtrekk = NAN;
  float avkast  = NAN;

  int fan_percent = 0;
  int heat_element_percent = 0;
  int efficiency_percent = 0; // ETA/GJENV in %

  String mode = "N/A";
  String filter_status = "N/A";
  String wifi_status = "NO";
  String ip = "";     // short IP display, e.g. ".25"
  String time = "--:--";
};
