/* SPDX-License-Identifier: BSD-3-Clause */
#ifdef FOREIGN_FIRST
#include <inv_imu_driver.h>
#include <nsx/sensors/icm45605.h>
#else
#include <nsx/sensors/icm45605.h>
#include <inv_imu_driver.h>
#endif
#if UI_SPI4 != 97
#error Generic vendor macros escaped from the NSX public header
#endif
#ifdef _INV_IMU_DEFS_H_
#error Generic vendor register headers escaped from the NSX public header
#endif
int main(void)
{
    inv_imu_device_t foreign = {{42, 0, 0}};
    icm45605_context_t sensor = {0};
    icm45605_config_t config = icm45605_default_config();
    /* Pull in the real NSX wrapper and vendor archive members, without hardware. */
    return inv_imu_read_reg(&foreign) != 42 ||
           icm45605_validate_config(&config) != NSX_SENSOR_OK ||
           icm45605_probe(&sensor, 0) != NSX_SENSOR_E_ARGUMENT;
}
