/* SPDX-License-Identifier: BSD-3-Clause
 * Runnable fake-HAL example: replace endpoint_transfer with your synchronous
 * board HAL. No physical sensor is accessed by this example. */
#include <nsx/sensors/bme688.h>
#include <nsx/sensors/ledstick.h>
#include <string.h>
static nsx_sensor_status_t endpoint_transfer(void *user, const nsx_sensor_transfer_t *t)
{
    (void)user;
    if (t->rx_size) {
        if (t->tx_size != 1 || t->rx_size != 1) return NSX_SENSOR_E_UNSUPPORTED;
        t->rx[0] = t->tx[0] == 0xd0 ? 0x61 : t->tx[0] == 0xf0 ? 1 : 0;
    }
    return NSX_SENSOR_OK;
}
int main(void)
{
    static const nsx_sensor_bus_ops_t ops = { endpoint_transfer };
    nsx_sensor_bus_t bus = { &ops, NULL, NSX_SENSOR_BUS_I2C, 5, 23 };
    bme688_context_t environment = {0};
    bme688_identity_t id;
    bme688_config_t config = bme688_default_config();
    if (bme688_validate_config(&config) != NSX_SENSOR_OK ||
        bme688_bind(&environment, &bus, NULL) != NSX_SENSOR_OK ||
        bme688_probe(&environment, &id) != NSX_SENSOR_OK) return 1;
    /* Real initialization additionally needs delay_us and factory calibration
     * reads: bme688_init(&environment, &config), then start/collect or measure. */
    ledstick_context_t lights = {0};
    ledstick_config_t light_config = {10};
    if (ledstick_bind(&lights, &bus) != NSX_SENSOR_OK ||
        ledstick_init(&lights, &light_config) != NSX_SENSOR_OK ||
        ledstick_set_color(&lights, 1, 32, 0, 0) != NSX_SENSOR_OK) return 2;
    return id.chip_id != 0x61;
}
