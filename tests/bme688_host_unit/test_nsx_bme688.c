/* SPDX-License-Identifier: BSD-3-Clause */
#include "nsx_bme688_nsx.h"
#include "fixture.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

#define ERROR_CODE 77u

typedef struct {
    uint8_t memory[256], page;
    bme688_interface_t interface;
    unsigned operations, reads, writes, fail_at, reset_count, page_writes;
    uint64_t delayed_us;
    bool stuck_mode, fail_page_write, page_error_has_side_effect;
    uint32_t finish_after_us;
} fake_t;

static void prepare(fake_t *f, bme688_interface_t interface)
{
    memset(f, 0, sizeof(*f));
    fixture(f->memory); f->interface = interface; f->page = 0xa0;
}

static uint32_t fake_read(void *user, uint8_t wire, uint8_t *data, size_t length)
{
    fake_t *f = user;
    uint8_t reg = wire;
    assert(length > 0 && length <= 23);
    ++f->reads;
    if (++f->operations == f->fail_at) return ERROR_CODE;
    if (f->interface == BME688_SPI) {
        assert((wire & 0x80) != 0);
        reg = wire & 0x7f;
        if (reg == 0x73 && length == 1) { data[0] = f->page; return 0; }
        if ((f->page & 0x10) == 0) reg |= 0x80;
    }
    assert((unsigned)reg + length <= 256);
    memcpy(data, f->memory + reg, length);
    /* Reading a complete field consumes its new-data indication. */
    if (reg == 0x1d && length == 17) f->memory[0x1d] &= 0x7f;
    return 0;
}

static uint32_t fake_write(void *user, uint8_t wire, const uint8_t *data, size_t length)
{
    fake_t *f = user;
    uint8_t reg = wire;
    assert(length == 1); /* Both buses require address/value pairs, never autoincrement writes. */
    ++f->writes;
    if (++f->operations == f->fail_at) return ERROR_CODE;
    if (f->interface == BME688_SPI) {
        assert((wire & 0x80) == 0);
        if (wire == 0x73) {
            assert((data[0] & ~0x10) == (f->page & ~0x10));
            ++f->page_writes;
            if (f->fail_page_write) {
                f->fail_page_write = false;
                if (f->page_error_has_side_effect) f->page = data[0];
                return ERROR_CODE;
            }
            f->page = data[0]; return 0;
        }
        if ((f->page & 0x10) == 0) reg |= 0x80;
    }
    if (reg == 0xe0) {
        assert(data[0] == 0xb6); ++f->reset_count;
        /* Model reset configuration, but preserve factory calibration/identity. */
        memset(f->memory + 0x50, 0, 0x29); f->page = 0xa0; return 0;
    }
    if (reg == 0x74 && (data[0] & 3) == 0 &&
        (f->stuck_mode || f->finish_after_us != 0)) return 0;
    if (reg != 0x74) assert((f->memory[0x74] & 3) == 0);
    f->memory[reg] = data[0];
    return 0;
}

static void fake_delay(void *user, uint32_t duration)
{
    fake_t *f = user;
    f->delayed_us += duration;
    if (f->finish_after_us != 0) {
        if (duration >= f->finish_after_us) {
            f->finish_after_us = 0;
            f->memory[0x74] &= (uint8_t)~3;
            raw_field(f->memory, 519888, 415148, 35000, 512, 5, (uint8_t)(f->memory[0x1e] + 1));
        } else f->finish_after_us -= duration;
    }
}

static bme688_status_t initialize(bme688_context_t *ctx, fake_t *f)
{
    bme688_transport_t transport = { fake_read, fake_write, fake_delay, f, f->interface };
    return bme688_init(ctx, &transport);
}

static bme688_heater_t gas_heater(void)
{
    bme688_heater_t result = { true, 320, 100, 25.0f };
    return result;
}

