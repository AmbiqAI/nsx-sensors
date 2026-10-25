// Common application names must not collide with device enums.
enum { CLOCK_INTERNAL, DLPF_044HZ, GYRO_FS_250DPS, ACCEL_FS_2G };
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
    optical.slots[0] = MAX86150_SLOT_PPG_LED1;
    optical.ppg_sample_rate = MAX86150_PPG_SAMPLE_RATE_400_HZ;
    optical.ecg_pga_gain = MAX86150_ECG_PGA_GAIN_2;
    auto motion = mpu6050_default_config();
    motion.clock_src = MPU6050_CLOCK_GX_PLL;
    motion.dlpf_cfg = MPU6050_DLPF_044HZ;
    motion.gyro_fullscale_range = MPU6050_GYRO_FS_250DPS;
    motion.accel_fullscale_range = MPU6050_ACCEL_FS_2G;
    ina228_context_t unbound = {};
    float scale = 42;
    if (ina228_get_current_lsb(&unbound, &scale) != NSX_SENSOR_E_STATE || scale != 42)
        return 1;
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
