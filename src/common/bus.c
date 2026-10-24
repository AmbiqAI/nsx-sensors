/* SPDX-License-Identifier: BSD-3-Clause */
#include <nsx/sensors/bus.h>
nsx_sensor_status_t nsx_sensor_bus_validate(const nsx_sensor_bus_t *bus)
{
    if (!bus || !bus->ops || !bus->ops->transfer ||
        (bus->kind != NSX_SENSOR_BUS_I2C && bus->kind != NSX_SENSOR_BUS_SPI) ||
        (bus->max_tx_size == 0 && bus->max_rx_size == 0))
        return NSX_SENSOR_E_ARGUMENT;
    return NSX_SENSOR_OK;
}
nsx_sensor_status_t nsx_sensor_bus_transfer(const nsx_sensor_bus_t *bus,
                                          const nsx_sensor_transfer_t *t)
{
    nsx_sensor_status_t status = nsx_sensor_bus_validate(bus);
    if (status != NSX_SENSOR_OK) return status;
    if (!t || (!t->tx_size && !t->rx_size) ||
        (t->tx_size && !t->tx) || (t->rx_size && !t->rx))
        return NSX_SENSOR_E_ARGUMENT;
    if (t->tx_size > bus->max_tx_size || t->rx_size > bus->max_rx_size)
        return NSX_SENSOR_E_UNSUPPORTED;
    return bus->ops->transfer(bus->user, t);
}
