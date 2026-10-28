/* SPDX-License-Identifier: BSD-3-Clause */
#include <nsx/sensors/icm42670.h>

/* DS-000451 revision 1.1, bank 0. No indirect MREG accesses in this subset. */
#define REG_DEVICE_CONFIG 0x01u
#define REG_RESET 0x02u
#define REG_INT_CONFIG 0x06u
#define REG_TEMP_DATA 0x09u
#define REG_POWER 0x1fu
#define REG_GYRO_CONFIG 0x20u
#define REG_ACCEL_CONFIG 0x21u
#define REG_GYRO_FILTER 0x23u
#define REG_ACCEL_FILTER 0x24u
#define REG_INT_SOURCE0 0x2bu
#define REG_INTF_CONFIG0 0x35u
#define REG_INTF_CONFIG1 0x36u
#define REG_DRDY 0x39u
#define REG_INT_STATUS 0x3au
#define REG_ID 0x75u

static nsx_sensor_status_t read_regs(icm42670_context_t *ctx, uint8_t reg,
                                      uint8_t *data, size_t size)
{
    if (ctx->bus.kind == NSX_SENSOR_BUS_SPI) reg |= 0x80u;
    const nsx_sensor_transfer_t t = { &reg, 1, data, size };
    return nsx_sensor_bus_transfer(&ctx->bus, &t);
}
static nsx_sensor_status_t write_reg(icm42670_context_t *ctx, uint8_t reg, uint8_t value)
{
    const uint8_t data[] = {reg, value};
    const nsx_sensor_transfer_t t = {data, sizeof(data), NULL, 0};
    return nsx_sensor_bus_transfer(&ctx->bus, &t);
}
static nsx_sensor_status_t update_reg(icm42670_context_t *ctx, uint8_t reg,
                                      uint8_t mask, uint8_t value)
{
    uint8_t old;
    nsx_sensor_status_t s = read_regs(ctx, reg, &old, 1);
    if (s != NSX_SENSOR_OK) return s;
    return write_reg(ctx, reg, (uint8_t)((old & (uint8_t)~mask) | value));
}
static nsx_sensor_status_t mutation_result(icm42670_context_t *ctx, nsx_sensor_status_t s)
{
    if (s != NSX_SENSOR_OK) { ctx->_initialized = false; ctx->_running = false; }
    return s;
}
static nsx_sensor_status_t initialized(const icm42670_context_t *ctx)
{
    if (!ctx) return NSX_SENSOR_E_ARGUMENT;
    return ctx->_bound && ctx->_initialized ? NSX_SENSOR_OK : NSX_SENSOR_E_STATE;
}
icm42670_config_t icm42670_default_config(void)
{
    const icm42670_config_t c = { ICM42670_ACCEL_4G, ICM42670_GYRO_500DPS,
        ICM42670_ODR_100HZ, ICM42670_FILTER_53HZ, ICM42670_FILTER_53HZ, false };
    return c;
}
nsx_sensor_status_t icm42670_validate_config(const icm42670_config_t *c)
{
    if (!c || (unsigned)c->accel_range > 3 || (unsigned)c->gyro_range > 3 ||
        (unsigned)c->odr < 5 || (unsigned)c->odr > 12 ||
        (unsigned)c->accel_filter > 7 || (unsigned)c->gyro_filter > 7)
        return NSX_SENSOR_E_ARGUMENT;
    return NSX_SENSOR_OK;
}
nsx_sensor_status_t icm42670_bind(icm42670_context_t *ctx, const nsx_sensor_bus_t *bus,
                                const nsx_sensor_runtime_t *runtime)
{
    if (!ctx || !runtime || !runtime->delay_us) return NSX_SENSOR_E_ARGUMENT;
    nsx_sensor_status_t s = nsx_sensor_bus_validate(bus);
    if (s != NSX_SENSOR_OK) return s;
    if (bus->max_tx_size < 2 || bus->max_rx_size < 14) return NSX_SENSOR_E_UNSUPPORTED;
    const icm42670_context_t bound = { .bus = *bus, .runtime = *runtime,
        ._config = icm42670_default_config(), ._bound = true };
    *ctx = bound;
    return NSX_SENSOR_OK;
}
nsx_sensor_status_t icm42670_probe(icm42670_context_t *ctx, uint8_t *identity)
{
    if (!ctx || !identity) return NSX_SENSOR_E_ARGUMENT;
    if (!ctx->_bound) return NSX_SENSOR_E_STATE;
    uint8_t id;
    nsx_sensor_status_t s = read_regs(ctx, REG_ID, &id, 1);
    if (s != NSX_SENSOR_OK) return s;
    if (id != 0x67) return NSX_SENSOR_E_DEVICE;
    *identity = id;
    return NSX_SENSOR_OK;
}
nsx_sensor_status_t icm42670_reset(icm42670_context_t *ctx)
{
    if (!ctx) return NSX_SENSOR_E_ARGUMENT;
    if (!ctx->_bound) return NSX_SENSOR_E_STATE;
    ctx->_initialized = false;
    ctx->_running = false;
    ctx->runtime.delay_us(ctx->runtime.user, 60000);
    uint8_t id;
    nsx_sensor_status_t s = icm42670_probe(ctx, &id);
    if (s != NSX_SENSOR_OK) return s;
    s = write_reg(ctx, REG_RESET, 0x10);
    /* A failed write may have reached the device. Respect reset settling. */
    ctx->runtime.delay_us(ctx->runtime.user, 1000);
    if (s != NSX_SENSOR_OK) return s;
    uint8_t status;
    s = read_regs(ctx, REG_INT_STATUS, &status, 1);
    if (s != NSX_SENSOR_OK) return s;
    return (status & 0x10) ? NSX_SENSOR_OK : NSX_SENSOR_E_TIMEOUT;
}
static nsx_sensor_status_t apply_config(icm42670_context_t *ctx, const icm42670_config_t *c)
{
    nsx_sensor_status_t s = update_reg(ctx, REG_ACCEL_CONFIG, 0x6f,
        (uint8_t)(((unsigned)c->accel_range << 5) | (unsigned)c->odr));
    if (s == NSX_SENSOR_OK) s = update_reg(ctx, REG_GYRO_CONFIG, 0x6f,
        (uint8_t)(((unsigned)c->gyro_range << 5) | (unsigned)c->odr));
    if (s == NSX_SENSOR_OK) s = update_reg(ctx, REG_ACCEL_FILTER, 0x07, (uint8_t)c->accel_filter);
    if (s == NSX_SENSOR_OK) s = update_reg(ctx, REG_GYRO_FILTER, 0x07, (uint8_t)c->gyro_filter);
    if (s == NSX_SENSOR_OK) s = update_reg(ctx, REG_INT_CONFIG, 0x07, 0x03);
    /* Own INT1 sources: remove reset-done routing enabled by reset default. */
    if (s == NSX_SENSOR_OK) s = write_reg(ctx, REG_INT_SOURCE0, c->drdy_interrupt ? 0x08 : 0);
    if (s == NSX_SENSOR_OK) ctx->_config = *c;
    return mutation_result(ctx, s);
}
nsx_sensor_status_t icm42670_init(icm42670_context_t *ctx, const icm42670_config_t *config)
{
    if (!ctx) return NSX_SENSOR_E_ARGUMENT;
    nsx_sensor_status_t s = icm42670_validate_config(config);
    if (s != NSX_SENSOR_OK) return s;
    if (!ctx->_bound) return NSX_SENSOR_E_STATE;
    /* Copy before changing context, so passing its cached config is safe. */
    const icm42670_config_t c = *config;
    s = icm42670_reset(ctx);
    if (s != NSX_SENSOR_OK) return s;
    if (ctx->bus.kind == NSX_SENSOR_BUS_SPI) {
        s = write_reg(ctx, REG_DEVICE_CONFIG, 0x04);
        /* TDK reference shared-SPI mitigation (AN-000352), plus big endian.
         * Low bits are reserved in DS rev1.1; see PROVENANCE.md. */
        if (s == NSX_SENSOR_OK) s = update_reg(ctx, REG_INTF_CONFIG0, 0x13, 0x13);
    } else {
        s = update_reg(ctx, REG_INTF_CONFIG0, 0x10, 0x10);
    }
    if (s == NSX_SENSOR_OK) s = update_reg(ctx, REG_INTF_CONFIG1, 0x0c, 0);
    if (s == NSX_SENSOR_OK) s = apply_config(ctx, &c);
    if (s == NSX_SENSOR_OK) ctx->_initialized = true;
    return mutation_result(ctx, s);
}
nsx_sensor_status_t icm42670_configure(icm42670_context_t *ctx, const icm42670_config_t *config)
{
    nsx_sensor_status_t s = icm42670_validate_config(config);
    if (s != NSX_SENSOR_OK) return s;
    s = initialized(ctx);
    if (s != NSX_SENSOR_OK) return s;
    if (ctx->_running) return NSX_SENSOR_E_BUSY;
    const icm42670_config_t c = *config;
    return apply_config(ctx, &c);
}
#define SETTER(name, type, member) \
nsx_sensor_status_t icm42670_set_##name(icm42670_context_t *ctx, type value) \
{ \
    nsx_sensor_status_t s = initialized(ctx); \
    if (s != NSX_SENSOR_OK) return s; \
    icm42670_config_t c = ctx->_config; c.member = value; \
    return icm42670_configure(ctx, &c); \
}
SETTER(accel_range, icm42670_accel_range_t, accel_range)
SETTER(gyro_range, icm42670_gyro_range_t, gyro_range)
SETTER(odr, icm42670_odr_t, odr)
SETTER(accel_filter, icm42670_filter_t, accel_filter)
SETTER(gyro_filter, icm42670_filter_t, gyro_filter)
SETTER(drdy_interrupt, bool, drdy_interrupt)
#undef SETTER
static nsx_sensor_status_t power(icm42670_context_t *ctx, bool running)
{
    nsx_sensor_status_t s = initialized(ctx);
    if (s != NSX_SENSOR_OK) return s;
    if (ctx->_running == running) return NSX_SENSOR_OK;
    s = write_reg(ctx, REG_POWER, running ? 0x0f : 0);
    /* Required even when the transfer reports failure after taking effect. */
    ctx->runtime.delay_us(ctx->runtime.user, running ? 60000 : 21000);
    if (s == NSX_SENSOR_OK) ctx->_running = running;
    return mutation_result(ctx, s);
}
nsx_sensor_status_t icm42670_start(icm42670_context_t *ctx) { return power(ctx, true); }
nsx_sensor_status_t icm42670_stop(icm42670_context_t *ctx) { return power(ctx, false); }
static int16_t signed_be(const uint8_t *p)
{
    int32_t v = (int32_t)p[0] * 256 + p[1];
    if (v >= 32768) v -= 65536;
    return (int16_t)v;
}
nsx_sensor_status_t icm42670_read_raw(icm42670_context_t *ctx, icm42670_raw_sample_t *sample)
{
    if (!sample) return NSX_SENSOR_E_ARGUMENT;
    nsx_sensor_status_t s = initialized(ctx);
    if (s != NSX_SENSOR_OK) return s;
    if (!ctx->_running) return NSX_SENSOR_E_STATE;
    uint8_t bytes[14];
    s = read_regs(ctx, REG_TEMP_DATA, bytes, sizeof(bytes));
    if (s != NSX_SENSOR_OK) return s;
    icm42670_raw_sample_t result = { .temperature = signed_be(bytes) };
    for (unsigned i = 0; i < 3; ++i) {
        result.accel[i] = signed_be(bytes + 2 + i * 2);
        result.gyro[i] = signed_be(bytes + 8 + i * 2);
    }
    *sample = result;
    return NSX_SENSOR_OK;
}
nsx_sensor_status_t icm42670_read(icm42670_context_t *ctx, icm42670_sample_t *sample)
{
    if (!sample) return NSX_SENSOR_E_ARGUMENT;
    icm42670_raw_sample_t raw;
    nsx_sensor_status_t s = icm42670_read_raw(ctx, &raw);
    if (s != NSX_SENSOR_OK) return s;
    icm42670_sample_t result = { .temperature_c = 25.0f + (float)raw.temperature / 128.0f };
    const float accel_scale = (float)(16u >> (unsigned)ctx->_config.accel_range) / 32768.0f;
    const float gyro_scale = (float)(2000u >> (unsigned)ctx->_config.gyro_range) / 32768.0f;
    for (unsigned i = 0; i < 3; ++i) {
        result.accel_g[i] = (float)raw.accel[i] * accel_scale;
        result.gyro_dps[i] = (float)raw.gyro[i] * gyro_scale;
    }
    *sample = result;
    return NSX_SENSOR_OK;
}
nsx_sensor_status_t icm42670_read_data_ready(icm42670_context_t *ctx, bool *ready)
{
    if (!ready) return NSX_SENSOR_E_ARGUMENT;
    nsx_sensor_status_t s = initialized(ctx);
    if (s != NSX_SENSOR_OK) return s;
    if (!ctx->_running) return NSX_SENSOR_E_STATE;
    uint8_t status;
    s = read_regs(ctx, REG_DRDY, &status, 1);
    if (s == NSX_SENSOR_OK) *ready = (status & 1u) != 0;
    return s;
}
