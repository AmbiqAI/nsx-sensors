/* SPDX-License-Identifier: BSD-3-Clause */
#include <nsx/sensors/mpu6050.h>
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint8_t regs[128], fifo[1024];
    size_t fifo_len, fifo_pos, calls, fail_at, fifo_reads, fail_fifo_read;
    uint64_t time;
    bool stuck_reset, stuck_fifo, auto_ready, frozen_clock, fail_after_read;
    unsigned sample_reads, delay_calls;
    uint32_t transfer_us, oversleep_us;
    size_t i2c_clocks, overflow_fifo_read;
    int16_t bias_accel[3], bias_gyro[3];
    bool simulate_offsets;
} fake_t;
static void put_word(uint8_t *p, int16_t value)
{ p[0] = (uint8_t)((uint16_t)value >> 8); p[1] = (uint8_t)value; }
static int16_t word(const uint8_t *p)
{ unsigned u = (unsigned)p[0] * 256 + p[1]; return (int16_t)(u < 32768 ? (int)u : (int)u - 65536); }
static void refresh(fake_t *f)
{
    for (size_t i = 0; i < 3; ++i) {
        int32_t a = f->bias_accel[i] + (i == 2 ? 16384 : 0);
        int32_t g = f->bias_gyro[i];
        if (f->simulate_offsets) {
            uint8_t offset[2] = {f->regs[6 + 2*i], (uint8_t)(f->regs[7 + 2*i] & 0xfe)};
            a += word(offset) * 8; g += word(f->regs + 0x13 + 2*i) * 4;
        }
        assert(a >= -32768 && a <= 32767 && g >= -32768 && g <= 32767);
        put_word(f->regs + 0x3b + 2*i, (int16_t)a);
        put_word(f->regs + 0x43 + 2*i, (int16_t)g);
    }
}
static nsx_sensor_status_t transfer(void *user, const nsx_sensor_transfer_t *t)
{
    fake_t *f = user;
    ++f->calls; f->time += f->transfer_us;
    f->i2c_clocks += 9 * (t->rx_size ? 2 + t->tx_size + t->rx_size : 1 + t->tx_size);
    assert(t->tx && t->tx_size >= 1 && t->tx_size <= 3);
    uint8_t reg = t->tx[0];
    bool fail = f->calls == f->fail_at;
    if (fail && !f->fail_after_read) return NSX_SENSOR_E_IO;
    if (t->rx_size) {
        assert(t->tx_size == 1 && t->rx && t->rx_size <= 14);
        if (reg == 0x74) {
            if (!(f->regs[0x6a] & 0x40)) {
                memset(t->rx, 0, t->rx_size); /* Disabled FIFO cannot consume data. */
                return NSX_SENSOR_OK;
            }
            ++f->fifo_reads;
            assert(f->fifo_pos + t->rx_size <= f->fifo_len);
            memcpy(t->rx, f->fifo + f->fifo_pos, t->rx_size);
            f->fifo_pos += t->rx_size;
            if (f->fifo_reads == f->fail_fifo_read) fail = true;
            if (f->fifo_reads == f->overflow_fifo_read) f->regs[0x3a] |= 0x10;
        } else if (reg == 0x72) {
            assert(t->rx_size == 2);
            size_t count = f->fifo_len - f->fifo_pos;
            t->rx[0] = (uint8_t)(count >> 8); t->rx[1] = (uint8_t)count;
        } else {
            assert((size_t)reg + t->rx_size <= sizeof(f->regs));
            if (reg == 0x3b) ++f->sample_reads;
            memcpy(t->rx, f->regs + reg, t->rx_size);
        }
        if (reg == 0x3a || (f->regs[0x37] & 0x10)) f->regs[0x3a] = 0;
    } else {
        assert(t->tx_size >= 2);
        memcpy(f->regs + reg, t->tx + 1, t->tx_size - 1);
        if (reg == 0x6b && (t->tx[1] & 0x80) && !f->stuck_reset) {
            memset(f->regs, 0, sizeof(f->regs)); f->regs[0x75] = 0x68; f->regs[0x6b] = 0x40;
            f->fifo_len = f->fifo_pos = 0;
        }
        if (reg == 0x6a && (t->tx[1] & 4) && !f->stuck_fifo) {
            assert(!(t->tx[1] & 0x40));
            f->fifo_len = f->fifo_pos = 0; f->regs[0x6a] &= 0xfb;
        }
        if (reg == 0x6a) f->regs[0x6a] &= 0xfe;
        if (reg == 0x68) f->regs[0x68] = 0;
    }
    return fail ? NSX_SENSOR_E_IO : NSX_SENSOR_OK;
}
static void delay(void *user, uint32_t us)
{
    fake_t *f = user; ++f->delay_calls; f->time += us + f->oversleep_us;
    if (f->auto_ready && !(f->regs[0x6b] & 0x40)) { refresh(f); f->regs[0x3a] |= 1; }
}
static uint64_t now(void *user) { fake_t *f = user; return f->frozen_clock ? 0 : f->time; }
static const nsx_sensor_bus_ops_t ops = {transfer};
static void bind_fake(mpu6050_context_t *ctx, fake_t *f)
{
    memset(f, 0, sizeof(*f)); f->regs[0x75] = 0x68;
    nsx_sensor_bus_t bus = {&ops, f, NSX_SENSOR_BUS_I2C, 3, 14};
    nsx_sensor_runtime_t runtime = {f, now, delay};
    assert(mpu6050_bind(ctx, &bus, &runtime) == NSX_SENSOR_OK);
    assert(f->calls == 0);
}
static void init_fake(mpu6050_context_t *ctx, fake_t *f)
{
    bind_fake(ctx, f);
    mpu6050_config_t c = mpu6050_default_config();
    assert(mpu6050_init(ctx, &c) == NSX_SENSOR_OK);
    assert(f->regs[0x6b] == 0x41 && !f->regs[0x23] && !f->regs[0x38]);
    assert(mpu6050_set_sleep(ctx, 0) == NSX_SENSOR_OK);
}
static void fill_fifo(fake_t *f, size_t frames)
{
    static const uint8_t frame[] = {0xff, 0xfe, 0x7f, 0xff, 0x80, 0, 0x01, 0x54, 0, 1, 0xff, 0xff, 0, 3};
    f->fifo_pos = 0; f->fifo_len = frames * sizeof(frame);
    for (size_t i = 0; i < frames; ++i) memcpy(f->fifo + i*sizeof(frame), frame, sizeof(frame));
}
static void setup_fifo(mpu6050_context_t *ctx, fake_t *f)
{
    init_fake(ctx, f);
    const mpu6050_fifo_config_t config = {1,1,1,1,1,0,0,0};
    assert(mpu6050_configure_fifo(ctx, &config) == NSX_SENSOR_OK);
    assert(mpu6050_set_fifo_enable(ctx, 1) == NSX_SENSOR_E_STATE);
    assert(mpu6050_reset_fifo(ctx) == NSX_SENSOR_OK);
    assert(!(f->regs[0x6a] & 0x40));
    assert(mpu6050_set_fifo_enable(ctx, 1) == NSX_SENSOR_OK);
    fill_fifo(f, 3);
}
static void test_lifecycle(void)
{
    fake_t f; mpu6050_context_t ctx;
    bind_fake(&ctx, &f);
    mpu6050_config_t c = mpu6050_default_config();
    assert(mpu6050_validate_config(&c) == NSX_SENSOR_OK);
    uint8_t id = 99;
    f.regs[0x75] = 0x69;
    assert(mpu6050_probe(&ctx, &id) == NSX_SENSOR_OK && id == 0x69);
    f.regs[0x75] = 0x70;
    assert(mpu6050_probe(&ctx, &id) == NSX_SENSOR_E_DEVICE && id == 0x69);
    f.regs[0x75] = 0x68;
    ctx.runtime.delay_us = NULL;
    size_t calls = f.calls;
    assert(mpu6050_init(&ctx, &c) == NSX_SENSOR_E_UNSUPPORTED && f.calls == calls);
    ctx.runtime.delay_us = delay;
    assert(mpu6050_init(&ctx, &c) == NSX_SENSOR_OK);
    mpu6050_raw_sample_t r = {.temperature = 123};
    assert(mpu6050_read_raw(&ctx, &r) == NSX_SENSOR_E_STATE && r.temperature == 123);
    assert(mpu6050_set_sleep(&ctx, 0) == NSX_SENSOR_OK);
    assert(mpu6050_read_raw(&ctx, &r) == NSX_SENSOR_OK);
    assert(mpu6050_device_reset(&ctx, 99999) == NSX_SENSOR_E_ARGUMENT);
    f.stuck_reset = true; f.frozen_clock = true;
    unsigned delays = f.delay_calls;
    assert(mpu6050_device_reset(&ctx, 105000) == NSX_SENSOR_E_TIMEOUT);
    assert(f.delay_calls - delays == 6);
    assert(mpu6050_read_raw(&ctx, &r) == NSX_SENSOR_E_STATE);
    f.stuck_reset = false; ctx.runtime.now_us = NULL;
    assert(mpu6050_init(&ctx, &c) == NSX_SENSOR_OK);
    nsx_sensor_bus_t bad = ctx.bus; bad.kind = NSX_SENSOR_BUS_SPI;
    assert(mpu6050_bind(&ctx, &bad, NULL) == NSX_SENSOR_E_UNSUPPORTED);
    bad.kind = NSX_SENSOR_BUS_I2C; bad.max_rx_size = 13;
    assert(mpu6050_bind(&ctx, &bad, NULL) == NSX_SENSOR_E_UNSUPPORTED);
    assert(mpu6050_set_sleep(&ctx, 0) == NSX_SENSOR_OK); /* Rejected bind preserves context. */
    c.clock_src = (mpu6050_clock_src_t)6;
    calls = f.calls;
    assert(mpu6050_init(&ctx, &c) == NSX_SENSOR_E_ARGUMENT && f.calls == calls);
}
static void test_config_and_data(void)
{
    fake_t f; mpu6050_context_t ctx; init_fake(&ctx, &f);
    for (unsigned i = 0; i < 4; ++i) {
        f.regs[0x1b] = 0xe7; f.regs[0x1c] = 0xe7;
        assert(mpu6050_set_gyro_full_scale(&ctx, (mpu6050_gyro_fs_t)i) == NSX_SENSOR_OK);
        assert(mpu6050_set_accel_full_scale(&ctx, (mpu6050_accel_fs_t)i) == NSX_SENSOR_OK);
        assert(f.regs[0x1b] == (0xe7 | (i << 3)) && f.regs[0x1c] == (0xe7 | (i << 3)));
        float value;
        assert(mpu6050_accel_to_gravity((int16_t)(16384 >> i), (mpu6050_accel_fs_t)i, &value) == NSX_SENSOR_OK);
        assert(fabsf(value - 1) < 0.00001f);
    }
    assert(mpu6050_set_accel_full_scale(&ctx, (mpu6050_accel_fs_t)-1) == NSX_SENSOR_E_ARGUMENT);
    assert(mpu6050_set_sample_rate(&ctx, 333) == NSX_SENSOR_E_ARGUMENT);
    assert(mpu6050_set_sample_rate(&ctx, 100) == NSX_SENSOR_OK && f.regs[0x19] == 9);
    assert(mpu6050_set_lowpass_filter(&ctx, MPU6050_DLPF_260HZ) == NSX_SENSOR_OK);
    float hz = -1;
    assert(mpu6050_get_sample_rate(&ctx, &hz) == NSX_SENSOR_OK && hz == 800);
    assert(mpu6050_set_sample_rate(&ctx, 8000) == NSX_SENSOR_OK && f.regs[0x19] == 0);
    assert(mpu6050_set_sample_rate(&ctx, 10) == NSX_SENSOR_E_ARGUMENT);
    f.regs[0x1a] = 7;
    assert(mpu6050_get_sample_rate(&ctx, &hz) == NSX_SENSOR_E_DEVICE && hz == 800);
    fill_fifo(&f, 1); memcpy(f.regs + 0x3b, f.fifo, 14);
    mpu6050_raw_sample_t raw;
    assert(mpu6050_read_raw(&ctx, &raw) == NSX_SENSOR_OK);
    assert(raw.accel[0] == -2 && raw.accel[1] == 32767 && raw.accel[2] == -32768);
    assert(raw.temperature == 340 && raw.gyro[0] == 1 && raw.gyro[1] == -1 && raw.gyro[2] == 3 && raw.valid == 31);
    mpu6050_sample_t scaled;
    assert(mpu6050_convert_sample(&raw, MPU6050_ACCEL_FS_2G, MPU6050_GYRO_FS_250DPS, &scaled) == NSX_SENSOR_OK);
    assert(fabsf(scaled.temperature_c - 37.53f) < 0.0001f && scaled.accel_g[2] == -2);
    float value = 7;
    assert(mpu6050_gyro_to_deg_per_sec(131, MPU6050_GYRO_FS_250DPS, &value) == NSX_SENSOR_OK && value == 1);
    assert(mpu6050_gyro_resolution((mpu6050_gyro_fs_t)4, &value) == NSX_SENSOR_E_ARGUMENT && value == 1);
    assert(mpu6050_set_temperature_disable(&ctx, 1) == NSX_SENSOR_OK);
    assert(mpu6050_read_raw(&ctx, &raw) == NSX_SENSOR_OK && !(raw.valid & MPU6050_DATA_TEMP) && !raw.temperature);
    int16_t t = 77;
    assert(mpu6050_get_temperature(&ctx, &t) == NSX_SENSOR_E_STATE && t == 77);
    assert(mpu6050_set_lowpower_accel_mode(&ctx, 2) == NSX_SENSOR_OK);
    assert(f.regs[0x6b] == 0x28 && f.regs[0x6c] == 0x87);
    assert(mpu6050_read_raw(&ctx, &raw) == NSX_SENSOR_OK && raw.valid == MPU6050_DATA_ACCEL);
    assert(mpu6050_set_temperature_disable(&ctx, 0) == NSX_SENSOR_E_STATE);
}
static void test_failures(void)
{
    fake_t f; mpu6050_context_t ctx;
    /* Every transaction of initialization must propagate an I/O failure. */
    init_fake(&ctx, &f); size_t init_calls = f.calls - 2;
    for (size_t i = 1; i <= init_calls; ++i) {
        bind_fake(&ctx, &f); f.fail_at = i;
        mpu6050_config_t c = mpu6050_default_config();
        assert(mpu6050_init(&ctx, &c) == NSX_SENSOR_E_IO && !ctx._configured);
    }
    init_fake(&ctx, &f);
    mpu6050_raw_sample_t raw = {.temperature = 123}, saved = raw;
    f.fail_at = f.calls + 1; f.fail_after_read = true;
    assert(mpu6050_read_raw(&ctx, &raw) == NSX_SENSOR_E_IO && !memcmp(&raw, &saved, sizeof(raw)));
    f.fail_at = f.calls + 2;
    assert(mpu6050_set_gyro_full_scale(&ctx, MPU6050_GYRO_FS_500DPS) == NSX_SENSOR_E_IO);
    assert(mpu6050_read_raw(&ctx, &raw) == NSX_SENSOR_E_STATE);
    init_fake(&ctx, &f);
    f.fail_at = f.calls + 1;
    assert(mpu6050_set_accel_full_scale(&ctx, MPU6050_ACCEL_FS_4G) == NSX_SENSOR_E_IO && ctx._configured);
    /* A failed RMW read never attempts the write. */
    assert(f.regs[0x1c] == 0);
}
static void test_fifo(void)
{
    fake_t f; mpu6050_context_t ctx; setup_fifo(&ctx, &f);
    mpu6050_raw_sample_t frames[4]; size_t completed = 99;
    assert(mpu6050_read_fifo(&ctx, frames, 2, &completed) == NSX_SENSOR_OK && completed == 2);
    assert(f.fifo_pos == 28 && frames[1].accel[0] == -2 && frames[1].temperature == 340 && frames[1].gyro[2] == 3);
    assert(mpu6050_read_fifo(&ctx, frames, 4, &completed) == NSX_SENSOR_OK && completed == 1);
    assert(mpu6050_read_fifo(&ctx, frames, 4, &completed) == NSX_SENSOR_AGAIN && completed == 0);
    fill_fifo(&f, 1); f.fifo_len = 13;
    assert(mpu6050_read_fifo(&ctx, frames, 4, &completed) == NSX_SENSOR_AGAIN && f.fifo_pos == 0);
    setup_fifo(&ctx, &f);
    f.fail_fifo_read = 2;
    memset(frames, 0x5a, sizeof(frames)); mpu6050_raw_sample_t sentinel = frames[1];
    assert(mpu6050_read_fifo(&ctx, frames, 4, &completed) == NSX_SENSOR_E_IO && completed == 1);
    assert(!memcmp(&frames[1], &sentinel, sizeof(sentinel)) && f.fifo_pos == 28);
    size_t calls = f.calls;
    assert(mpu6050_read_fifo(&ctx, frames, 4, &completed) == NSX_SENSOR_E_STATE && completed == 0 && f.calls == calls);
    assert(mpu6050_reset_fifo(&ctx) == NSX_SENSOR_OK && !ctx._fifo_enabled);
    assert(mpu6050_set_fifo_enable(&ctx, 1) == NSX_SENSOR_OK);
    fill_fifo(&f, 1);
    int16_t v = 123;
    assert(mpu6050_fifo_pop(&ctx, &v) == NSX_SENSOR_OK && v == -2 && f.fifo_pos == 2);
    assert(mpu6050_read_fifo(&ctx, frames, 4, &completed) == NSX_SENSOR_E_STATE);
    setup_fifo(&ctx, &f); f.regs[0x3a] = 0x10;
    assert(mpu6050_read_fifo(&ctx, frames, 4, &completed) == NSX_SENSOR_E_OVERFLOW && !completed && !f.fifo_pos);
    assert(mpu6050_read_fifo(&ctx, frames, 4, &completed) == NSX_SENSOR_E_STATE);
    setup_fifo(&ctx, &f); f.regs[0x3a] = 0x10;
    uint8_t status;
    assert(mpu6050_get_interrupt_status(&ctx, &status) == NSX_SENSOR_OK && status == 0x10);
    assert(mpu6050_read_fifo(&ctx, frames, 4, &completed) == NSX_SENSOR_E_STATE);
    setup_fifo(&ctx, &f); f.fifo_len = 1025;
    uint16_t count = 7;
    assert(mpu6050_get_fifo_count(&ctx, &count) == NSX_SENSOR_E_OVERFLOW && count == 7);
    setup_fifo(&ctx, &f);
    const mpu6050_int_config_t intr = {.rdClear = 1};
    calls = f.calls;
    assert(mpu6050_configure_interrupt(&ctx, &intr) == NSX_SENSOR_E_STATE && f.calls == calls);
    assert(mpu6050_set_sample_rate(&ctx, 50) == NSX_SENSOR_OK);
    assert(mpu6050_read_fifo(&ctx, frames, 4, &completed) == NSX_SENSOR_E_STATE);
    assert(mpu6050_reset_fifo(&ctx) == NSX_SENSOR_OK);
    assert(mpu6050_set_fifo_enable(&ctx, 1) == NSX_SENSOR_OK);
    fill_fifo(&f, 1);
    uint8_t bytes[16];
    assert(mpu6050_read_fifo_bytes(&ctx, bytes, sizeof(bytes), &completed) == NSX_SENSOR_OK && completed == 14);
    assert(!memcmp(bytes, f.fifo, 14));
    f.stuck_fifo = true; unsigned delays = f.delay_calls;
    assert(mpu6050_reset_fifo(&ctx) == NSX_SENSOR_E_TIMEOUT && f.delay_calls - delays == 100);
}
static void test_fifo_formats(void)
{
    fake_t f; mpu6050_context_t ctx; init_fake(&ctx, &f);
    /* Each native format's byte layout and channel validity, including subsets. */
    for (unsigned channels = 1; channels < 32; ++channels) {
        mpu6050_fifo_config_t c = {(channels >> 1)&1, (channels >> 2)&1,
            (channels >> 3)&1, (channels >> 4)&1, channels&1, 0,0,0};
        assert(mpu6050_configure_fifo(&ctx, &c) == NSX_SENSOR_OK);
        assert(mpu6050_reset_fifo(&ctx) == NSX_SENSOR_OK);
        assert(mpu6050_set_fifo_enable(&ctx, 1) == NSX_SENSOR_OK);
        size_t size = 0;
        if (c.accelEnable) for (unsigned i = 0; i < 3; ++i) { put_word(f.fifo + size, (int16_t)(100+i)); size += 2; }
        if (c.tempEnable) { put_word(f.fifo + size, 200); size += 2; }
        for (unsigned i = 0; i < 3; ++i) if (channels & (4u << i)) { put_word(f.fifo + size, (int16_t)(300+i)); size += 2; }
        f.fifo_len = size;
        mpu6050_raw_sample_t r; size_t done;
        assert(mpu6050_read_fifo(&ctx, &r, 1, &done) == NSX_SENSOR_OK && done == 1 && r.valid == channels);
        for (unsigned i = 0; i < 3; ++i) {
            assert(r.accel[i] == (c.accelEnable ? (int)(100+i) : 0));
            assert(r.gyro[i] == (channels & (4u << i) ? (int)(300+i) : 0));
        }
        assert(r.temperature == (c.tempEnable ? 200 : 0));
    }
    mpu6050_fifo_config_t slave = {.slv0Enable = 1};
    assert(mpu6050_configure_fifo(&ctx, &slave) == NSX_SENSOR_OK);
    assert(mpu6050_reset_fifo(&ctx) == NSX_SENSOR_OK);
    mpu6050_raw_sample_t r; size_t done;
    assert(mpu6050_read_fifo(&ctx, &r, 1, &done) == NSX_SENSOR_E_UNSUPPORTED);
}
static void test_offsets_and_calibration(void)
{
    fake_t f; mpu6050_context_t ctx; init_fake(&ctx, &f);
    f.regs[7] = 1;
    assert(mpu6050_set_accel_offset(&ctx, 0, -18) == NSX_SENSOR_OK);
    assert(f.regs[6] == 0xff && f.regs[7] == 0xef);
    int16_t offset;
    assert(mpu6050_get_accel_offset(&ctx, 0, &offset) == NSX_SENSOR_OK && offset == -18);
    assert(mpu6050_set_gyro_offset(&ctx, 2, -32768) == NSX_SENSOR_OK);
    assert(mpu6050_get_gyro_offset(&ctx, 2, &offset) == NSX_SENSOR_OK && offset == -32768);
    init_fake(&ctx, &f);
    mpu6050_raw_sample_t mean = {.temperature = 88};
    f.frozen_clock = true;
    assert(mpu6050_mean_sensors(&ctx, 2, 5000, &mean) == NSX_SENSOR_E_TIMEOUT && mean.temperature == 88);
    assert(f.sample_reads == 0);
    f.auto_ready = true; f.frozen_clock = false;
    f.bias_accel[0] = -320; f.bias_gyro[1] = 80;
    assert(mpu6050_mean_sensors(&ctx, 3, 10000, &mean) == NSX_SENSOR_OK);
    assert(mean.accel[0] == -320 && mean.accel[2] == 16384 && mean.gyro[1] == 80 && f.sample_reads == 3);
    f.simulate_offsets = true; f.regs[7] = 1;
    mpu6050_calibration_config_t c = mpu6050_default_calibration_config();
    c.samples = 2;
    mpu6050_offsets_t result;
    assert(mpu6050_calibrate(&ctx, &c, &result) == NSX_SENSOR_OK);
    assert(result.accel[0] == 40 && result.accel[2] == 0 && result.gyro[1] == -20 && result.iterations == 2);
    assert(f.regs[7] & 1);
    c.max_iterations = 1; f.bias_gyro[0] = 100;
    mpu6050_offsets_t saved = result;
    assert(mpu6050_calibrate(&ctx, &c, &result) == NSX_SENSOR_E_TIMEOUT && !memcmp(&result, &saved, sizeof(result)));
    c.samples = 0;
    assert(mpu6050_calibrate(&ctx, &c, &result) == NSX_SENSOR_E_ARGUMENT);
    assert(mpu6050_reset_signal_paths(&ctx) == NSX_SENSOR_OK);
    assert(mpu6050_reset_signal_conds(&ctx) == NSX_SENSOR_OK);
}
static void test_instances_and_recovery(void)
{
    fake_t a, b; mpu6050_context_t x, y;
    init_fake(&x, &a); init_fake(&y, &b);
    assert(mpu6050_set_accel_full_scale(&y, MPU6050_ACCEL_FS_8G) == NSX_SENSOR_OK);
    put_word(a.regs + 0x3b, 4096); put_word(b.regs + 0x3b, 4096);
    mpu6050_sample_t sx, sy;
    assert(mpu6050_read_sample(&x, &sx) == NSX_SENSOR_OK);
    assert(mpu6050_read_sample(&y, &sy) == NSX_SENSOR_OK);
    assert(sx.accel_g[0] == 0.25f && sy.accel_g[0] == 1.0f);
    assert(a.regs[0x1c] == 0 && b.regs[0x1c] == 16);
    /* Power2 changed, but final Power1 RMW read fails: cached mode is uncertain. */
    a.fail_at = a.calls + 4;
    assert(mpu6050_set_lowpower_accel_mode(&x, 1) == NSX_SENSOR_E_IO && !x._configured);
    assert(mpu6050_read_sample(&y, &sy) == NSX_SENSOR_OK);
    init_fake(&x, &a); a.oversleep_us = 100001;
    assert(mpu6050_device_reset(&x, 200000) == NSX_SENSOR_E_TIMEOUT);
    init_fake(&x, &a); a.stuck_reset = true; x.runtime.now_us = NULL;
    assert(mpu6050_device_reset(&x, 101000) == NSX_SENSOR_E_TIMEOUT);
    setup_fifo(&x, &a); a.regs[0x3a] = 0x10;
    a.fail_at = a.calls + 1; a.fail_after_read = true;
    uint8_t status = 0x55;
    assert(mpu6050_get_interrupt_status(&x, &status) == NSX_SENSOR_E_IO && status == 0x55);
    mpu6050_raw_sample_t frame; size_t done;
    assert(mpu6050_read_fifo(&x, &frame, 1, &done) == NSX_SENSOR_E_STATE);
    /* Error at each read in one frame: snapshot status/count, pre-status,
     * consuming read and post-status. No frame is published in any case. */
    for (size_t i = 1; i <= 5; ++i) {
        setup_fifo(&x, &a); a.fail_at = a.calls + i; a.fail_after_read = true;
        memset(&frame, 0x33, sizeof(frame)); mpu6050_raw_sample_t saved = frame;
        assert(mpu6050_read_fifo(&x, &frame, 1, &done) == NSX_SENSOR_E_IO && done == 0);
        assert(!memcmp(&frame, &saved, sizeof(frame)));
        if (i != 2) assert(!x._fifo_valid); /* Count read cannot consume FIFO/status. */
    }
    init_fake(&x, &a);
    const mpu6050_int_config_t intr = {1,1,1,1,1,1};
    a.regs[0x37] = 2; /* Preserve I2C bypass field. */
    assert(mpu6050_configure_interrupt(&x, &intr) == NSX_SENSOR_OK && a.regs[0x37] == 0xfe);
    const mpu6050_fifo_config_t fifo = {.accelEnable = 1};
    assert(mpu6050_configure_fifo(&x, &fifo) == NSX_SENSOR_E_STATE);
    assert(mpu6050_set_interrupt_enable(&x, 1, 1) == NSX_SENSOR_OK && a.regs[0x38] == 0x11);
}
static void test_fifo_enable_and_batches(void)
{
    fake_t f; mpu6050_context_t ctx; setup_fifo(&ctx, &f);
    assert(mpu6050_set_fifo_enable(&ctx, 0) == NSX_SENSOR_OK);
    mpu6050_raw_sample_t frames[15];
    memset(frames, 0x5a, sizeof(frames));
    mpu6050_raw_sample_t saved = frames[0];
    size_t done = 99, calls = f.calls;
    int16_t value = 77; uint8_t bytes[4] = {1,2,3,4};
    assert(mpu6050_read_fifo(&ctx, frames, 15, &done) == NSX_SENSOR_E_STATE && done == 0);
    assert(mpu6050_read_fifo_bytes(&ctx, bytes, 4, &done) == NSX_SENSOR_E_STATE && done == 0);
    assert(mpu6050_fifo_pop(&ctx, &value) == NSX_SENSOR_E_STATE && value == 77);
    assert(!memcmp(frames, &saved, sizeof(saved)) && bytes[0] == 1 && bytes[3] == 4);
    assert(f.calls == calls && f.fifo_pos == 0);
    /* Disabling preserves queued data; re-enable permits consuming it. */
    assert(mpu6050_set_fifo_enable(&ctx, 1) == NSX_SENSOR_OK);
    assert(mpu6050_read_fifo(&ctx, frames, 1, &done) == NSX_SENSOR_OK && done == 1);

    const mpu6050_fifo_config_t config = {.xgEnable = 1};
    assert(mpu6050_configure_fifo(&ctx, &config) == NSX_SENSOR_OK);
    assert(mpu6050_set_lowpass_filter(&ctx, MPU6050_DLPF_260HZ) == NSX_SENSOR_OK);
    assert(mpu6050_set_sample_rate(&ctx, 8000) == NSX_SENSOR_OK);
    for (unsigned failure = 0; failure < 3; ++failure) {
        assert(mpu6050_reset_fifo(&ctx) == NSX_SENSOR_OK);
        assert(mpu6050_set_fifo_enable(&ctx, 1) == NSX_SENSOR_OK);
        for (size_t i = 0; i < 15; ++i) put_word(f.fifo + 2*i, (int16_t)((int)i - 7));
        f.fifo_len = 30; f.fifo_reads = 0;
        f.fail_fifo_read = failure == 1 ? 2 : 0;
        f.overflow_fifo_read = failure == 2 ? 2 : 0;
        memset(frames, 0x5a, sizeof(frames));
        nsx_sensor_status_t result = mpu6050_read_fifo(&ctx, frames, 15, &done);
        if (!failure) {
            assert(result == NSX_SENSOR_OK && done == 15 && f.fifo_pos == 30);
            for (size_t i = 0; i < done; ++i)
                assert(frames[i].gyro[0] == (int)i - 7 && frames[i].valid == MPU6050_DATA_GYRO_X);
        } else {
            assert(result == (failure == 1 ? NSX_SENSOR_E_IO : NSX_SENSOR_E_OVERFLOW));
            assert(done == 7 && f.fifo_pos == 28 && !ctx._fifo_valid);
            for (size_t i = 0; i < done; ++i) assert(frames[i].gyro[0] == (int)i - 7);
            for (size_t i = done; i < 15; ++i) assert(!memcmp(&frames[i], &saved, sizeof(saved)));
        }
    }
    assert(mpu6050_reset_fifo(&ctx) == NSX_SENSOR_OK);
    assert(mpu6050_set_fifo_enable(&ctx, 1) == NSX_SENSOR_OK);
    f.overflow_fifo_read = f.fail_fifo_read = 0; f.fifo_reads = 0;
    f.fifo_len = 15; /* Seven words and an incomplete tail. */
    f.i2c_clocks = 0;
    assert(mpu6050_read_fifo(&ctx, frames, 15, &done) == NSX_SENSOR_OK && done == 7 && f.fifo_pos == 14);
    /* Include snapshot/status overhead: bus demand must fit 400kHz even when
     * each call has just seven 8kHz gyro samples. Excludes software delays. */
    assert(f.i2c_clocks * 8000 / done < 400000);
    assert(mpu6050_read_fifo(&ctx, frames, 15, &done) == NSX_SENSOR_AGAIN && done == 0 && f.fifo_pos == 14);
    assert(mpu6050_reset_fifo(&ctx) == NSX_SENSOR_OK);
    assert(mpu6050_read_fifo(&ctx, frames, 15, &done) == NSX_SENSOR_E_STATE);
}
typedef struct {
    fake_t device;
    uint64_t epoch;
    unsigned produced, reads;
    int16_t samples[4];
} timed_fake_t;
static void tick_conversion(timed_fake_t *t)
{
    unsigned n = (unsigned)((t->device.time - t->epoch) / 10000);
    if (n > t->produced) {
        t->produced = n;
        put_word(t->device.regs + 0x3b, (int16_t)(100*n));
        t->device.regs[0x3a] |= 1;
    }
}
static void conversion_delay(void *user, uint32_t us)
{
    timed_fake_t *t = user;
    t->device.time += us;
    tick_conversion(t);
}
static nsx_sensor_status_t conversion_transfer(void *user, const nsx_sensor_transfer_t *transfer_desc)
{
    timed_fake_t *t = user;
    if (transfer_desc->tx[0] == 0x3b && transfer_desc->rx_size) {
        /* Conversion advances independently while the task waits for the bus. */
        if (!t->reads) t->device.time += 10000;
        tick_conversion(t);
        assert(t->reads < 4);
        t->samples[t->reads++] = word(t->device.regs + 0x3b);
    }
    return transfer(&t->device, transfer_desc);
}
static void test_fresh_average(void)
{
    for (unsigned any_read_clear = 0; any_read_clear < 2; ++any_read_clear) {
        timed_fake_t t = {0}; mpu6050_context_t ctx;
        init_fake(&ctx, &t.device);
        const mpu6050_int_config_t intr = {.rdClear = (uint8_t)any_read_clear};
        assert(mpu6050_configure_interrupt(&ctx, &intr) == NSX_SENSOR_OK);
        assert(mpu6050_set_interrupt_enable(&ctx, 0, 1) == NSX_SENSOR_OK);
        static const nsx_sensor_bus_ops_t timed_ops = {conversion_transfer};
        ctx.bus.ops = &timed_ops; ctx.bus.user = &t;
        ctx.runtime.delay_us = conversion_delay; ctx.runtime.user = &t;
        t.epoch = t.device.time;
        mpu6050_raw_sample_t mean;
        assert(mpu6050_mean_sensors(&ctx, 2, 100000, &mean) == NSX_SENSOR_OK);
        assert(t.reads == 2 && t.samples[0] == 200 && t.samples[1] == 300);
        assert(mean.accel[0] == 250 && t.device.time - t.epoch == 30000);
        /* Clearing pending DRDY does not extend the timeout to obtain freshness. */
        t.device.regs[0x3a] = 1;
        mpu6050_raw_sample_t saved = mean;
        assert(mpu6050_mean_sensors(&ctx, 1, 5000, &mean) == NSX_SENSOR_E_TIMEOUT);
        assert(!memcmp(&mean, &saved, sizeof(mean)));
    }
}
int main(void)
{
    test_lifecycle(); test_config_and_data(); test_failures(); test_fifo();
    test_fifo_formats(); test_offsets_and_calibration(); test_instances_and_recovery();
    test_fifo_enable_and_batches(); test_fresh_average();
    puts("MPU6050: lifecycle, register fields, samples, FIFO recovery, offsets and bounded calibration passed");
    return 0;
}
