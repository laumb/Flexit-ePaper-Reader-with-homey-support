#include "flexit_web.h"
#include "flexit_bacnet.h"

void flexit_web_set_runtime_config(const DeviceConfig& cfg)
{
  flexit_bacnet_set_runtime_config(cfg);
}

bool flexit_web_poll(FlexitData& out)
{
  return flexit_bacnet_poll(out);
}

const char* flexit_web_last_error()
{
  return flexit_bacnet_last_error();
}

bool flexit_web_is_ready()
{
  return flexit_bacnet_is_ready();
}
