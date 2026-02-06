#include "data_example.h"

void data_example_fill(FlexitData& data)
{
  data.uteluft  = -4.5;
  data.tilluft  = 22.1;
  data.avtrekk  = 25.4;
  data.avkast   = 2.1;

  data.fan_percent          = 55;
  data.heat_element_percent = 43;

  data.mode          = "VARME";
  data.filter_status = "OK";
}
