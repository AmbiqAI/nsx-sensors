#include <nsx/sensors/bme688.h>
#include <nsx/sensors/ledstick.h>
#include <nsx/sensors/runtime.h>
int main()
{
    auto config = bme688_default_config();
    ledstick_config_t leds = {10};
    return bme688_validate_config(&config) != NSX_SENSOR_OK ||
           ledstick_validate_config(&leds) != NSX_SENSOR_OK;
}
