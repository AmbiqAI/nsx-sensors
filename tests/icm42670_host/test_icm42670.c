/* SPDX-License-Identifier: BSD-3-Clause */
#include <nsx/sensors/icm42670.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Independent bounded model: literal bank-0 addresses/defaults and timing from
 * DS-000451 v1.1 sections 3, 12, 14 and 17. Not an electrical/FIFO simulation.
 * No production register constants or algorithms are shared with this model. */
typedef struct {
    uint8_t reg[128];
    uint64_t us, on_at, off_at, quiet_until;
    unsigned calls, writes, fail_at;
    bool spi, after_effect, running, has_stopped, suppress_reset;
} model_t;
static void defaults(model_t *m)
{
    memset(m->reg, 0, sizeof(m->reg));
    m->reg[1] = 4; m->reg[0x20] = 6; m->reg[0x21] = 6;
    m->reg[0x23] = 0x31; m->reg[0x24] = 0x41;
    m->reg[0x28] = 1; m->reg[0x2b] = 0x10;
    m->reg[0x35] = 0x30; m->reg[0x36] = 0x4d;
    m->reg[0x3a] = m->suppress_reset ? 0 : 0x10; m->reg[0x75] = 0x67;
    m->running = false;
}
static nsx_sensor_status_t transfer(void *user, const nsx_sensor_transfer_t *t)
{
    model_t *m = user;
    ++m->calls;
    assert(t->tx && t->tx_size == (t->rx_size ? 1u : 2u));
    bool fail = m->calls == m->fail_at;
    if (fail && !m->after_effect) {
        if (t->rx_size) memset(t->rx, 0xee, t->rx_size);
        return NSX_SENSOR_E_IO;
    }
    unsigned address = t->tx[0];
    if (m->spi) {
        assert(((address & 0x80) != 0) == (t->rx_size != 0));
        address &= 0x7f;
    }
    assert(address + (t->rx_size ? t->rx_size : 1) <= 128);
    if (t->rx_size) {
        memcpy(t->rx, m->reg + address, t->rx_size);
        if (address == 0x39 || address == 0x3a) m->reg[address] = 0;
    } else {
        ++m->writes;
        assert(m->us >= m->quiet_until);
        uint8_t value = t->tx[1];
        assert(m->spi || address != 1); /* Forbidden in I2C mode. */
        if (address == 2 && (value & 0x10)) {
            if (m->running) assert(m->us - m->on_at >= 45000);
            if (m->running) { m->off_at = m->us; m->has_stopped = true; }
            defaults(m);
            m->quiet_until = m->us + 1000;
        } else {
            if (address == 0x1f) {
                assert(value == 0 || value == 15);
                if (value == 15 && !m->running) {
                    if (m->has_stopped) assert(m->us - m->off_at > 20000);
                    m->on_at = m->us; m->quiet_until = m->us + 200;
                } else if (value == 0 && m->running) {
                    assert(m->us - m->on_at >= 45000);
                    m->off_at = m->us; m->has_stopped = true;
                }
                m->running = value == 15;
            }
            m->reg[address] = value;
        }
    }
    return fail ? NSX_SENSOR_E_IO : NSX_SENSOR_OK;
}
static void delay(void *user, uint32_t us) { ((model_t *)user)->us += us; }
static const nsx_sensor_bus_ops_t ops = {transfer};
static void bind_model(icm42670_context_t *ctx, model_t *m, bool spi)
{
    memset(m, 0, sizeof(*m)); m->spi = spi; defaults(m);
    nsx_sensor_bus_t bus = {&ops, m, spi ? NSX_SENSOR_BUS_SPI : NSX_SENSOR_BUS_I2C, 2, 14};
    nsx_sensor_runtime_t rt = {.user=m, .delay_us=delay};
    assert(icm42670_bind(ctx, &bus, &rt) == NSX_SENSOR_OK);
    assert(m->calls == 0 && m->us == 0);
}
static void happy_path(bool spi)
{
    model_t m; icm42670_context_t ctx;
    bind_model(&ctx, &m, spi);
    icm42670_config_t config = icm42670_default_config();
    assert(icm42670_init(&ctx, &config) == NSX_SENSOR_OK);
    assert(m.reg[0x21] == 0x49 && m.reg[0x20] == 0x49);
    assert(m.reg[0x23] == 0x34 && m.reg[0x24] == 0x44);
    assert(m.reg[0x35] == (spi ? 0x33 : 0x30));
    assert(m.reg[0x36] == 0x41 && m.reg[0x2b] == 0 && m.reg[6] == 3);
    assert(m.reg[0x28] == 1 && !m.running);
    /* Reserved bits must survive stopped reconfiguration. */
    m.reg[0x21] |= 0x90; m.reg[0x20] |= 0x90;
    assert(icm42670_set_odr(&ctx, ICM42670_ODR_12_5HZ) == NSX_SENSOR_OK);
    assert(m.reg[0x21] == 0xdc && m.reg[0x20] == 0xdc);
    assert(icm42670_set_drdy_interrupt(&ctx, true) == NSX_SENSOR_OK);
    assert(m.reg[0x2b] == 8);
    assert(icm42670_start(&ctx) == NSX_SENSOR_OK);
    unsigned calls = m.calls;
    assert(icm42670_start(&ctx) == NSX_SENSOR_OK && m.calls == calls);
    assert(icm42670_configure(&ctx, &config) == NSX_SENSOR_E_BUSY && m.calls == calls);
    /* Temperature -1 C offset, accel -FS / half-FS / +LSB, gyro same. */
    const uint8_t bytes[14] = {0xff,0x80, 0x80,0, 0x40,0, 0,1, 0x80,0, 0x40,0, 0,1};
    memcpy(m.reg + 9, bytes, sizeof(bytes));
    icm42670_raw_sample_t raw;
    assert(icm42670_read_raw(&ctx, &raw) == NSX_SENSOR_OK);
    assert(raw.temperature == -128 && raw.accel[0] == -32768 && raw.gyro[1] == 16384);
    icm42670_sample_t value;
    assert(icm42670_read(&ctx, &value) == NSX_SENSOR_OK);
    assert(value.temperature_c == 24 && value.accel_g[0] == -4 && value.accel_g[1] == 2);
    assert(value.gyro_dps[0] == -500 && value.gyro_dps[1] == 250);
    bool ready = false;
    m.reg[0x39] = 1;
    assert(icm42670_read_data_ready(&ctx, &ready) == NSX_SENSOR_OK && ready);
    assert(icm42670_read_data_ready(&ctx, &ready) == NSX_SENSOR_OK && !ready);
    icm42670_sample_t saved = value;
    m.fail_at = m.calls + 1;
    assert(icm42670_read(&ctx, &value) == NSX_SENSOR_E_IO);
    assert(memcmp(&value, &saved, sizeof(value)) == 0 && ctx._initialized);
    assert(icm42670_stop(&ctx) == NSX_SENSOR_OK && !m.running);
    calls = m.calls;
    assert(icm42670_read(&ctx, &value) == NSX_SENSOR_E_STATE && m.calls == calls);
    assert(icm42670_start(&ctx) == NSX_SENSOR_OK); /* off-to-on guard */
    assert(icm42670_reset(&ctx) == NSX_SENSOR_OK && !ctx._initialized);
    assert(icm42670_init(&ctx, &config) == NSX_SENSOR_OK);
    assert(icm42670_start(&ctx) == NSX_SENSOR_OK);
    assert(icm42670_init(&ctx, &config) == NSX_SENSOR_OK); /* reset running gyro */
    assert(icm42670_start(&ctx) == NSX_SENSOR_OK);
}
static void failures(bool spi)
{
    model_t baseline; icm42670_context_t ctx;
    icm42670_config_t c = icm42670_default_config();
    bind_model(&ctx, &baseline, spi);
    assert(icm42670_init(&ctx, &c) == NSX_SENSOR_OK);
    unsigned init_calls = baseline.calls;
    for (unsigned after = 0; after < 2; ++after) {
        for (unsigned n = 1; n <= init_calls; ++n) {
            model_t m; bind_model(&ctx, &m, spi);
            m.fail_at = n; m.after_effect = after != 0;
            assert(icm42670_init(&ctx, &c) == NSX_SENSOR_E_IO);
            assert(m.calls == n && !ctx._initialized); /* stop at first error */
            assert(icm42670_start(&ctx) == NSX_SENSOR_E_STATE);
            assert(icm42670_init(&ctx, &c) == NSX_SENSOR_OK);
        }
        /* Every configure read/write can fail, including after write effect. */
        for (unsigned n = 1; n <= 11; ++n) {
            model_t m; bind_model(&ctx, &m, spi);
            assert(icm42670_init(&ctx, &c) == NSX_SENSOR_OK);
            unsigned before = m.calls;
            m.fail_at = before + n; m.after_effect = after != 0;
            assert(icm42670_configure(&ctx, &c) == NSX_SENSOR_E_IO);
            assert(m.calls == before + n && !ctx._initialized);
            assert(icm42670_init(&ctx, &c) == NSX_SENSOR_OK);
        }
        for (unsigned operation = 0; operation < 2; ++operation) {
            model_t m; bind_model(&ctx, &m, spi);
            assert(icm42670_init(&ctx, &c) == NSX_SENSOR_OK);
            if (operation) assert(icm42670_start(&ctx) == NSX_SENSOR_OK);
            uint64_t before = m.us;
            m.fail_at = m.calls + 1; m.after_effect = after != 0;
            assert((operation ? icm42670_stop(&ctx) : icm42670_start(&ctx)) == NSX_SENSOR_E_IO);
            assert(m.us - before >= (operation ? 21000u : 60000u));
            assert(!ctx._initialized);
            assert(icm42670_init(&ctx, &c) == NSX_SENSOR_OK);
        }
    }
    model_t m; bind_model(&ctx, &m, spi);
    m.reg[0x75] = 0x69;
    uint8_t id = 0xaa;
    assert(icm42670_probe(&ctx, &id) == NSX_SENSOR_E_DEVICE && id == 0xaa);
    assert(icm42670_init(&ctx, &c) == NSX_SENSOR_E_DEVICE && !m.writes);
    m.reg[0x75] = 0x67; m.suppress_reset = true;
    assert(icm42670_init(&ctx, &c) == NSX_SENSOR_E_TIMEOUT);
}
static void configurations(void)
{
    model_t m; icm42670_context_t ctx;
    bind_model(&ctx, &m, false);
    icm42670_config_t c = icm42670_default_config();
    c.odr = (icm42670_odr_t)4;
    assert(icm42670_init(&ctx, &c) == NSX_SENSOR_E_ARGUMENT && m.calls == 0);
    c = icm42670_default_config();
    assert(icm42670_init(&ctx, &c) == NSX_SENSOR_OK);
    for (unsigned range = 0; range < 4; ++range) {
        for (unsigned odr = 5; odr <= 12; ++odr) {
            c.accel_range = (icm42670_accel_range_t)range;
            c.gyro_range = (icm42670_gyro_range_t)range;
            c.odr = (icm42670_odr_t)odr;
            c.accel_filter = (icm42670_filter_t)(odr - 5);
            c.gyro_filter = c.accel_filter;
            assert(icm42670_configure(&ctx, &c) == NSX_SENSOR_OK);
            assert((m.reg[0x21] & 0x6f) == range * 32 + odr);
            assert((m.reg[0x20] & 0x6f) == range * 32 + odr);
            assert((m.reg[0x23] & 7) == odr - 5);
        }
    }
    /* Failed bind must preserve an existing live context. */
    icm42670_context_t saved = ctx;
    nsx_sensor_bus_t small = ctx.bus; small.max_rx_size = 13;
    assert(icm42670_bind(&ctx, &small, &ctx.runtime) == NSX_SENSOR_E_UNSUPPORTED);
    assert(memcmp(&ctx, &saved, sizeof(ctx)) == 0);
    model_t second; icm42670_context_t other;
    bind_model(&other, &second, true);
    assert(icm42670_init(&other, &c) == NSX_SENSOR_OK);
    assert(icm42670_start(&ctx) == NSX_SENSOR_OK && !other._running);
    assert(icm42670_set_accel_range(&other, ICM42670_ACCEL_2G) == NSX_SENSOR_OK);
    assert(ctx._config.accel_range == ICM42670_ACCEL_2G); /* loop's last value */
    assert(icm42670_set_accel_range(&other, ICM42670_ACCEL_16G) == NSX_SENSOR_OK);
    assert(ctx._config.accel_range == ICM42670_ACCEL_2G);
}
int main(void)
{
    happy_path(false); happy_path(true); failures(false); failures(true); configurations();
    puts("ICM-42670-P datasheet contract cases passed");
    return 0;
}
