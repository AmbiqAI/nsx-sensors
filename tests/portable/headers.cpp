#include <nsx/sensors/icm45605.h>
#include <nsx/sensors/bme688.h>
#include <nsx/sensors/ina228.h>
#include <nsx/sensors/mpu6050.h>
#include <nsx/sensors/max86150.h>
#include <nsx/sensors/ledstick.h>
#include <nsx/sensors/runtime.h>
int main()
{
    auto icm = icm45605_default_config();
    auto config = bme688_default_config();
    auto optical = max86150_default_config();
    auto motion = mpu6050_default_config();
    auto monitor = ina228_default_config();
    monitor.shunt_ohms = 0.1f; monitor.max_current_amps = 0.5f;
    ledstick_config_t leds = {10};
    return icm45605_validate_config(&icm) != NSX_SENSOR_OK ||
           max86150_validate_config(&optical) != NSX_SENSOR_OK ||
           mpu6050_validate_config(&motion) != NSX_SENSOR_OK ||
           ina228_validate_config(&monitor) != NSX_SENSOR_OK ||
           bme688_validate_config(&config) != NSX_SENSOR_OK ||
           ledstick_validate_config(&leds) != NSX_SENSOR_OK;
}
