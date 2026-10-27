/* SPDX-License-Identifier: BSD-3-Clause
 * Same datasheet vectors executed against the actual pre-migration and current
 * INA228 sources. Deliberately excludes changed lifecycle/error contracts.
 * SLYS021A: SHUNT_CAL, DIAG_ALRT.CNVRF, signed 40-bit CHARGE. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#ifdef LEGACY
#include "nsx_ina228.h"
#else
#include <nsx/sensors/ina228.h>
#endif
static uint8_t registers[64][5];
static uint32_t read_bytes(const void *tx, size_t nt, void *rx, size_t nr)
{
    const uint8_t *p = tx;
    assert(nt == 1 && p[0] < 64 && nr <= 5);
    memcpy(rx, registers[p[0]], nr);
    return 0;
}
static uint32_t write_bytes(const void *tx, size_t n)
{
    const uint8_t *p = tx;
    assert(n == 3 && p[0] < 64);
    memcpy(registers[p[0]], p + 1, 2);
    return 0;
}
#ifdef LEGACY
uint32_t nsx_i2c_write(nsx_i2c_config_t *c, const void *b, uint32_t n, uint16_t a)
{ (void)c; (void)a; return write_bytes(b, n); }
uint32_t nsx_i2c_write_read(nsx_i2c_config_t *c, uint16_t a, const void *tx,
                          size_t nt, void *rx, size_t nr)
{ (void)c; (void)a; return read_bytes(tx, nt, rx, nr); }
#else
static nsx_sensor_status_t transfer(void *u, const nsx_sensor_transfer_t *t)
{
    (void)u;
    return (nsx_sensor_status_t)(t->rx_size ? read_bytes(t->tx, t->tx_size, t->rx, t->rx_size)
                                         : write_bytes(t->tx, t->tx_size));
}
#endif
int main(void)
{
    for (unsigned range = 0; range < 2; ++range) {
        memset(registers, 0, sizeof(registers));
        registers[0][1] = range ? 16 : 0;
        ina228_context_t c = {0};
#ifdef LEGACY
        nsx_i2c_config_t i2c = {0}; c.i2c_config = &i2c; c.addr = 0x40;
#else
        const nsx_sensor_bus_ops_t ops = {transfer};
        const nsx_sensor_bus_t bus = {&ops, NULL, NSX_SENSOR_BUS_I2C, 3, 5};
        assert(ina228_bind(&c, &bus) == 0);
#endif
        assert(ina228_set_shunt(&c, 0.1f, 0.5f) == 0);
        unsigned cal = (unsigned)registers[2][0] * 256 + registers[2][1];
        assert(cal == (range ? 5000u : 1250u));
        printf("range=%u calibration=%u\n", range, cal);
        for (unsigned status = 0; status < 4; ++status) {
            registers[11][1] = (uint8_t)status;
            uint8_t ready = 99;
            assert(ina228_conversion_ready(&c, &ready) == 0);
            assert(ready == ((status >> 1) & 1));
            printf("status=%u ready=%u\n", status, ready);
        }
        static const uint8_t codes[][5] = {
            {0,0,0,0,0}, {0,0,0,0,1}, {0xff,0xff,0xff,0xff,0xff},
            {0x80,0,0,0,0}, {0x7f,0xff,0xff,0xff,0xff}};
        static const int64_t expected[] = {0,1,-1,-549755813888LL,549755813887LL};
        for (unsigned i = 0; i < 5; ++i) {
            memcpy(registers[10], codes[i], 5);
            int64_t raw = 42;
            assert(ina228_read_charge_raw(&c, &raw) == 0 && raw == expected[i]);
            printf("charge=%lld\n", (long long)raw);
        }
    }
    return 0;
}
