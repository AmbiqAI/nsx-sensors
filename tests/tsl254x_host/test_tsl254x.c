/* SPDX-License-Identifier: BSD-3-Clause */
#include <assert.h>
#include <string.h>
#include <nsx/sensors/tsl254x.h>
typedef struct { uint8_t reg[256]; unsigned calls, fail; bool after; } fake_t;
static nsx_sensor_status_t transfer(void *user, const nsx_sensor_transfer_t *t)
{
    fake_t *f = user;
    bool fail = ++f->calls == f->fail;
    if (fail && !f->after) return NSX_SENSOR_E_IO;
    assert(t->tx && t->tx_size);
    uint8_t r = t->tx[0];
    if (t->rx_size) {
        assert(t->tx_size == 1);
        memcpy(t->rx, &f->reg[r], t->rx_size);
    } else {
        assert(t->tx_size == 2);
        if (r == 0x93) f->reg[r] &= (uint8_t)~t->tx[1];
        else f->reg[r] = t->tx[1];
    }
    return fail ? NSX_SENSOR_E_IO : NSX_SENSOR_OK;
}
static const nsx_sensor_bus_ops_t ops = {transfer};
static void bind_fake(fake_t *f, tsl254x_context_t *c, tsl254x_variant_t variant)
{
    *f = (fake_t){0}; f->reg[0x92] = 0xe4; f->reg[0x91] = 0x61;
    f->reg[0x9e] = variant == TSL254X_TSL2541 ? 2 : 1;
    f->reg[0xab] = 0xdc; f->reg[0x90] = 0xf0; f->reg[0x9f] = 0xeb;
    const nsx_sensor_bus_t bus = {&ops, f, NSX_SENSOR_BUS_I2C, 2, 4};
    assert(tsl254x_bind(c, &bus, variant) == NSX_SENSOR_OK);
    assert(f->calls == 0);
}
int main(void)
{
    fake_t f, second; tsl254x_context_t c = {0}, other = {0};
    tsl254x_config_t cfg = tsl254x_default_config();
    tsl254x_sample_t sample = {.visible = 42}; float scale = 42;
    assert(tsl254x_get_visible_scale(&c, &scale) == NSX_SENSOR_E_STATE && scale == 42);
    bind_fake(&f, &c, TSL254X_TSL2540);
    cfg.integration_cycles = 0;
    assert(tsl254x_init(&c, &cfg) == NSX_SENSOR_E_ARGUMENT && !f.calls);
    cfg = tsl254x_default_config();
    f.reg[0x9e] = 2;
    tsl254x_identity_t id = {1,2,3};
    assert(tsl254x_probe(&c, &id) == NSX_SENSOR_E_DEVICE && id.id == 1);
    f.reg[0x9e] = 1;
    assert(tsl254x_init(&c, &cfg) == NSX_SENSOR_OK);
    assert(f.reg[0x80] == 0 && f.reg[0x81] == 35 && f.reg[0xab] == 0x4c);
    assert(f.reg[0x90] == 0xf0 && f.reg[0x9f] == 0xef);
    assert(tsl254x_get_visible_scale(&c, &scale) == NSX_SENSOR_OK && scale == 1);
    assert(tsl254x_read_sample(&c, &sample) == NSX_SENSOR_E_STATE && sample.visible == 42);
    assert(tsl254x_start(&c) == NSX_SENSOR_OK && f.reg[0x80] == 3);
    assert(tsl254x_start(&c) == NSX_SENSOR_E_BUSY);
    f.reg[0x94] = 0x34; f.reg[0x95] = 0x12; f.reg[0x96] = 0xcd; f.reg[0x97] = 0xab;
    f.reg[0x93] = 0x98;
    assert(tsl254x_read_sample(&c, &sample) == NSX_SENSOR_OK);
    assert(sample.visible == 0x1234 && sample.infrared == 0xabcd && sample.analog_saturated);
    assert(sample.digital_saturated && sample.threshold_interrupt && sample.calibration_complete);
    assert(f.reg[0x93] == 0x98);
    assert(tsl254x_clear_status(&c, true, false, false) == NSX_SENSOR_OK && f.reg[0x93] == 0x18);
    tsl254x_sample_t saved = sample;
    f.fail = f.calls + 2; f.after = true;
    assert(tsl254x_read_sample(&c, &sample) == NSX_SENSOR_E_IO);
    assert(memcmp(&saved, &sample, sizeof(sample)) == 0);
    f.fail = 0;
    assert(tsl254x_stop(&c) == NSX_SENSOR_OK && f.reg[0x80] == 0);
    static const uint8_t gain1[] = {0,0,1,2,3,3}, gain2[] = {0,4,4,4,4,0x14};
    for (unsigned i = 0; i < 6; ++i) {
        cfg.gain = (tsl254x_gain_t)i; cfg.integration_cycles = 256;
        assert(tsl254x_init(&c, &cfg) == NSX_SENSOR_OK);
        assert((f.reg[0x90] & 3) == gain1[i] && (f.reg[0x9f] & 0x14) == gain2[i]);
        assert(f.reg[0x81] == 255);
    }
    cfg.integration_cycles = 1;
    cfg.low_threshold = 0x1234; cfg.high_threshold = 0xabcd;
    cfg.persistence = 15; cfg.threshold_interrupt = true; cfg.saturation_interrupt = true;
    assert(tsl254x_init(&c, &cfg) == NSX_SENSOR_OK);
    assert(f.reg[0x84] == 0x34 && f.reg[0x85] == 0x12 && f.reg[0x86] == 0xcd && f.reg[0x87] == 0xab);
    assert((f.reg[0x8c] & 15) == 15 && f.reg[0xdd] == 0x90);
    assert(tsl254x_start(&c) == NSX_SENSOR_OK);
    f.reg[0x94] = 0xfe; f.reg[0x95] = 3; f.reg[0x96] = 0; f.reg[0x97] = 0;
    assert(tsl254x_read_sample(&c, &sample) == NSX_SENSOR_OK && !sample.digital_saturated);
    f.reg[0x94] = 0xff;
    assert(tsl254x_read_sample(&c, &sample) == NSX_SENSOR_OK && sample.digital_saturated);
    cfg.gain = (tsl254x_gain_t)-1;
    unsigned before = f.calls;
    assert(tsl254x_init(&c, &cfg) == NSX_SENSOR_E_ARGUMENT && f.calls == before);
    cfg = tsl254x_default_config();
    bind_fake(&second, &other, TSL254X_TSL2541); second.reg[0xe6] = 0x94;
    assert(tsl254x_init(&other, &cfg) == NSX_SENSOR_OK);
    assert(tsl254x_get_visible_scale(&other, &scale) == NSX_SENSOR_OK && scale > .799f && scale < .801f);
    assert(tsl254x_get_visible_scale(&c, &scale) == NSX_SENSOR_OK && scale == 1);
    second.reg[0xe6] = 0xe4;
    assert(tsl254x_init(&other, &cfg) == NSX_SENSOR_E_CALIBRATION);
    /* Every init transfer can fail before or after side effects. No partial
     * configuration may subsequently acquire; a complete init recovers. */
    bind_fake(&f, &c, TSL254X_TSL2541);
    assert(tsl254x_init(&c, &cfg) == NSX_SENSOR_OK);
    unsigned count = f.calls;
    for (unsigned after = 0; after < 2; ++after) for (unsigned i = 1; i <= count; ++i) {
        bind_fake(&f, &c, TSL254X_TSL2541); f.fail = i; f.after = after != 0;
        assert(tsl254x_init(&c, &cfg) == NSX_SENSOR_E_IO);
        unsigned calls = f.calls;
        assert(tsl254x_start(&c) == NSX_SENSOR_E_STATE && f.calls == calls);
        f.fail = 0; assert(tsl254x_init(&c, &cfg) == NSX_SENSOR_OK);
    }
    f.fail = f.calls + 2; f.after = true;
    assert(tsl254x_start(&c) == NSX_SENSOR_E_IO);
    assert(tsl254x_stop(&c) == NSX_SENSOR_E_STATE);
    return 0;
}
