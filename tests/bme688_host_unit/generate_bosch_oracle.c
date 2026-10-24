/* SPDX-License-Identifier: BSD-3-Clause
 * OPTIONAL maintainer tool; not needed or downloaded by host tests.
 * Run against Bosch BME68x Sensor API v4.4.8 at
 * 80ea120a8b8ac987d7d79eb68a9ed796736be845:
 * cc -std=c11 -I/path/to/BME68x_SensorAPI -Itests/bme688_host_unit \
 *   tests/bme688_host_unit/generate_bosch_oracle.c \
 *   /path/to/BME68x_SensorAPI/bme68x.c -o /tmp/bme688-oracle
 * /tmp/bme688-oracle
 * The generator uses Bosch public APIs, not the NSX implementation.
 */
#include "bme68x.h"
#include "fixture.h"
#include <assert.h>
#include <stdio.h>
static int8_t read_bus(uint8_t reg, uint8_t *out, uint32_t length, void *user)
{ memcpy(out, (uint8_t *)user + reg, length); return 0; }
static int8_t write_bus(uint8_t reg, const uint8_t *in, uint32_t length, void *user)
{
    uint8_t *memory = user;
    memory[reg] = in[0];
    for (uint32_t i = 1; i < length; i += 2) memory[in[i]] = in[i + 1];
    return 0;
}
static void delay_bus(uint32_t duration, void *user) { (void)duration; (void)user; }
int main(void)
{
    uint8_t memory[256], count;
    struct bme68x_dev dev = { .intf = BME68X_I2C_INTF, .read = read_bus, .write = write_bus,
                             .delay_us = delay_bus, .intf_ptr = memory, .amb_temp = 25 };
    struct bme68x_data data;
    struct bme68x_heatr_conf heater = { .enable = BME68X_ENABLE, .heatr_temp = 320, .heatr_dur = 100 };
    const uint32_t temp[] = { 519888, 450000, 600000 };
    const uint32_t pressure[] = { 415148, 500000, 300000 };
    const uint16_t humidity[] = { 35000, 1000, 65535 };
    const uint16_t gas[] = { 512, 0, 1023 };
    const uint8_t range[] = { 5, 0, 15 };
    fixture(memory);
    assert(bme68x_init(&dev) == 0);
    assert(bme68x_set_heatr_conf(BME68X_FORCED_MODE, &heater, &dev) == 0);
    printf("heater320 ambient25: %u, duration100: 0x%02x\n", memory[0x5a], memory[0x64]);
    for (size_t i = 0; i < 3; ++i) {
        raw_field(memory, temp[i], pressure[i], humidity[i], gas[i], range[i], (uint8_t)(i + 1));
        assert(bme68x_get_data(BME68X_FORCED_MODE, &data, &count, &dev) == 0 && count == 1);
        printf("{ %.9gf, %.9gf, %.9gf, %.9gf },\n", (double)data.temperature, (double)data.pressure,
               (double)data.humidity, (double)data.gas_resistance);
    }
    for (int i = 0; i < 3; ++i) {
        heater.heatr_temp = (uint16_t)(i * 200); dev.amb_temp = (int8_t)(i == 0 ? -40 : i == 1 ? 25 : 85);
        assert(bme68x_set_heatr_conf(BME68X_FORCED_MODE, &heater, &dev) == 0);
        printf("heater%u ambient%d: %u\n", heater.heatr_temp, dev.amb_temp, memory[0x5a]);
    }
    return 0;
}
