/* SPDX-License-Identifier: BSD-3-Clause */
#include <assert.h>
#include <string.h>
#include <nsx/sensors/t5838.h>
#include "../../examples/spark/t5838_pair.h"
typedef struct {
    uint8_t reg[256], addresses[32]; unsigned calls, fail;
    bool after, wake, wake_fail;
} fake_t;
/* Decode waveform independently into wire bytes; reject all malformed timing. */
static nsx_sensor_status_t send_frame(void *user, const t5838_write_frame_t *f)
{
    fake_t *device = user;
    uint8_t bytes[3] = {0};
    assert(f->high_cycles[0] == 10 && f->high_cycles[25] > 128);
    assert(f->space_cycles == 10 && f->pre_cycles >= 5 && f->post_cycles >= 5);
    for (unsigned i = 0; i < 24; ++i) {
        uint16_t width = f->high_cycles[i + 1];
        assert(width == 10 || width == 30);
        bytes[i / 8] = (uint8_t)((bytes[i / 8] << 1) | (width == 30));
    }
    assert(bytes[0] == 0xa6);
    bool fail = ++device->calls == device->fail;
    if (fail && !device->after) return NSX_SENSOR_E_IO;
    device->addresses[(device->calls - 1) % 32] = bytes[1];
    device->reg[bytes[1]] = bytes[2];
    return fail ? NSX_SENSOR_E_IO : NSX_SENSOR_OK;
}
static nsx_sensor_status_t wake(void *user, bool *active)
{
    fake_t *d = user; *active = d->wake;
    return d->wake_fail ? NSX_SENSOR_E_IO : NSX_SENSOR_OK;
}
static const t5838_control_ops_t ops = {send_frame, wake};
static void bind_fake(fake_t *d, t5838_context_t *ctx)
{
    *d = (fake_t){0}; const t5838_control_t bus = {&ops, d};
    assert(t5838_bind(ctx, &bus) == NSX_SENSOR_OK && !d->calls);
}
typedef struct { fake_t *a, *b; uint32_t hz; unsigned changes, waits; bool fail_final; } clock_t;
static nsx_sensor_status_t clock_set(void *user, uint32_t hz)
{
    clock_t *c = user; ++c->changes;
    if (c->changes == 1) assert(hz == 100000);
    else {
        assert(c->waits == 2);
        assert(c->a->reg[0x29] == c->b->reg[0x29]);
        assert(c->a->reg[0x29] == (hz ? 1 : 8));
        if (c->fail_final) return NSX_SENSOR_E_IO;
    }
    c->hz = hz; return NSX_SENSOR_OK;
}
static void delay(void *user, uint32_t us)
{
    clock_t *c = user; assert(us == 2000 && c->hz == 100000); ++c->waits;
}
int main(void)
{
    fake_t a, b; t5838_context_t first = {0}, second = {0};
    t5838_config_t config = t5838_default_config();
    bool active = true;
    assert(t5838_enable(&first) == NSX_SENSOR_E_STATE);
    assert(t5838_encode_write(0, 0, NULL) == NSX_SENSOR_E_ARGUMENT);
    /* All payload bits independently exercised, including stop not a data bit. */
    for (unsigned i = 0; i < 256; ++i) {
        t5838_write_frame_t frame;
        bind_fake(&a, &first);
        assert(t5838_encode_write((uint8_t)i, (uint8_t)~i, &frame) == NSX_SENSOR_OK);
        assert(send_frame(&a, &frame) == NSX_SENSOR_OK);
        assert(a.reg[i] == (uint8_t)~i && a.addresses[0] == i);
    }
    bind_fake(&a, &first); bind_fake(&b, &second);
    assert(t5838_init(&first, &config) == NSX_SENSOR_OK);
    static const uint8_t order[] = {0x5c,0x3e,0x6f,0x3b,0x4c,0x29,0x35,0x36};
    assert(a.calls == sizeof(order) && !memcmp(a.addresses, order, sizeof(order)));
    assert(a.reg[0x35] == 1 && a.reg[0x36] == 4 && a.reg[0x29] == 0);
    assert(t5838_read_wake(&first, &active) == NSX_SENSOR_E_STATE && active);
    assert(t5838_enable(&first) == NSX_SENSOR_OK && a.reg[0x29] == 8);
    assert(t5838_read_wake(&first, &active) == NSX_SENSOR_OK && !active);
    a.wake = true; a.wake_fail = true;
    assert(t5838_read_wake(&first, &active) == NSX_SENSOR_E_IO && !active);
    config.mode = T5838_AAD_DIGITAL2;
    config.absolute_pulse_min = 3500; config.relative_pulse_min = 300;
    assert(t5838_init(&second, &config) == NSX_SENSOR_OK && b.calls == 16);
    assert(b.reg[0x2a] == 0 && b.reg[0x2b] == 0x16 && b.reg[0x2c] == 0x32);
    assert(b.reg[0x2d] == 0xc0 && b.reg[0x2e] == 0x2c && b.reg[0x2f] == 0xd1);
    assert(b.reg[0x30] == 0xac && b.reg[0x31] == 0x13 && b.reg[0x32] == 0x41);
    assert(b.reg[0x33] == 0x24 && b.reg[0x29] == 0 && a.reg[0x29] == 8);
    assert(t5838_enable(&second) == NSX_SENSOR_OK && b.reg[0x29] == 2);
    unsigned before = b.calls;
    config.absolute_pulse_min = 3501;
    assert(t5838_init(&second, &config) == NSX_SENSOR_E_ARGUMENT && before == b.calls);
    config.absolute_pulse_min = 3500;
    assert(t5838_disable(&second) == NSX_SENSOR_OK && b.reg[0x29] == 0);
    assert(t5838_enable(&second) == NSX_SENSOR_E_STATE);
    for (unsigned after = 0; after < 2; ++after) for (unsigned i = 1; i <= 16; ++i) {
        bind_fake(&b, &second); b.fail = i; b.after = after != 0;
        assert(t5838_init(&second, &config) == NSX_SENSOR_E_IO);
        before = b.calls;
        assert(t5838_enable(&second) == NSX_SENSOR_E_STATE && before == b.calls);
        b.fail = 0; assert(t5838_init(&second, &config) == NSX_SENSOR_OK);
    }
    config.absolute_threshold = 0x7bc; config.floor = 0x7bc;
    config.algorithm = T5838_THRESHOLD_ABSOLUTE;
    assert(t5838_init(&second, &config) == NSX_SENSOR_OK);
    assert(b.reg[0x2a] == 7 && b.reg[0x2b] == 0xbc && b.reg[0x2d] == 0x80);
    assert(b.reg[0x31] == 0xbc && b.reg[0x32] == 0x47);
    config.floor = 14; before = b.calls;
    assert(t5838_init(&second, &config) == NSX_SENSOR_E_ARGUMENT && b.calls == before);
    b.fail = b.calls + 1; b.after = true;
    assert(t5838_enable(&second) == NSX_SENSOR_E_IO);
    assert(t5838_read_wake(&second, &active) == NSX_SENSOR_E_STATE);
    t5838_invalidate(&first);
    assert(t5838_enable(&first) == NSX_SENSOR_E_STATE);
    assert(t5838_validate_operating_clock(T5838_AAD_DIGITAL1,T5838_AUDIO_LOW_POWER,750000) == NSX_SENSOR_OK);
    assert(t5838_validate_operating_clock(T5838_AAD_OFF,T5838_AUDIO_HIGH_QUALITY,2400000) == NSX_SENSOR_OK);
    assert(t5838_validate_operating_clock(T5838_AAD_OFF,T5838_AUDIO_HIGH_QUALITY,1500000) == NSX_SENSOR_E_ARGUMENT);
    assert(t5838_validate_operating_clock(T5838_AAD_ANALOG,T5838_AUDIO_SLEEP,0) == NSX_SENSOR_OK);
    assert(t5838_validate_operating_clock(T5838_AAD_DIGITAL2,T5838_AUDIO_SLEEP,100000) == NSX_SENSOR_E_ARGUMENT);
    assert(t5838_validate_operating_clock(T5838_AAD_DIGITAL1,T5838_AUDIO_SLEEP,0) == NSX_SENSOR_E_ARGUMENT);
    for (unsigned mode = 0; mode < 2; ++mode) for (unsigned fail = 0; fail < 2; ++fail) {
        bind_fake(&a, &first); bind_fake(&b, &second);
        config = t5838_default_config();
        config.mode = mode ? T5838_AAD_DIGITAL1 : T5838_AAD_ANALOG;
        clock_t clock = {.a=&a, .b=&b, .fail_final=fail != 0};
        spark_microphone_clock_t board = {&clock, clock_set, {&clock, NULL, delay}};
        assert(spark_t5838_configure_pair(&first, &second, &config, &board) ==
               (fail ? NSX_SENSOR_E_IO : NSX_SENSOR_OK));
        assert(clock.changes == 2 && clock.waits == 2);
        if (fail) assert(!first._configured && !second._configured);
        else assert(clock.hz == (mode ? 750000u : 0));
    }
    bind_fake(&a, &first); bind_fake(&b, &second);
    config = t5838_default_config(); b.fail = 1;
    clock_t clock = {.a=&a, .b=&b};
    spark_microphone_clock_t board = {&clock, clock_set, {&clock, NULL, delay}};
    assert(spark_t5838_configure_pair(&first, &second, &config, &board) == NSX_SENSOR_E_IO);
    assert(clock.changes == 1 && clock.hz == 100000);
    assert(!first._configured && !second._configured);
    return 0;
}
