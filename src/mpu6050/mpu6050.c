/* SPDX-License-Identifier: BSD-3-Clause
 * Original MPU6050 driver: Adam Page, Copyright (c) 2022. */
#include <nsx/sensors/mpu6050.h>
#include <limits.h>
#include <string.h>

/* MPU-6000/MPU-6050 register map. Private implementation details. */
enum {
    XA_OFFSET = 0x06, XG_OFFSET = 0x13, DIVIDER = 0x19, CONFIG = 0x1a,
    GYRO_CONFIG = 0x1b, ACCEL_CONFIG = 0x1c, FIFO_ENABLE = 0x23,
    INT_CONFIG = 0x37, INT_ENABLE = 0x38, INT_STATUS = 0x3a,
    SAMPLE = 0x3b, SIGNAL_RESET = 0x68, USER_CTRL = 0x6a,
    POWER1 = 0x6b, POWER2 = 0x6c, FIFO_COUNT = 0x72, FIFO_DATA = 0x74,
    WHO_AM_I = 0x75, FIFO_CAPACITY = 1024
};
typedef struct { uint32_t remaining; uint64_t start; uint32_t timeout; } budget_t;

static nsx_sensor_status_t read_reg(mpu6050_context_t *ctx, uint8_t reg, uint8_t *data, size_t n)
{
    const nsx_sensor_transfer_t t = {&reg, 1, data, n};
    return nsx_sensor_bus_transfer(&ctx->bus, &t);
}
static nsx_sensor_status_t write_bytes(mpu6050_context_t *ctx, uint8_t reg, const uint8_t *data, size_t n)
{
    uint8_t bytes[3] = {reg, 0, 0};
    memcpy(bytes + 1, data, n);
    const nsx_sensor_transfer_t t = {bytes, n + 1, NULL, 0};
    nsx_sensor_status_t s = nsx_sensor_bus_transfer(&ctx->bus, &t);
    if (s != NSX_SENSOR_OK) {
        ctx->_configured = false;
        ctx->_fifo_valid = false;
    }
    return s;
}
static nsx_sensor_status_t write_reg(mpu6050_context_t *ctx, uint8_t reg, uint8_t value)
{
    return write_bytes(ctx, reg, &value, 1);
}
static nsx_sensor_status_t ready(const mpu6050_context_t *ctx)
{
    if (!ctx) return NSX_SENSOR_E_ARGUMENT;
    return ctx->_bound && ctx->_configured ? NSX_SENSOR_OK : NSX_SENSOR_E_STATE;
}
static nsx_sensor_status_t active(const mpu6050_context_t *ctx)
{
    nsx_sensor_status_t s = ready(ctx);
    if (s != NSX_SENSOR_OK) return s;
    return ctx->_sleeping || ctx->_config.clock_src == MPU6050_CLOCK_KEEP_RESET ? NSX_SENSOR_E_STATE : NSX_SENSOR_OK;
}
static bool clock_valid(mpu6050_clock_src_t clock)
{
    return (unsigned)clock <= 5 || clock == MPU6050_CLOCK_KEEP_RESET;
}
static int16_t signed_word(const uint8_t *p)
{
    uint16_t u = (uint16_t)((uint16_t)p[0] << 8 | p[1]);
    return (int16_t)((int32_t)(u & 0x7fff) - (int32_t)(u & 0x8000));
}
static budget_t budget_start(mpu6050_context_t *ctx, uint32_t timeout)
{
    budget_t b = {timeout, ctx->runtime.now_us ? ctx->runtime.now_us(ctx->runtime.user) : 0, timeout};
    return b;
}
static uint32_t budget_left(mpu6050_context_t *ctx, budget_t *b)
{
    if (ctx->runtime.now_us) {
        uint64_t elapsed = ctx->runtime.now_us(ctx->runtime.user) - b->start;
        if (elapsed >= b->timeout) return 0;
        uint32_t left = b->timeout - (uint32_t)elapsed;
        if (left < b->remaining) return left;
    }
    return b->remaining;
}
static nsx_sensor_status_t pause_for(mpu6050_context_t *ctx, budget_t *b, uint32_t us)
{
    if (budget_left(ctx, b) < us) return NSX_SENSOR_E_TIMEOUT;
    ctx->runtime.delay_us(ctx->runtime.user, us);
    b->remaining -= us;
    return NSX_SENSOR_OK;
}
static nsx_sensor_status_t poll_clear(mpu6050_context_t *ctx, uint8_t reg, uint8_t mask, budget_t *b)
{
    for (;;) {
        uint8_t value;
        nsx_sensor_status_t s = read_reg(ctx, reg, &value, 1);
        if (s != NSX_SENSOR_OK) return s;
        if (ctx->runtime.now_us && ctx->runtime.now_us(ctx->runtime.user) - b->start > b->timeout)
            return NSX_SENSOR_E_TIMEOUT;
        if (!(value & mask)) return NSX_SENSOR_OK;
        s = pause_for(ctx, b, 1000);
        if (s != NSX_SENSOR_OK) return s;
    }
}
static nsx_sensor_status_t update(mpu6050_context_t *ctx, uint8_t reg, uint8_t mask, uint8_t value)
{
    nsx_sensor_status_t s = ready(ctx);
    if (s != NSX_SENSOR_OK) return s;
    uint8_t old;
    s = read_reg(ctx, reg, &old, 1);
    if (s != NSX_SENSOR_OK) return s;
    /* Never replay a self-clearing command read from hardware. */
    if (reg == POWER1) old &= 0x7f;
    if (reg == USER_CTRL) old &= 0xf0;
    return write_reg(ctx, reg, (uint8_t)((old & (uint8_t)~mask) | value));
}
static void invalidate_fifo(mpu6050_context_t *ctx)
{
    ctx->_fifo_valid = false;
    ctx->_fifo_aligned = false;
}
mpu6050_config_t mpu6050_default_config(void)
{
    const mpu6050_config_t c = {MPU6050_CLOCK_GX_PLL, MPU6050_DLPF_044HZ, MPU6050_GYRO_FS_250DPS, MPU6050_ACCEL_FS_2G, 9};
    return c;
}
nsx_sensor_status_t mpu6050_validate_config(const mpu6050_config_t *c)
{
    if (!c || !clock_valid(c->clock_src) || (unsigned)c->dlpf_cfg > 6 ||
        (unsigned)c->gyro_fullscale_range > 3 || (unsigned)c->accel_fullscale_range > 3)
        return NSX_SENSOR_E_ARGUMENT;
    return NSX_SENSOR_OK;
}
nsx_sensor_status_t mpu6050_bind(mpu6050_context_t *ctx, const nsx_sensor_bus_t *bus,
                                const nsx_sensor_runtime_t *runtime)
{
    if (!ctx) return NSX_SENSOR_E_ARGUMENT;
    nsx_sensor_status_t s = nsx_sensor_bus_validate(bus);
    if (s != NSX_SENSOR_OK) return s;
    if (bus->kind != NSX_SENSOR_BUS_I2C || bus->max_tx_size < 3 || bus->max_rx_size < 14)
        return NSX_SENSOR_E_UNSUPPORTED;
    mpu6050_context_t next = {0};
    next.bus = *bus;
    if (runtime) next.runtime = *runtime;
    next._bound = true;
    *ctx = next;
    return NSX_SENSOR_OK;
}
nsx_sensor_status_t mpu6050_probe(mpu6050_context_t *ctx, uint8_t *identity)
{
    if (!ctx || !identity) return NSX_SENSOR_E_ARGUMENT;
    if (!ctx->_bound) return NSX_SENSOR_E_STATE;
    uint8_t id;
    nsx_sensor_status_t s = read_reg(ctx, WHO_AM_I, &id, 1);
    if (s != NSX_SENSOR_OK) return s;
    if ((id & 0x7e) != 0x68) return NSX_SENSOR_E_DEVICE;
    *identity = id;
    return NSX_SENSOR_OK;
}
nsx_sensor_status_t mpu6050_device_reset(mpu6050_context_t *ctx, uint32_t timeout_us)
{
    if (!ctx || timeout_us < 100000) return NSX_SENSOR_E_ARGUMENT;
    if (!ctx->_bound) return NSX_SENSOR_E_STATE;
    if (!ctx->runtime.delay_us) return NSX_SENSOR_E_UNSUPPORTED;
    ctx->_configured = false;
    invalidate_fifo(ctx);
    budget_t b = budget_start(ctx, timeout_us);
    nsx_sensor_status_t s = write_reg(ctx, POWER1, 0x80);
    if (s != NSX_SENSOR_OK) return s;
    s = pause_for(ctx, &b, 100000);
    if (s != NSX_SENSOR_OK) return s;
    return poll_clear(ctx, POWER1, 0x80, &b);
}
nsx_sensor_status_t mpu6050_init(mpu6050_context_t *ctx, const mpu6050_config_t *config)
{
    nsx_sensor_status_t s = mpu6050_validate_config(config);
    if (s != NSX_SENSOR_OK || !ctx) return NSX_SENSOR_E_ARGUMENT;
    /* Copy first: callers may pass the context's last applied configuration. */
    mpu6050_config_t c = *config;
    if (!ctx->_bound) return NSX_SENSOR_E_STATE;
    if (!ctx->runtime.delay_us) return NSX_SENSOR_E_UNSUPPORTED;
    uint8_t id;
    s = mpu6050_probe(ctx, &id);
    if (s != NSX_SENSOR_OK) return s;
    s = mpu6050_device_reset(ctx, 200000);
    if (s != NSX_SENSOR_OK) return s;
    const uint8_t regs[][2] = {
        {POWER1, (uint8_t)(0x40 | c.clock_src)}, {POWER2, 0},
        {CONFIG, (uint8_t)c.dlpf_cfg}, {GYRO_CONFIG, (uint8_t)(c.gyro_fullscale_range << 3)},
        {ACCEL_CONFIG, (uint8_t)(c.accel_fullscale_range << 3)}, {DIVIDER, c.sample_rate_divider},
        {FIFO_ENABLE, 0}, {USER_CTRL, 0}, {INT_ENABLE, 0}, {INT_CONFIG, 0}
    };
    for (size_t i = 0; i < sizeof(regs)/sizeof(regs[0]); ++i) {
        s = write_reg(ctx, regs[i][0], regs[i][1]);
        if (s != NSX_SENSOR_OK) return s;
    }
    ctx->_config = c;
    ctx->_sleeping = true;
    ctx->_lowpower = ctx->_temp_disabled = ctx->_fifo_enabled = ctx->_any_read_clear = false;
    ctx->_fifo_mask = 0;
    ctx->_fifo_valid = ctx->_fifo_aligned = ctx->_configured = true;
    return NSX_SENSOR_OK;
}
nsx_sensor_status_t mpu6050_set_sleep(mpu6050_context_t *ctx, uint8_t sleep)
{
    if (sleep > 1) return NSX_SENSOR_E_ARGUMENT;
    nsx_sensor_status_t s = update(ctx, POWER1, 0x40, (uint8_t)(sleep << 6));
    if (s == NSX_SENSOR_OK) { ctx->_sleeping = sleep != 0; invalidate_fifo(ctx); }
    return s;
}
nsx_sensor_status_t mpu6050_set_clock_source(mpu6050_context_t *ctx, mpu6050_clock_src_t clock)
{
    if (!clock_valid(clock)) return NSX_SENSOR_E_ARGUMENT;
    nsx_sensor_status_t s = ready(ctx);
    if (s != NSX_SENSOR_OK) return s;
    if (ctx->_lowpower && clock != MPU6050_CLOCK_INTERNAL) return NSX_SENSOR_E_STATE;
    s = update(ctx, POWER1, 7, (uint8_t)clock);
    if (s == NSX_SENSOR_OK) { ctx->_config.clock_src = clock; invalidate_fifo(ctx); }
    return s;
}
nsx_sensor_status_t mpu6050_set_temperature_disable(mpu6050_context_t *ctx, uint8_t disable)
{
    if (disable > 1) return NSX_SENSOR_E_ARGUMENT;
    nsx_sensor_status_t s = ready(ctx);
    if (s != NSX_SENSOR_OK) return s;
    if (ctx->_lowpower && !disable) return NSX_SENSOR_E_STATE;
    s = update(ctx, POWER1, 8, (uint8_t)(disable << 3));
    if (s == NSX_SENSOR_OK) { ctx->_temp_disabled = disable != 0; invalidate_fifo(ctx); }
    return s;
}
nsx_sensor_status_t mpu6050_set_lowpower_accel_mode(mpu6050_context_t *ctx, uint8_t frequency)
{
    if (frequency > 3) return NSX_SENSOR_E_ARGUMENT;
    nsx_sensor_status_t s = ready(ctx);
    if (s != NSX_SENSOR_OK) return s;
    if (ctx->_fifo_enabled) return NSX_SENSOR_E_STATE;
    s = mpu6050_set_sleep(ctx, 1);
    if (s != NSX_SENSOR_OK) return s;
    s = write_reg(ctx, POWER2, (uint8_t)((frequency << 6) | 7));
    if (s != NSX_SENSOR_OK) return s;
    /* Cycling requires the internal clock; PLL gyro references are powered off. */
    s = update(ctx, POWER1, 0x7f, 0x28);
    if (s != NSX_SENSOR_OK) ctx->_configured = false;
    if (s == NSX_SENSOR_OK) {
        ctx->_lowpower = ctx->_temp_disabled = true;
        ctx->_sleeping = false;
        ctx->_config.clock_src = MPU6050_CLOCK_INTERNAL;
    }
    return s;
}
nsx_sensor_status_t mpu6050_set_lowpass_filter(mpu6050_context_t *ctx, mpu6050_dlpf_cfg_t filter)
{
    if ((unsigned)filter > 6) return NSX_SENSOR_E_ARGUMENT;
    nsx_sensor_status_t s = update(ctx, CONFIG, 7, (uint8_t)filter);
    if (s == NSX_SENSOR_OK) { ctx->_config.dlpf_cfg = filter; invalidate_fifo(ctx); }
    return s;
}
nsx_sensor_status_t mpu6050_set_gyro_full_scale(mpu6050_context_t *ctx, mpu6050_gyro_fs_t range)
{
    if ((unsigned)range > 3) return NSX_SENSOR_E_ARGUMENT;
    nsx_sensor_status_t s = update(ctx, GYRO_CONFIG, 0x18, (uint8_t)(range << 3));
    if (s == NSX_SENSOR_OK) { ctx->_config.gyro_fullscale_range = range; invalidate_fifo(ctx); }
    return s;
}
nsx_sensor_status_t mpu6050_set_accel_full_scale(mpu6050_context_t *ctx, mpu6050_accel_fs_t range)
{
    if ((unsigned)range > 3) return NSX_SENSOR_E_ARGUMENT;
    nsx_sensor_status_t s = update(ctx, ACCEL_CONFIG, 0x18, (uint8_t)(range << 3));
    if (s == NSX_SENSOR_OK) { ctx->_config.accel_fullscale_range = range; invalidate_fifo(ctx); }
    return s;
}
nsx_sensor_status_t mpu6050_set_sample_rate_divider(mpu6050_context_t *ctx, uint8_t divider)
{
    nsx_sensor_status_t s = ready(ctx);
    if (s != NSX_SENSOR_OK) return s;
    s = write_reg(ctx, DIVIDER, divider);
    if (s == NSX_SENSOR_OK) { ctx->_config.sample_rate_divider = divider; invalidate_fifo(ctx); }
    return s;
}
nsx_sensor_status_t mpu6050_set_sample_rate(mpu6050_context_t *ctx, uint16_t hz)
{
    if (!hz) return NSX_SENSOR_E_ARGUMENT;
    nsx_sensor_status_t s = ready(ctx);
    if (s != NSX_SENSOR_OK) return s;
    unsigned base = ctx->_config.dlpf_cfg == MPU6050_DLPF_260HZ ? 8000 : 1000;
    if (hz > base || base % hz || base / hz > 256) return NSX_SENSOR_E_ARGUMENT;
    return mpu6050_set_sample_rate_divider(ctx, (uint8_t)(base / hz - 1));
}
nsx_sensor_status_t mpu6050_get_sample_rate(mpu6050_context_t *ctx, float *hz)
{
    if (!hz) return NSX_SENSOR_E_ARGUMENT;
    nsx_sensor_status_t s = ready(ctx);
    if (s != NSX_SENSOR_OK) return s;
    if (ctx->_lowpower) return NSX_SENSOR_E_STATE;
    uint8_t bytes[2];
    s = read_reg(ctx, DIVIDER, bytes, 2);
    if (s != NSX_SENSOR_OK) return s;
    if ((bytes[1] & 7) == 7) return NSX_SENSOR_E_DEVICE;
    *hz = ((bytes[1] & 7) == 0 ? 8000.0f : 1000.0f) / (1.0f + bytes[0]);
    return NSX_SENSOR_OK;
}
nsx_sensor_status_t mpu6050_read_sensors(mpu6050_context_t *ctx, uint8_t *buffer, size_t capacity)
{
    if (!buffer || capacity < 14) return NSX_SENSOR_E_ARGUMENT;
    nsx_sensor_status_t s = active(ctx);
    if (s != NSX_SENSOR_OK) return s;
    uint8_t bytes[14];
    s = read_reg(ctx, SAMPLE, bytes, sizeof(bytes));
    if (s == NSX_SENSOR_OK) memcpy(buffer, bytes, sizeof(bytes));
    return s;
}
static mpu6050_raw_sample_t decode(const uint8_t *p, uint8_t mask)
{
    mpu6050_raw_sample_t r = {0};
    if (mask & 8) {
        for (size_t i = 0; i < 3; ++i, p += 2) r.accel[i] = signed_word(p);
        r.valid |= MPU6050_DATA_ACCEL;
    }
    if (mask & 0x80) { r.temperature = signed_word(p); p += 2; r.valid |= MPU6050_DATA_TEMP; }
    for (size_t i = 0; i < 3; ++i) {
        if (mask & (0x40 >> i)) { r.gyro[i] = signed_word(p); p += 2; r.valid |= (uint8_t)(4 << i); }
    }
    return r;
}
nsx_sensor_status_t mpu6050_read_raw(mpu6050_context_t *ctx, mpu6050_raw_sample_t *sample)
{
    if (!sample) return NSX_SENSOR_E_ARGUMENT;
    uint8_t bytes[14];
    nsx_sensor_status_t s = mpu6050_read_sensors(ctx, bytes, sizeof(bytes));
    if (s != NSX_SENSOR_OK) return s;
    mpu6050_raw_sample_t r = decode(bytes, 0xf8);
    if (ctx->_temp_disabled) { r.temperature = 0; r.valid &= (uint8_t)~MPU6050_DATA_TEMP; }
    if (ctx->_lowpower) { memset(r.gyro, 0, sizeof(r.gyro)); r.valid &= MPU6050_DATA_ACCEL; }
    *sample = r;
    return NSX_SENSOR_OK;
}
nsx_sensor_status_t mpu6050_accel_fsr_value(mpu6050_accel_fs_t range, uint16_t *value)
{
    if (!value || (unsigned)range > 3) return NSX_SENSOR_E_ARGUMENT;
    *value = (uint16_t)(2u << range); return NSX_SENSOR_OK;
}
nsx_sensor_status_t mpu6050_gyro_fsr_value(mpu6050_gyro_fs_t range, uint16_t *value)
{
    if (!value || (unsigned)range > 3) return NSX_SENSOR_E_ARGUMENT;
    *value = (uint16_t)(250u << range); return NSX_SENSOR_OK;
}
nsx_sensor_status_t mpu6050_accel_resolution(mpu6050_accel_fs_t range, float *value)
{
    if (!value || (unsigned)range > 3) return NSX_SENSOR_E_ARGUMENT;
    *value = 1.0f / (float)(16384u >> range); return NSX_SENSOR_OK;
}
nsx_sensor_status_t mpu6050_gyro_resolution(mpu6050_gyro_fs_t range, float *value)
{
    static const float sensitivity[] = {131.0f, 65.5f, 32.8f, 16.4f};
    if (!value || (unsigned)range > 3) return NSX_SENSOR_E_ARGUMENT;
    *value = 1.0f / sensitivity[range]; return NSX_SENSOR_OK;
}
nsx_sensor_status_t mpu6050_accel_to_gravity(int16_t raw, mpu6050_accel_fs_t range, float *value)
{
    if (!value) return NSX_SENSOR_E_ARGUMENT;
    float scale;
    nsx_sensor_status_t s = mpu6050_accel_resolution(range, &scale);
    if (s == NSX_SENSOR_OK) *value = raw * scale;
    return s;
}
nsx_sensor_status_t mpu6050_gyro_to_deg_per_sec(int16_t raw, mpu6050_gyro_fs_t range, float *value)
{
    if (!value) return NSX_SENSOR_E_ARGUMENT;
    float scale;
    nsx_sensor_status_t s = mpu6050_gyro_resolution(range, &scale);
    if (s == NSX_SENSOR_OK) *value = raw * scale;
    return s;
}
nsx_sensor_status_t mpu6050_temperature_to_celsius(int16_t raw, float *value)
{
    if (!value) return NSX_SENSOR_E_ARGUMENT;
    *value = raw / 340.0f + 36.53f; return NSX_SENSOR_OK;
}
nsx_sensor_status_t mpu6050_convert_sample(const mpu6050_raw_sample_t *raw,
    mpu6050_accel_fs_t accel_range, mpu6050_gyro_fs_t gyro_range, mpu6050_sample_t *sample)
{
    if (!raw || !sample || (raw->valid & ~MPU6050_DATA_ALL) ||
        (unsigned)accel_range > 3 || (unsigned)gyro_range > 3) return NSX_SENSOR_E_ARGUMENT;
    mpu6050_sample_t r = {0};
    r.valid = raw->valid;
    for (size_t i = 0; i < 3; ++i) {
        if (r.valid & MPU6050_DATA_ACCEL) (void)mpu6050_accel_to_gravity(raw->accel[i], accel_range, &r.accel_g[i]);
        if (r.valid & (4u << i)) (void)mpu6050_gyro_to_deg_per_sec(raw->gyro[i], gyro_range, &r.gyro_dps[i]);
    }
    if (r.valid & MPU6050_DATA_TEMP) (void)mpu6050_temperature_to_celsius(raw->temperature, &r.temperature_c);
    *sample = r;
    return NSX_SENSOR_OK;
}
nsx_sensor_status_t mpu6050_read_sample(mpu6050_context_t *ctx, mpu6050_sample_t *sample)
{
    if (!sample) return NSX_SENSOR_E_ARGUMENT;
    mpu6050_raw_sample_t raw;
    nsx_sensor_status_t s = mpu6050_read_raw(ctx, &raw);
    if (s != NSX_SENSOR_OK) return s;
    return mpu6050_convert_sample(&raw, ctx->_config.accel_fullscale_range, ctx->_config.gyro_fullscale_range, sample);
}
static nsx_sensor_status_t get_axes(mpu6050_context_t *ctx, bool gyro, int16_t *x, int16_t *y, int16_t *z)
{
    if (!x || !y || !z) return NSX_SENSOR_E_ARGUMENT;
    mpu6050_raw_sample_t r;
    nsx_sensor_status_t s = mpu6050_read_raw(ctx, &r);
    if (s != NSX_SENSOR_OK) return s;
    if (gyro && !(r.valid & MPU6050_DATA_GYRO_X)) return NSX_SENSOR_E_STATE;
    const int16_t *v = gyro ? r.gyro : r.accel;
    *x = v[0]; *y = v[1]; *z = v[2];
    return NSX_SENSOR_OK;
}
nsx_sensor_status_t mpu6050_get_accel_values(mpu6050_context_t *ctx, int16_t *x, int16_t *y, int16_t *z)
{ return get_axes(ctx, false, x, y, z); }
nsx_sensor_status_t mpu6050_get_gyro_values(mpu6050_context_t *ctx, int16_t *x, int16_t *y, int16_t *z)
{ return get_axes(ctx, true, x, y, z); }
nsx_sensor_status_t mpu6050_get_temperature(mpu6050_context_t *ctx, int16_t *temperature)
{
    if (!temperature) return NSX_SENSOR_E_ARGUMENT;
    mpu6050_raw_sample_t r;
    nsx_sensor_status_t s = mpu6050_read_raw(ctx, &r);
    if (s != NSX_SENSOR_OK) return s;
    if (!(r.valid & MPU6050_DATA_TEMP)) return NSX_SENSOR_E_STATE;
    *temperature = r.temperature; return NSX_SENSOR_OK;
}