static void finish(fake_t *f, uint8_t index)
{
    raw_field(f->memory, 519888, 415148, 35000, 512, 5, index);
    f->memory[0x74] &= (uint8_t)~3;
}

static void close_float(float value, float expected)
{
    float tolerance = fmaxf(fabsf(expected) * 0.000002f, 0.00001f);
    assert(fabsf(value - expected) <= tolerance);
}

static void test_configuration_and_protocol(bme688_interface_t interface)
{
    fake_t f;
    bme688_context_t ctx;
    bme688_config_t config;
    bme688_heater_state_t actual;
    bme688_heater_t heater = gas_heater();
    uint8_t chip = 0, variant = 0;
    prepare(&f, interface);
    assert(initialize(&ctx, &f) == BME688_OK);
    assert(f.reset_count == 1 && f.delayed_us == 10000);
    assert(bme688_get_id(&ctx, &chip, &variant) == BME688_OK && chip == 0x61 && variant == 1);
    assert(bme688_get_config(&ctx, &config) == BME688_OK);
    assert(config.temperature == BME688_OS_2X && config.pressure == BME688_OS_16X &&
           config.humidity == BME688_OS_1X && config.filter == BME688_FILTER_3);
    assert(f.memory[0x72] == 1 && f.memory[0x74] == 0x54 && f.memory[0x75] == 8);
    assert(bme688_get_heater(&ctx, &actual) == BME688_OK && !actual.enabled);
    assert(bme688_set_heater(&ctx, &heater) == BME688_OK);
    assert(f.memory[0x5a] == 145); /* Bosch v4.4.8 oracle, synthetic fixture. */
    assert(f.memory[0x64] == 0x59); /* Datasheet rev1.3 sec 3.3.1: 100 ms -> 0x59. */
    assert(f.memory[0x71] == 0x20 && (f.memory[0x70] & 8) == 0);
    assert(bme688_get_heater(&ctx, &actual) == BME688_OK && actual.enabled &&
           actual.resistance_code == 145 && actual.duration_ms == 100);
    f.memory[0x72] |= 0xf8; f.memory[0x75] |= 0xe3;
    config.humidity = BME688_OS_4X; config.filter = BME688_FILTER_7;
    assert(bme688_set_config(&ctx, &config) == BME688_OK);
    assert(f.memory[0x72] == 0xfb && f.memory[0x75] == 0xef);
    if (interface == BME688_SPI) assert(f.page_writes >= 3 && (f.page & ~0x10) == 0xa0);
    assert(bme688_reset(&ctx) == BME688_OK && f.reset_count == 2);
    assert(bme688_get_heater(&ctx, &actual) == BME688_OK && !actual.enabled);
}

