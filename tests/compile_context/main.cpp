/* SPDX-License-Identifier: BSD-3-Clause */
#include <cstdio>
#include <nsx/sensors/max86150.h>
extern "C" size_t sensor_driver_config_size(void);
int main()
{
    const size_t driver = sensor_driver_config_size();
    const size_t caller = sizeof(max86150_config_t);
    if (driver != caller) {
        std::fprintf(stderr, "driver config=%zu, caller config=%zu\n", driver, caller);
        return 1;
    }
    // Check real struct-return and pointer-argument calls across the boundary.
    auto config = max86150_default_config();
    return max86150_validate_config(&config) == NSX_SENSOR_OK ? 0 : 2;
}