nsx_sensor_status_t mpu6050_configure_fifo(mpu6050_context_t *ctx, const mpu6050_fifo_config_t *config)
{
    if (!config) return NSX_SENSOR_E_ARGUMENT;
    const uint8_t fields[] = {config->tempEnable, config->xgEnable, config->ygEnable,
        config->zgEnable, config->accelEnable, config->slv2Enable, config->slv1Enable, config->slv0Enable};
    uint8_t mask = 0;
    for (size_t i = 0; i < sizeof(fields); ++i) {
        if (fields[i] > 1) return NSX_SENSOR_E_ARGUMENT;
        mask |= (uint8_t)(fields[i] << (7 - i));
    }
    nsx_sensor_status_t s = ready(ctx);
    if (s != NSX_SENSOR_OK) return s;
    if (mask && (ctx->_any_read_clear || ctx->_lowpower || (ctx->_temp_disabled && (mask & 0x80))))
        return NSX_SENSOR_E_STATE;
    s = mpu6050_set_fifo_enable(ctx, 0);
    if (s != NSX_SENSOR_OK) return s;
    invalidate_fifo(ctx);
    s = write_reg(ctx, FIFO_ENABLE, mask);
    if (s == NSX_SENSOR_OK) ctx->_fifo_mask = mask;
    return s;
}
nsx_sensor_status_t mpu6050_set_fifo_enable(mpu6050_context_t *ctx, uint8_t enable)
{
    if (enable > 1) return NSX_SENSOR_E_ARGUMENT;
    nsx_sensor_status_t s = ready(ctx);
    if (s != NSX_SENSOR_OK) return s;
    if (enable && (!ctx->_fifo_valid || !ctx->_fifo_mask || ctx->_any_read_clear || ctx->_lowpower ||
                   (ctx->_temp_disabled && (ctx->_fifo_mask & 0x80)))) return NSX_SENSOR_E_STATE;
    s = update(ctx, USER_CTRL, 0x40, (uint8_t)(enable << 6));
    if (s == NSX_SENSOR_OK) ctx->_fifo_enabled = enable != 0;
    return s;
}
nsx_sensor_status_t mpu6050_reset_fifo(mpu6050_context_t *ctx)
{
    nsx_sensor_status_t s = ready(ctx);
    if (s != NSX_SENSOR_OK) return s;
    if (!ctx->runtime.delay_us) return NSX_SENSOR_E_UNSUPPORTED;
    s = mpu6050_set_fifo_enable(ctx, 0);
    if (s != NSX_SENSOR_OK) return s;
    invalidate_fifo(ctx);
    s = update(ctx, USER_CTRL, 4, 4);
    if (s != NSX_SENSOR_OK) return s;
    budget_t b = budget_start(ctx, 100000);
    s = poll_clear(ctx, USER_CTRL, 4, &b);
    if (s != NSX_SENSOR_OK) return s;
    /* Clear old read-to-clear overflow status before declaring recovery. */
    uint8_t status;
    s = read_reg(ctx, INT_STATUS, &status, 1);
    if (s == NSX_SENSOR_OK) ctx->_fifo_valid = ctx->_fifo_aligned = true;
    return s;
}
nsx_sensor_status_t mpu6050_get_interrupt_status(mpu6050_context_t *ctx, uint8_t *status)
{
    if (!status) return NSX_SENSOR_E_ARGUMENT;
    nsx_sensor_status_t s = ready(ctx);
    if (s != NSX_SENSOR_OK) return s;
    uint8_t v;
    s = read_reg(ctx, INT_STATUS, &v, 1);
    if (s != NSX_SENSOR_OK) { invalidate_fifo(ctx); return s; }
    if (v & MPU6050_INTERRUPT_FIFO_OVERFLOW) invalidate_fifo(ctx);
    *status = v;
    return NSX_SENSOR_OK;
}
nsx_sensor_status_t mpu6050_get_fifo_count(mpu6050_context_t *ctx, uint16_t *count)
{
    if (!count) return NSX_SENSOR_E_ARGUMENT;
    nsx_sensor_status_t s = ready(ctx);
    if (s != NSX_SENSOR_OK) return s;
    uint8_t bytes[2];
    s = read_reg(ctx, FIFO_COUNT, bytes, 2);
    if (s != NSX_SENSOR_OK) return s;
    unsigned n = (unsigned)bytes[0] << 8 | bytes[1];
    if (n > FIFO_CAPACITY) { invalidate_fifo(ctx); return NSX_SENSOR_E_OVERFLOW; }
    *count = (uint16_t)n;
    return NSX_SENSOR_OK;
}
static nsx_sensor_status_t check_overflow(mpu6050_context_t *ctx)
{
    uint8_t status;
    nsx_sensor_status_t s = mpu6050_get_interrupt_status(ctx, &status);
    if (s != NSX_SENSOR_OK) return s;
    return status & MPU6050_INTERRUPT_FIFO_OVERFLOW ? NSX_SENSOR_E_OVERFLOW : NSX_SENSOR_OK;
}
static nsx_sensor_status_t fifo_snapshot(mpu6050_context_t *ctx, uint16_t *count)
{
    nsx_sensor_status_t s = ready(ctx);
    if (s != NSX_SENSOR_OK) return s;
    if (!ctx->_fifo_enabled || !ctx->_fifo_valid || ctx->_any_read_clear) return NSX_SENSOR_E_STATE;
    s = check_overflow(ctx);
    return s == NSX_SENSOR_OK ? mpu6050_get_fifo_count(ctx, count) : s;
}
static nsx_sensor_status_t consume(mpu6050_context_t *ctx, uint8_t *bytes, size_t n)
{
    nsx_sensor_status_t s = check_overflow(ctx);
    if (s != NSX_SENSOR_OK) return s;
    s = read_reg(ctx, FIFO_DATA, bytes, n);
    if (s == NSX_SENSOR_OK) s = check_overflow(ctx);
    if (s != NSX_SENSOR_OK) invalidate_fifo(ctx);
    return s;
}
nsx_sensor_status_t mpu6050_read_fifo(mpu6050_context_t *ctx, mpu6050_raw_sample_t *frames,
                                    size_t capacity, size_t *completed)
{
    if (!completed) return NSX_SENSOR_E_ARGUMENT;
    *completed = 0;
    if (!frames || !capacity) return NSX_SENSOR_E_ARGUMENT;
    nsx_sensor_status_t s = ready(ctx);
    if (s != NSX_SENSOR_OK) return s;
    if (!ctx->_fifo_aligned || !ctx->_fifo_mask) return NSX_SENSOR_E_STATE;
    if (ctx->_fifo_mask & 7) return NSX_SENSOR_E_UNSUPPORTED;
    if (ctx->_lowpower || (ctx->_temp_disabled && (ctx->_fifo_mask & 0x80))) return NSX_SENSOR_E_STATE;
    uint16_t available;
    s = fifo_snapshot(ctx, &available);
    if (s != NSX_SENSOR_OK) return s;
    size_t frame_size = ctx->_fifo_mask & 8 ? 6 : 0;
    for (unsigned bit = 0x10; bit <= 0x80; bit <<= 1) if (ctx->_fifo_mask & bit) frame_size += 2;
    size_t count = available / frame_size;
    if (!count) return NSX_SENSOR_AGAIN;
    if (count > capacity) count = capacity;
    while (*completed < count) {
        uint8_t bytes[14];
        size_t batch = count - *completed;
        if (batch > sizeof(bytes) / frame_size) batch = sizeof(bytes) / frame_size;
        /* Publish only after the entire burst and its overflow check succeed. */
        s = consume(ctx, bytes, batch * frame_size);
        if (s != NSX_SENSOR_OK) return s;
        for (size_t i = 0; i < batch; ++i)
            frames[*completed + i] = decode(bytes + i * frame_size, ctx->_fifo_mask);
        *completed += batch;
    }
    return NSX_SENSOR_OK;
}
nsx_sensor_status_t mpu6050_read_fifo_bytes(mpu6050_context_t *ctx, uint8_t *bytes,
                                          size_t capacity, size_t *completed)
{
    if (!completed) return NSX_SENSOR_E_ARGUMENT;
    *completed = 0;
    if (!bytes || !capacity) return NSX_SENSOR_E_ARGUMENT;
    uint16_t available;
    nsx_sensor_status_t s = fifo_snapshot(ctx, &available);
    if (s != NSX_SENSOR_OK) return s;
    if (!available) return NSX_SENSOR_AGAIN;
    size_t count = available < capacity ? available : capacity;
    ctx->_fifo_aligned = false;
    while (*completed < count) {
        uint8_t chunk[14];
        size_t n = count - *completed;
        if (n > sizeof(chunk)) n = sizeof(chunk);
        s = consume(ctx, chunk, n);
        if (s != NSX_SENSOR_OK) return s;
        memcpy(bytes + *completed, chunk, n);
        *completed += n;
    }
    return NSX_SENSOR_OK;
}
nsx_sensor_status_t mpu6050_fifo_pop(mpu6050_context_t *ctx, int16_t *value)
{
    if (!value) return NSX_SENSOR_E_ARGUMENT;
    uint16_t available;
    nsx_sensor_status_t s = fifo_snapshot(ctx, &available);
    if (s != NSX_SENSOR_OK) return s;
    if (available < 2) return NSX_SENSOR_AGAIN;
    ctx->_fifo_aligned = false;
    uint8_t bytes[2];
    s = consume(ctx, bytes, 2);
    if (s == NSX_SENSOR_OK) *value = signed_word(bytes);
    return s;
}
nsx_sensor_status_t mpu6050_configure_interrupt(mpu6050_context_t *ctx, const mpu6050_int_config_t *config)
{
    if (!config) return NSX_SENSOR_E_ARGUMENT;
    const uint8_t fields[] = {config->intLevel, config->intOpen, config->latchEnable,
        config->rdClear, config->fsyncLevel, config->fsyncEnable};
    uint8_t mask = 0;
    for (size_t i = 0; i < sizeof(fields); ++i) {
        if (fields[i] > 1) return NSX_SENSOR_E_ARGUMENT;
        mask |= (uint8_t)(fields[i] << (7 - i));
    }
    nsx_sensor_status_t s = ready(ctx);
    if (s != NSX_SENSOR_OK) return s;
    if (config->rdClear && (ctx->_fifo_mask || ctx->_fifo_enabled)) return NSX_SENSOR_E_STATE;
    s = update(ctx, INT_CONFIG, 0xfc, mask);
    if (s == NSX_SENSOR_OK) ctx->_any_read_clear = config->rdClear != 0;
    return s;
}
nsx_sensor_status_t mpu6050_set_interrupt_enable(mpu6050_context_t *ctx, uint8_t overflow, uint8_t data_ready)
{
    if (overflow > 1 || data_ready > 1) return NSX_SENSOR_E_ARGUMENT;
    return update(ctx, INT_ENABLE, 0x11, (uint8_t)((overflow << 4) | data_ready));
}
static nsx_sensor_status_t reset_signal(mpu6050_context_t *ctx, bool conditions)
{
    nsx_sensor_status_t s = ready(ctx);
    if (s != NSX_SENSOR_OK) return s;
    if (!ctx->runtime.delay_us) return NSX_SENSOR_E_UNSUPPORTED;
    if (ctx->_fifo_enabled) return NSX_SENSOR_E_STATE;
    invalidate_fifo(ctx);
    s = conditions ? update(ctx, USER_CTRL, 1, 1) : write_reg(ctx, SIGNAL_RESET, 7);
    if (s != NSX_SENSOR_OK) return s;
    budget_t b = budget_start(ctx, 200000);
    s = pause_for(ctx, &b, 100000);
    if (s == NSX_SENSOR_OK) s = poll_clear(ctx, conditions ? USER_CTRL : SIGNAL_RESET, conditions ? 1 : 7, &b);
    if (s != NSX_SENSOR_OK) ctx->_configured = false;
    return s;
}
nsx_sensor_status_t mpu6050_reset_signal_paths(mpu6050_context_t *ctx) { return reset_signal(ctx, false); }
nsx_sensor_status_t mpu6050_reset_signal_conds(mpu6050_context_t *ctx) { return reset_signal(ctx, true); }
static nsx_sensor_status_t get_offset(mpu6050_context_t *ctx, uint8_t axis, int16_t *offset, bool gyro)
{
    if (axis > 2 || !offset) return NSX_SENSOR_E_ARGUMENT;
    nsx_sensor_status_t s = ready(ctx);
    if (s != NSX_SENSOR_OK) return s;
    uint8_t bytes[2];
    s = read_reg(ctx, (uint8_t)((gyro ? XG_OFFSET : XA_OFFSET) + 2 * axis), bytes, 2);
    if (s != NSX_SENSOR_OK) return s;
    if (!gyro) bytes[1] &= 0xfe;
    *offset = signed_word(bytes); return NSX_SENSOR_OK;
}
static nsx_sensor_status_t set_offset(mpu6050_context_t *ctx, uint8_t axis, int16_t offset, bool gyro)
{
    if (axis > 2) return NSX_SENSOR_E_ARGUMENT;
    nsx_sensor_status_t s = ready(ctx);
    if (s != NSX_SENSOR_OK) return s;
    uint8_t reg = (uint8_t)((gyro ? XG_OFFSET : XA_OFFSET) + 2 * axis);
    uint8_t bytes[] = {(uint8_t)((uint16_t)offset >> 8), (uint8_t)offset};
    if (!gyro) {
        uint8_t low;
        s = read_reg(ctx, (uint8_t)(reg + 1), &low, 1);
        if (s != NSX_SENSOR_OK) return s;
        bytes[1] = (uint8_t)((bytes[1] & 0xfe) | (low & 1));
    }
    invalidate_fifo(ctx);
    return write_bytes(ctx, reg, bytes, 2);
}
nsx_sensor_status_t mpu6050_set_accel_offset(mpu6050_context_t *ctx, uint8_t axis, int16_t offset)
{ return set_offset(ctx, axis, offset, false); }
nsx_sensor_status_t mpu6050_set_gyro_offset(mpu6050_context_t *ctx, uint8_t axis, int16_t offset)
{ return set_offset(ctx, axis, offset, true); }
nsx_sensor_status_t mpu6050_get_accel_offset(mpu6050_context_t *ctx, uint8_t axis, int16_t *offset)
{ return get_offset(ctx, axis, offset, false); }
nsx_sensor_status_t mpu6050_get_gyro_offset(mpu6050_context_t *ctx, uint8_t axis, int16_t *offset)
{ return get_offset(ctx, axis, offset, true); }