static void test_golden_compensation(void)
{
    /* Captured from the unmodified Bosch public APIs by generate_bosch_oracle.c
     * at 80ea120a8b8ac987d7d79eb68a9ed796736be845. Outputs are fixed, not
     * computed using NSX helper formulas. Inputs are synthetic, not HIL data.
     */
    static const float expected[][4] = {
        { 25.1583958f, 78683.8438f, 66.0606384f, 2000000.0f },
        { 3.13118315f, 61305.0117f, 0.0f, 102400000.0f },
        { 50.4145508f, 103514.922f, 100.0f, 1421.21155f }
    };
    const uint32_t temp[] = { 519888, 450000, 600000 }, pressure[] = { 415148, 500000, 300000 };
    const uint16_t humidity[] = { 35000, 1000, 65535 }, gas[] = { 512, 0, 1023 };
    const uint8_t range[] = { 5, 0, 15 };
    fake_t f;
    bme688_context_t ctx;
    bme688_heater_t heater = gas_heater();
    bme688_sample_t sample;
    uint32_t wait;
    prepare(&f, BME688_I2C);
    assert(initialize(&ctx, &f) == BME688_OK);
    assert(bme688_set_heater(&ctx, &heater) == BME688_OK);
    for (unsigned i = 0; i < 3; ++i) {
        assert(bme688_start(&ctx, &wait) == BME688_OK);
        assert(wait == 143590); /* 19 cycles, switching/wakeup, 100ms heater, 1ms guard. */
        raw_field(f.memory, temp[i], pressure[i], humidity[i], gas[i], range[i], (uint8_t)(i + 1));
        f.memory[0x74] &= (uint8_t)~3;
        assert(bme688_collect(&ctx, &sample) == BME688_OK);
        close_float(sample.temperature_c, expected[i][0]); close_float(sample.pressure_pa, expected[i][1]);
        close_float(sample.humidity_percent, expected[i][2]); close_float(sample.gas_resistance_ohm, expected[i][3]);
        assert(sample.pressure_valid && sample.humidity_valid && sample.gas_valid && sample.heater_stable);
    }
    /* Shared humidity nibble extraction and sign extension are exercised by
     * this fixture, including negative P/H/GH coefficients. */
    assert(ctx._calibration.par_h1 == 75 && ctx._calibration.par_h2 == 362);
    assert(ctx._calibration.par_p2 == -10685 && ctx._calibration.par_h7 == -100);
    const uint16_t targets[] = { 0, 200, 400 };
    const float ambients[] = { -40, 25, 85 };
    const uint8_t heat_codes[] = { 46, 108, 170 };
    for (unsigned i = 0; i < 3; ++i) {
        heater.temperature_c = targets[i]; heater.ambient_temperature_c = ambients[i];
        assert(bme688_set_heater(&ctx, &heater) == BME688_OK && f.memory[0x5a] == heat_codes[i]);
    }
}

static void test_collection_state_and_channels(void)
{
    fake_t f;
    bme688_context_t ctx;
    bme688_sample_t sample = { .temperature_c = 123.0f };
    bme688_config_t config = { BME688_OS_1X, BME688_OS_OFF, BME688_OS_OFF, BME688_FILTER_OFF };
    uint32_t wait;
    prepare(&f, BME688_SPI);
    assert(initialize(&ctx, &f) == BME688_OK);
    assert(bme688_collect(&ctx, &sample) == BME688_E_STATE);
    assert(bme688_set_config(&ctx, &config) == BME688_OK);
    finish(&f, 255);
    assert(bme688_start(&ctx, &wait) == BME688_OK && wait == 8256);
    uint64_t delayed = f.delayed_us;
    assert(bme688_start(&ctx, &wait) == BME688_E_BUSY);
    assert(bme688_collect(&ctx, &sample) == BME688_AGAIN && sample.temperature_c == 123.0f);
    assert(f.delayed_us == delayed);
    finish(&f, 0); /* Measurement-index wrap 255 -> 0 must be fresh. */
    f.memory[0x1d] &= 0x7f;
    assert(bme688_collect(&ctx, &sample) == BME688_AGAIN);
    f.memory[0x1d] = 0xc0; /* Gas conversion still busy. */
    assert(bme688_collect(&ctx, &sample) == BME688_AGAIN);
    f.memory[0x1d] = 0x80;
    assert(bme688_collect(&ctx, &sample) == BME688_OK);
    assert(sample.measurement_index == 0 && !sample.pressure_valid && !sample.humidity_valid);
    assert(sample.pressure_pa == 0 && sample.humidity_percent == 0 && sample.gas_resistance_ohm == 0);
    assert(!sample.gas_valid && !sample.heater_stable);
    assert(bme688_collect(&ctx, &sample) == BME688_E_STATE);
    bme688_heater_t heater = gas_heater();
    assert(bme688_set_heater(&ctx, &heater) == BME688_OK);
    assert(bme688_start(&ctx, &wait) == BME688_OK);
    finish(&f, 1); f.memory[0x2d] &= (uint8_t)~0x20;
    assert(bme688_collect(&ctx, &sample) == BME688_OK && !sample.gas_valid && sample.heater_stable);
    assert(sample.gas_resistance_ohm == 0);
    assert(bme688_start(&ctx, &wait) == BME688_OK);
    finish(&f, 2); f.memory[0x2d] &= (uint8_t)~0x10;
    assert(bme688_collect(&ctx, &sample) == BME688_OK && sample.gas_valid && !sample.heater_stable);
}

