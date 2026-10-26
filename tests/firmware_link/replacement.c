/* SPDX-License-Identifier: BSD-3-Clause */
#include <nsx/sensors/bme688.h>
/* Strong consumer definition suppresses extraction of the real implementation. */
nsx_sensor_status_t bme688_validate_config(const bme688_config_t *config)
{
    (void)config;
    return NSX_SENSOR_OK;
}
int main(void) { return bme688_validate_config(0); }