static nsx_sensor_status_t mean_samples(mpu6050_context_t *ctx, uint16_t samples, budget_t *b,
                                       mpu6050_raw_sample_t *mean)
{
    uint8_t status;
    nsx_sensor_status_t s;
    int64_t accel[3] = {0}, gyro[3] = {0}, temp = 0;
    mpu6050_raw_sample_t r = {0};
    for (unsigned n = 0; n < samples; ++n) {
        /* A conversion between the previous status check and burst can leave
         * DRDY set for data already consumed. Discard it before each wait.
         * This may skip a conversion, but never counts that stale flag twice. */
        s = mpu6050_get_interrupt_status(ctx, &status);
        if (s != NSX_SENSOR_OK) return s;
        do {
            s = pause_for(ctx, b, 1000);
            if (s != NSX_SENSOR_OK) return s;
            s = mpu6050_get_interrupt_status(ctx, &status);
            if (s != NSX_SENSOR_OK) return s;
        } while (!(status & MPU6050_INTERRUPT_DATA_READY));
        s = mpu6050_read_raw(ctx, &r);
        if (s != NSX_SENSOR_OK) return s;
        if (ctx->runtime.now_us && !budget_left(ctx, b)) return NSX_SENSOR_E_TIMEOUT;
        for (size_t i = 0; i < 3; ++i) { accel[i] += r.accel[i]; gyro[i] += r.gyro[i]; }
        temp += r.temperature;
    }
    for (size_t i = 0; i < 3; ++i) { r.accel[i] = (int16_t)(accel[i] / samples); r.gyro[i] = (int16_t)(gyro[i] / samples); }
    r.temperature = (int16_t)(temp / samples);
    *mean = r;
    return NSX_SENSOR_OK;
}
static nsx_sensor_status_t averaging_ready(mpu6050_context_t *ctx)
{
    nsx_sensor_status_t s = active(ctx);
    if (s != NSX_SENSOR_OK) return s;
    if (ctx->_fifo_enabled || ctx->_lowpower) return NSX_SENSOR_E_STATE;
    return ctx->runtime.delay_us ? NSX_SENSOR_OK : NSX_SENSOR_E_UNSUPPORTED;
}
nsx_sensor_status_t mpu6050_mean_sensors(mpu6050_context_t *ctx, uint16_t samples,
                                       uint32_t timeout_us, mpu6050_raw_sample_t *mean)
{
    if (!mean || !samples || samples > 4096 || !timeout_us) return NSX_SENSOR_E_ARGUMENT;
    nsx_sensor_status_t s = averaging_ready(ctx);
    if (s != NSX_SENSOR_OK) return s;
    budget_t b = budget_start(ctx, timeout_us);
    return mean_samples(ctx, samples, &b, mean);
}
mpu6050_calibration_config_t mpu6050_default_calibration_config(void)
{
    const mpu6050_calibration_config_t c = {32, 20, 8, 4, 5000000};
    return c;
}
static int32_t magnitude(int32_t n) { return n < 0 ? -n : n; }
nsx_sensor_status_t mpu6050_calibrate(mpu6050_context_t *ctx,
    const mpu6050_calibration_config_t *config, mpu6050_offsets_t *result)
{
    if (!config || !result || !config->samples || config->samples > 4096 ||
        !config->max_iterations || config->max_iterations > 256 || config->timeout_us < 100000 ||
        !config->accel_deadzone || !config->gyro_deadzone) return NSX_SENSOR_E_ARGUMENT;
    nsx_sensor_status_t s = averaging_ready(ctx);
    if (s != NSX_SENSOR_OK) return s;
    if (ctx->_config.accel_fullscale_range != MPU6050_ACCEL_FS_2G ||
        ctx->_config.gyro_fullscale_range != MPU6050_GYRO_FS_250DPS || ctx->_config.dlpf_cfg == MPU6050_DLPF_260HZ)
        return NSX_SENSOR_E_STATE;
    budget_t b = budget_start(ctx, config->timeout_us);
    mpu6050_offsets_t offsets = {0};
    for (uint8_t i = 0; i < 3; ++i) {
        s = mpu6050_get_accel_offset(ctx, i, &offsets.accel[i]);
        if (s != NSX_SENSOR_OK) return s;
        s = mpu6050_get_gyro_offset(ctx, i, &offsets.gyro[i]);
        if (s != NSX_SENSOR_OK) return s;
    }
    s = pause_for(ctx, &b, 100000);
    if (s != NSX_SENSOR_OK) return s;
    for (uint16_t iteration = 1; iteration <= config->max_iterations; ++iteration) {
        mpu6050_raw_sample_t mean;
        s = mean_samples(ctx, config->samples, &b, &mean);
        if (s != NSX_SENSOR_OK) return s;
        bool done = true;
        int16_t accel[3], gyro[3];
        for (size_t i = 0; i < 3; ++i) {
            int32_t ae = (int32_t)mean.accel[i] - (i == 2 ? 16384 : 0), ge = mean.gyro[i];
            int32_t da = 0, dg = 0;
            if (magnitude(ae) > config->accel_deadzone) {
                done = false;
                da = (ae / 16) * 2; /* Bit 0 is factory temperature compensation. */
                if (!da) da = ae > 0 ? 2 : -2;
            }
            if (magnitude(ge) > config->gyro_deadzone) {
                done = false; dg = ge / 4; if (!dg) dg = ge > 0 ? 1 : -1;
            }
            int32_t a = offsets.accel[i] - da, g = offsets.gyro[i] - dg;
            if (a < INT16_MIN || a > INT16_MAX || g < INT16_MIN || g > INT16_MAX) return NSX_SENSOR_E_OVERFLOW;
            accel[i] = (int16_t)a; gyro[i] = (int16_t)g;
        }
        if (done) { offsets.iterations = iteration; *result = offsets; return NSX_SENSOR_OK; }
        if (iteration == config->max_iterations) break;
        for (uint8_t i = 0; i < 3; ++i) {
            s = mpu6050_set_accel_offset(ctx, i, accel[i]);
            if (s != NSX_SENSOR_OK) return s;
            s = mpu6050_set_gyro_offset(ctx, i, gyro[i]);
            if (s != NSX_SENSOR_OK) return s;
            offsets.accel[i] = accel[i]; offsets.gyro[i] = gyro[i];
        }
        /* Let filter history settle before measuring the adjusted offsets. */
        s = pause_for(ctx, &b, 100000);
        if (s != NSX_SENSOR_OK) return s;
    }
    return NSX_SENSOR_E_TIMEOUT;
}