static void test_forced_mode_repeated_index(bme688_interface_t interface)
{
    fake_t f;
    bme688_context_t ctx;
    bme688_sample_t sample = { .temperature_c = 123.0f };
    uint32_t wait;
    prepare(&f, interface);
    assert(initialize(&ctx, &f) == BME688_OK);
    /* Forced mode does not promise index advancement. Exercise zero after
     * reset and repeated zero/nonzero indices across successive triggers. */
    const uint8_t indices[] = { 0, 0, 7, 7 };
    for (unsigned i = 0; i < sizeof(indices); ++i) {
        finish(&f, indices[i]); /* Unread data from before this trigger. */
        assert(bme688_start(&ctx, &wait) == BME688_OK);
        float previous = sample.temperature_c;
        assert(bme688_collect(&ctx, &sample) == BME688_AGAIN);
        assert(sample.temperature_c == previous);
        finish(&f, indices[i]); /* A new completed conversion, same index. */
        assert(bme688_collect(&ctx, &sample) == BME688_OK);
        assert(sample.measurement_index == indices[i]);
        close_float(sample.temperature_c, 25.1583958f);
        assert(bme688_collect(&ctx, &sample) == BME688_E_STATE);
    }
}

static void test_bounded_wait_and_recovery(void)
{
    fake_t f;
    bme688_context_t ctx;
    bme688_sample_t sample = { .temperature_c = 123.0f };
    bme688_config_t config;
    uint32_t wait;
    prepare(&f, BME688_I2C);
    assert(initialize(&ctx, &f) == BME688_OK);
    assert(bme688_get_config(&ctx, &config) == BME688_OK);
    assert(bme688_start(&ctx, &wait) == BME688_OK);
    f.stuck_mode = true;
    uint64_t before = f.delayed_us;
    assert(bme688_sleep(&ctx, 2501) == BME688_E_TIMEOUT);
    assert(f.delayed_us - before == 2501);
    assert(bme688_collect(&ctx, &sample) == BME688_E_STATE);
    before = f.delayed_us;
    assert(bme688_set_config(&ctx, &config) == BME688_E_TIMEOUT);
    assert(f.delayed_us - before == 5000000);
    f.stuck_mode = false; f.finish_after_us = 2001;
    assert(bme688_set_config(&ctx, &config) == BME688_OK);
    before = f.delayed_us;
    assert(bme688_measure(&ctx, &sample, 2501) == BME688_E_TIMEOUT);
    assert(f.delayed_us - before == 2501 && sample.temperature_c == 123.0f);
    assert(bme688_start(&ctx, &wait) == BME688_E_BUSY);
    finish(&f, (uint8_t)(f.memory[0x1e] + 1));
    assert(bme688_collect(&ctx, &sample) == BME688_OK);
    f.finish_after_us = 1000;
    assert(bme688_measure(&ctx, &sample, 100000) == BME688_OK);
    assert(bme688_sleep(&ctx, 0) == BME688_OK);
}

