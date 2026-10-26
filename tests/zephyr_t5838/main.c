/* SPDX-License-Identifier: BSD-3-Clause */
#include <nsx/sensors/t5838.h>
int main(void)
{
    t5838_config_t config = t5838_default_config();
    return t5838_validate_config(&config) == NSX_SENSOR_OK ? 0 : 1;
}
