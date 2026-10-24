#include <nsx/sensors/port_nsx.h>
nsx_sensor_status_t bind_port(nsx_sensor_nsx_context_t *ctx, nsx_i2c_config_t *bus)
{
    return nsx_sensor_nsx_bind_i2c(ctx, bus, 0x76, nullptr);
}