static void test_failures(bme688_interface_t interface)
{
    fake_t f;
    bme688_context_t ctx;
    prepare(&f, interface);
    assert(initialize(&ctx, &f) == BME688_OK);
    unsigned operations = f.operations;
    /* Every initialization transfer must propagate a failure, including reset,
     * all calibration banks, page writes, and each default setting. */
    for (unsigned n = 1; n <= operations; ++n) {
        prepare(&f, interface); f.fail_at = n;
        assert(initialize(&ctx, &f) == BME688_E_IO);
        assert(!ctx._initialized && ctx.transport_status == ERROR_CODE);
    }
    prepare(&f, interface);
    assert(initialize(&ctx, &f) == BME688_OK);
    uint8_t chip = 9, variant = 9;
    f.fail_at = f.operations + (interface == BME688_SPI ? 2 : 1);
    assert(bme688_get_id(&ctx, &chip, &variant) == BME688_E_IO && chip == 9 && variant == 9);
    uint32_t wait;
    f.fail_at = 0;
    assert(bme688_start(&ctx, &wait) == BME688_OK);
    finish(&f, 1);
    bme688_sample_t sample = { .temperature_c = 999 };
    f.fail_at = f.operations + 1;
    assert(bme688_collect(&ctx, &sample) == BME688_E_IO && sample.temperature_c == 999);
    assert(ctx.transport_status == ERROR_CODE);
    f.fail_at = 0;
    assert(bme688_collect(&ctx, &sample) == BME688_OK);
    /* Partial configuration must not leave a usable stale software cache. */
    bme688_config_t config;
    assert(bme688_get_config(&ctx, &config) == BME688_OK);
    unsigned start = f.operations;
    f.fail_at = start + (interface == BME688_SPI ? 6 : 3); /* ctrl_hum write. */
    assert(bme688_set_config(&ctx, &config) == BME688_E_IO);
    assert(bme688_start(&ctx, &wait) == BME688_E_STATE);
    f.fail_at = 0;
    assert(bme688_set_config(&ctx, &config) == BME688_OK);
    bme688_heater_t heater = gas_heater();
    f.fail_at = f.operations + (interface == BME688_SPI ? 4 : 2); /* resistance write. */
    assert(bme688_set_heater(&ctx, &heater) == BME688_E_IO);
    assert(bme688_start(&ctx, &wait) == BME688_E_STATE);
    f.fail_at = 0;
    assert(bme688_set_heater(&ctx, &heater) == BME688_OK);
}

static void test_page_write_retry(void)
{
    for (unsigned side_effect = 0; side_effect < 2; ++side_effect) {
        fake_t f;
        bme688_context_t ctx;
        bme688_config_t config;
        prepare(&f, BME688_SPI);
        assert(initialize(&ctx, &f) == BME688_OK);
        uint8_t chip, variant;
        assert(bme688_get_id(&ctx, &chip, &variant) == BME688_OK); /* Leaves high page selected. */
        f.fail_page_write = true; f.page_error_has_side_effect = side_effect != 0;
        assert(bme688_get_config(&ctx, &config) == BME688_E_IO);
        assert(bme688_get_config(&ctx, &config) == BME688_OK);
        assert(config.temperature == BME688_OS_2X);
    }
}

