/* SPDX-License-Identifier: BSD-3-Clause */
#include <nsx/sensors/tsl254x.h>
#define REG_ENABLE 0x80u
#define REG_ATIME 0x81u
#define REG_THRESHOLDS 0x84u
#define REG_PERS 0x8cu
#define REG_CFG1 0x90u
#define REG_REVID 0x91u
#define REG_STATUS 0x93u
#define REG_DATA 0x94u
#define REG_AUX 0x9eu
#define REG_CFG2 0x9fu
#define REG_CFG3 0xabu
#define REG_AZ 0xd6u
#define REG_INTENAB 0xddu
#define REG_VISADJ 0xe6u
#define STATUS_ASAT 0x80u
#define STATUS_AINT 0x10u
#define STATUS_CINT 0x08u
static nsx_sensor_status_t read_reg(tsl254x_context_t *ctx, uint8_t reg, uint8_t *data, size_t n)
{
    const nsx_sensor_transfer_t t = { &reg, 1, data, n };
    return nsx_sensor_bus_transfer(&ctx->bus, &t);
}
static nsx_sensor_status_t write_reg(tsl254x_context_t *ctx, uint8_t reg, uint8_t value)
{
    const uint8_t bytes[] = {reg, value};
    const nsx_sensor_transfer_t t = {bytes, sizeof(bytes), NULL, 0};
    nsx_sensor_status_t s = nsx_sensor_bus_transfer(&ctx->bus, &t);
    if (s != NSX_SENSOR_OK) { ctx->_configured = false; ctx->_running = false; }
    return s;
}
static nsx_sensor_status_t update(tsl254x_context_t *ctx, uint8_t reg, uint8_t mask, uint8_t value)
{
    uint8_t old;
    nsx_sensor_status_t s = read_reg(ctx, reg, &old, 1);
    if (s != NSX_SENSOR_OK) return s;
    return write_reg(ctx, reg, (uint8_t)((old & (uint8_t)~mask) | value));
}
static nsx_sensor_status_t ready(const tsl254x_context_t *ctx)
{
    if (!ctx) return NSX_SENSOR_E_ARGUMENT;
    return ctx->_bound && ctx->_configured ? NSX_SENSOR_OK : NSX_SENSOR_E_STATE;
}
nsx_sensor_status_t tsl254x_bind(tsl254x_context_t *ctx, const nsx_sensor_bus_t *bus,
                               tsl254x_variant_t variant)
{
    if (!ctx || (unsigned)variant > TSL254X_TSL2541) return NSX_SENSOR_E_ARGUMENT;
    nsx_sensor_status_t s = nsx_sensor_bus_validate(bus);
    if (s != NSX_SENSOR_OK) return s;
    if (bus->kind != NSX_SENSOR_BUS_I2C || bus->max_tx_size < 2 || bus->max_rx_size < 4)
        return NSX_SENSOR_E_UNSUPPORTED;
    *ctx = (tsl254x_context_t){.bus = *bus, ._variant = variant, ._bound = true};
    return NSX_SENSOR_OK;
}
tsl254x_config_t tsl254x_default_config(void)
{
    return (tsl254x_config_t){.gain = TSL254X_GAIN_1, .integration_cycles = 36,
        .high_threshold = UINT16_MAX, .persistence = 1};
}
nsx_sensor_status_t tsl254x_validate_config(const tsl254x_config_t *c)
{
    if (!c || (unsigned)c->gain > TSL254X_GAIN_128 || !c->integration_cycles ||
        c->integration_cycles > 256 || c->low_threshold > c->high_threshold ||
        c->persistence > 15) return NSX_SENSOR_E_ARGUMENT;
    return NSX_SENSOR_OK;
}
nsx_sensor_status_t tsl254x_probe(tsl254x_context_t *ctx, tsl254x_identity_t *identity)
{
    uint8_t bytes[2], aux;
    if (!ctx || !identity) return NSX_SENSOR_E_ARGUMENT;
    if (!ctx->_bound) return NSX_SENSOR_E_STATE;
    nsx_sensor_status_t s = read_reg(ctx, REG_REVID, bytes, 2);
    if (s != NSX_SENSOR_OK) return s;
    s = read_reg(ctx, REG_AUX, &aux, 1);
    if (s != NSX_SENSOR_OK) return s;
    aux &= 0x0fu;
    if ((bytes[1] & 0xfcu) != 0xe4u ||
        (ctx->_variant == TSL254X_TSL2540 ? aux > 1 : aux != 2)) return NSX_SENSOR_E_DEVICE;
    *identity = (tsl254x_identity_t){bytes[1], bytes[0], aux};
    return NSX_SENSOR_OK;
}
nsx_sensor_status_t tsl254x_init(tsl254x_context_t *ctx, const tsl254x_config_t *config)
{
    static const uint8_t cfg1[] = {0, 0, 1, 2, 3, 3};
    static const uint8_t cfg2[] = {0, 4, 4, 4, 4, 0x14};
    tsl254x_identity_t identity;
    uint8_t adjustment = 0;
    if (!ctx) return NSX_SENSOR_E_ARGUMENT;
    nsx_sensor_status_t s = tsl254x_validate_config(config);
    if (s != NSX_SENSOR_OK) return s;
    if (!ctx->_bound) return NSX_SENSOR_E_STATE;
    /* Copy first: callers may pass the context's cached configuration. */
    const tsl254x_config_t c = *config;
    ctx->_configured = false; ctx->_running = false;
    s = tsl254x_probe(ctx, &identity);
    if (s != NSX_SENSOR_OK) return s;
#define TRY(expr) do { s = (expr); if (s != NSX_SENSOR_OK) return s; } while (0)
    TRY(write_reg(ctx, REG_ENABLE, 0));
    TRY(write_reg(ctx, REG_INTENAB, 0));
    TRY(write_reg(ctx, REG_ATIME, (uint8_t)(c.integration_cycles - 1u)));
    TRY(update(ctx, REG_CFG1, 3, cfg1[c.gain]));
    TRY(update(ctx, REG_CFG2, 0x14, cfg2[c.gain]));
    /* Preserve reserved bits; disable read-clear and sleep-after-interrupt. */
    TRY(update(ctx, REG_CFG3, 0x90, 0));
    TRY(update(ctx, REG_AZ, 0x7f, 0x7f));
    TRY(update(ctx, REG_PERS, 0x0f, c.persistence));
    TRY(write_reg(ctx, REG_THRESHOLDS, (uint8_t)c.low_threshold));
    TRY(write_reg(ctx, REG_THRESHOLDS + 1, (uint8_t)(c.low_threshold >> 8)));
    TRY(write_reg(ctx, REG_THRESHOLDS + 2, (uint8_t)c.high_threshold));
    TRY(write_reg(ctx, REG_THRESHOLDS + 3, (uint8_t)(c.high_threshold >> 8)));
    if (ctx->_variant == TSL254X_TSL2541) TRY(read_reg(ctx, REG_VISADJ, &adjustment, 1));
    /* A negative or zero scale is not a usable factory calibration. */
    if ((adjustment & 0x80u) && (adjustment & 0x7fu) >= 100u)
        return NSX_SENSOR_E_CALIBRATION;
    TRY(write_reg(ctx, REG_STATUS, STATUS_ASAT | STATUS_AINT | STATUS_CINT));
    TRY(write_reg(ctx, REG_INTENAB, (uint8_t)((c.threshold_interrupt ? 0x10u : 0) |
                                             (c.saturation_interrupt ? 0x80u : 0))));
#undef TRY
    ctx->_visible_scale = 1.0f + ((adjustment & 0x80u) ? -1.0f : 1.0f) *
                                 (float)(adjustment & 0x7fu) / 100.0f;
    ctx->_config = c; ctx->_configured = true;
    return NSX_SENSOR_OK;
}
nsx_sensor_status_t tsl254x_start(tsl254x_context_t *ctx)
{
    nsx_sensor_status_t s = ready(ctx);
    if (s != NSX_SENSOR_OK) return s;
    if (ctx->_running) return NSX_SENSOR_E_BUSY;
    s = write_reg(ctx, REG_STATUS, STATUS_ASAT | STATUS_AINT | STATUS_CINT);
    if (s != NSX_SENSOR_OK) return s;
    /* Simultaneously set PON/AEN so autozero runs before first conversion. */
    s = write_reg(ctx, REG_ENABLE, 3);
    if (s == NSX_SENSOR_OK) ctx->_running = true;
    return s;
}
nsx_sensor_status_t tsl254x_stop(tsl254x_context_t *ctx)
{
    nsx_sensor_status_t s = ready(ctx);
    if (s != NSX_SENSOR_OK) return s;
    s = write_reg(ctx, REG_ENABLE, 0);
    if (s == NSX_SENSOR_OK) ctx->_running = false;
    return s;
}
nsx_sensor_status_t tsl254x_read_sample(tsl254x_context_t *ctx, tsl254x_sample_t *sample)
{
    uint8_t flags, data[4];
    if (!sample) return NSX_SENSOR_E_ARGUMENT;
    nsx_sensor_status_t s = ready(ctx);
    if (s != NSX_SENSOR_OK) return s;
    if (!ctx->_running) return NSX_SENSOR_E_STATE;
    s = read_reg(ctx, REG_STATUS, &flags, 1);
    if (s != NSX_SENSOR_OK) return s;
    s = read_reg(ctx, REG_DATA, data, sizeof(data));
    if (s != NSX_SENSOR_OK) return s;
    uint16_t visible = (uint16_t)((uint16_t)data[1] << 8 | data[0]);
    uint16_t infrared = (uint16_t)((uint16_t)data[3] << 8 | data[2]);
    uint32_t limit = (uint32_t)ctx->_config.integration_cycles * 1024u - 1u;
    if (limit > UINT16_MAX) limit = UINT16_MAX;
    *sample = (tsl254x_sample_t){visible, infrared, (flags & STATUS_ASAT) != 0,
        visible >= limit || infrared >= limit, (flags & STATUS_AINT) != 0,
        (flags & STATUS_CINT) != 0};
    return NSX_SENSOR_OK;
}
nsx_sensor_status_t tsl254x_clear_status(tsl254x_context_t *ctx, bool saturation,
                                       bool threshold, bool calibration)
{
    nsx_sensor_status_t s = ready(ctx);
    if (s != NSX_SENSOR_OK) return s;
    uint8_t value = (uint8_t)((saturation ? STATUS_ASAT : 0) |
        (threshold ? STATUS_AINT : 0) | (calibration ? STATUS_CINT : 0));
    return value ? write_reg(ctx, REG_STATUS, value) : NSX_SENSOR_OK;
}
nsx_sensor_status_t tsl254x_get_visible_scale(const tsl254x_context_t *ctx, float *scale)
{
    if (!scale) return NSX_SENSOR_E_ARGUMENT;
    nsx_sensor_status_t s = ready(ctx);
    if (s == NSX_SENSOR_OK) *scale = ctx->_visible_scale;
    return s;
}