static void test_validation_and_heater_boundaries(void)
{
    fake_t f;
    bme688_context_t ctx;
    bme688_config_t config = { BME688_OS_OFF, BME688_OS_1X, BME688_OS_1X, BME688_FILTER_OFF };
    prepare(&f, BME688_I2C);
    assert(initialize(&ctx, &f) == BME688_OK);
    unsigned before = f.operations;
    assert(bme688_set_config(&ctx, &config) == BME688_E_ARGUMENT);
    config.temperature = (bme688_oversampling_t)-1;
    assert(bme688_set_config(&ctx, &config) == BME688_E_ARGUMENT);
    config.temperature = BME688_OS_1X; config.pressure = (bme688_oversampling_t)6;
    assert(bme688_set_config(&ctx, &config) == BME688_E_ARGUMENT);
    bme688_heater_t heater = gas_heater();
    heater.temperature_c = 401; assert(bme688_set_heater(&ctx, &heater) == BME688_E_ARGUMENT);
    heater = gas_heater(); heater.duration_ms = 0; assert(bme688_set_heater(&ctx, &heater) == BME688_E_ARGUMENT);
    heater.duration_ms = 4033; assert(bme688_set_heater(&ctx, &heater) == BME688_E_ARGUMENT);
    heater = gas_heater(); heater.ambient_temperature_c = NAN;
    assert(bme688_set_heater(&ctx, &heater) == BME688_E_ARGUMENT);
    heater.ambient_temperature_c = 86; assert(bme688_set_heater(&ctx, &heater) == BME688_E_ARGUMENT);
    assert(bme688_set_heater(&ctx, NULL) == BME688_E_ARGUMENT);
    assert(f.operations == before);
    const uint16_t durations[] = { 1, 63, 64, 65, 100, 4032 };
    const uint8_t codes[] = { 1, 63, 0x50, 0x50, 0x59, 0xff };
    const uint16_t rounded[] = { 1, 63, 64, 64, 100, 4032 };
    heater = gas_heater();
    for (unsigned i = 0; i < 6; ++i) {
        heater.duration_ms = durations[i];
        assert(bme688_set_heater(&ctx, &heater) == BME688_OK && f.memory[0x64] == codes[i]);
        bme688_heater_state_t actual;
        assert(bme688_get_heater(&ctx, &actual) == BME688_OK && actual.duration_ms == rounded[i]);
    }
    uint32_t wait;
    assert(bme688_start(&ctx, &wait) == BME688_OK && wait < 4200000);
    prepare(&f, BME688_I2C); f.memory[0xd0] = 0;
    assert(initialize(&ctx, &f) == BME688_E_DEVICE);
    prepare(&f, BME688_I2C); f.memory[0xf0] = 0;
    assert(initialize(&ctx, &f) == BME688_E_DEVICE);
    prepare(&f, BME688_I2C); memset(f.memory + 0x8a, 0, 23);
    assert(initialize(&ctx, &f) == BME688_E_CALIBRATION);
    prepare(&f, BME688_I2C); memset(f.memory + 0x8a, 0xff, 23);
    assert(initialize(&ctx, &f) == BME688_E_CALIBRATION);
    assert(bme688_init(NULL, NULL) == BME688_E_ARGUMENT);
    assert(bme688_reset(NULL) == BME688_E_ARGUMENT);
    assert(bme688_get_config(NULL, &config) == BME688_E_STATE);
    bme688_transport_t bad = { 0 };
    assert(bme688_init(&ctx, &bad) == BME688_E_ARGUMENT);
}

/* Actual NSX adapters are linked into this executable. These stand-ins verify
 * prefixes, address/CS routing, command lengths, and HAL-compatible alignment.
 */
static void assert_aligned(const void *p) { assert((uintptr_t)p % _Alignof(uint32_t) == 0); }
uint32_t nsx_i2c_write_read(nsx_i2c_config_t *bus, uint16_t addr, const void *tx, size_t tx_len,
                            void *rx, size_t rx_len)
{
    assert(addr == 0x76 || addr == 0x77); assert(tx_len == 1);
    assert_aligned(tx); assert_aligned(rx);
    return fake_read(bus->test_device, ((const uint8_t *)tx)[0], rx, rx_len);
}
uint32_t nsx_i2c_write(nsx_i2c_config_t *bus, const void *tx, uint32_t length, uint16_t addr)
{
    assert(addr == 0x76 || addr == 0x77); assert(length == 2); assert_aligned(tx);
    return fake_write(bus->test_device, ((const uint8_t *)tx)[0], (const uint8_t *)tx + 1, 1);
}
uint32_t nsx_spi_read(nsx_spi_config_t *bus, const void *rx, uint32_t length, uint64_t reg,
                       uint32_t reg_len, uint32_t cs)
{
    assert(cs == 7 && reg_len == 1 && reg <= 255); assert_aligned(rx);
    return fake_read(bus->test_device, (uint8_t)reg, (uint8_t *)rx, length);
}
uint32_t nsx_spi_write(nsx_spi_config_t *bus, const void *tx, uint32_t length, uint64_t reg,
                        uint32_t reg_len, uint32_t cs)
{
    assert(cs == 7 && reg_len == 1 && reg <= 255); assert_aligned(tx);
    return fake_write(bus->test_device, (uint8_t)reg, tx, length);
}

static void test_nsx_adapters(void)
{
    fake_t f;
    bme688_context_t ctx;
    bme688_sample_t sample;
    uint32_t wait;
    nsx_i2c_config_t i2c = { &f };
    nsx_spi_config_t spi = { &f };
    bme688_nsx_i2c_t i2c_binding = { &i2c, 0x76, fake_delay, &f, 0 };
    bme688_nsx_spi_t spi_binding = { &spi, 7, fake_delay, &f, 0 };
    for (unsigned address = 0x76; address <= 0x77; ++address) {
        prepare(&f, BME688_I2C); i2c_binding.address = (uint16_t)address;
        assert(bme688_init_i2c(&ctx, &i2c_binding) == BME688_OK);
        assert(bme688_start(&ctx, &wait) == BME688_OK); finish(&f, 1);
        assert(bme688_collect(&ctx, &sample) == BME688_OK);
        close_float(sample.temperature_c, 25.1583958f);
        f.fail_at = f.operations + 1;
        assert(bme688_reset(&ctx) == BME688_E_IO && i2c_binding.transport_status == ERROR_CODE);
    }
    prepare(&f, BME688_SPI);
    assert(bme688_init_spi(&ctx, &spi_binding) == BME688_OK);
    assert(bme688_start(&ctx, &wait) == BME688_OK); finish(&f, 1);
    assert(bme688_collect(&ctx, &sample) == BME688_OK && f.page_writes > 0);
    f.fail_at = f.operations + 1;
    assert(bme688_reset(&ctx) == BME688_E_IO && spi_binding.transport_status == ERROR_CODE);
    i2c_binding.address = 0x78;
    assert(bme688_init_i2c(&ctx, &i2c_binding) == BME688_E_ARGUMENT);
    assert(bme688_init_i2c(&ctx, NULL) == BME688_E_ARGUMENT);
    assert(bme688_init_spi(&ctx, NULL) == BME688_E_ARGUMENT);
    spi_binding.bus = NULL;
    assert(bme688_init_spi(&ctx, &spi_binding) == BME688_E_ARGUMENT);
}

static void test_multiple_devices(void)
{
    fake_t a, b;
    bme688_context_t ca, cb;
    uint32_t wa, wb;
    bme688_sample_t sample;
    prepare(&a, BME688_SPI); prepare(&b, BME688_SPI);
    assert(initialize(&ca, &a) == BME688_OK && initialize(&cb, &b) == BME688_OK);
    bme688_heater_t heater = gas_heater();
    assert(bme688_set_heater(&ca, &heater) == BME688_OK);
    assert(bme688_start(&ca, &wa) == BME688_OK && bme688_start(&cb, &wb) == BME688_OK && wa > wb);
    finish(&a, 1); finish(&b, 1);
    assert(bme688_collect(&cb, &sample) == BME688_OK && !sample.gas_valid);
    assert(bme688_collect(&ca, &sample) == BME688_OK && sample.gas_valid);
}

int main(void)
{
    test_configuration_and_protocol(BME688_I2C); test_configuration_and_protocol(BME688_SPI);
    test_forced_mode_repeated_index(BME688_I2C); test_forced_mode_repeated_index(BME688_SPI);
    test_golden_compensation(); test_collection_state_and_channels(); test_bounded_wait_and_recovery();
    test_failures(BME688_I2C); test_failures(BME688_SPI); test_page_write_retry();
    test_validation_and_heater_boundaries(); test_nsx_adapters(); test_multiple_devices();
    puts("BME688 host tests passed (synthetic data; hardware unqualified)");
    return 0;
}
