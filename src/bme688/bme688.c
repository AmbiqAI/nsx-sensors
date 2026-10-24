/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 Ambiq Micro, Inc.
 * Timing arithmetic adapted from Bosch BME68x Sensor API v4.4.8,
 * Copyright (c) 2023 Bosch Sensortec GmbH. All rights reserved.
 * See licenses/Bosch-BME68x-BSD-3-Clause.txt and PROVENANCE.md.
 * Register protocol: Bosch BME688 datasheet rev 1.3, February 2024.
 */
#include "private.h"
#include <math.h>
#include <string.h>

#define REG_CHIP_ID 0xd0
#define REG_VARIANT_ID 0xf0
#define REG_RESET 0xe0
#define REG_PAGE 0x73
#define REG_CTRL_GAS0 0x70
#define REG_CTRL_GAS1 0x71
#define REG_CTRL_HUM 0x72
#define REG_CTRL_MEAS 0x74
#define REG_CONFIG 0x75
#define REG_RES_HEAT0 0x5a
#define REG_GAS_WAIT0 0x64
#define REG_FIELD0 0x1d
#define SLEEP_TIMEOUT_US 5000000u
#define POLL_US 1000u

static bool ready(const bme688_context_t *ctx)
{
    return ctx != NULL && ctx->_initialized;
}

/* Monotonic deadline when supplied; otherwise bound requested delay time.
 * Neither strategy can preempt the endpoint's synchronous transfer. */
typedef struct { uint64_t start; uint32_t limit, requested; } wait_budget_t;
static wait_budget_t wait_begin(bme688_context_t *ctx, uint32_t limit)
{
    wait_budget_t budget = { ctx->runtime.now_us ? ctx->runtime.now_us(ctx->runtime.user) : 0,
                             limit, 0 };
    return budget;
}
static uint32_t wait_remaining(bme688_context_t *ctx, const wait_budget_t *budget)
{
    uint64_t elapsed = ctx->runtime.now_us ?
        ctx->runtime.now_us(ctx->runtime.user) - budget->start : budget->requested;
    return elapsed >= budget->limit ? 0 : budget->limit - (uint32_t)elapsed;
}
static void wait_delay(bme688_context_t *ctx, wait_budget_t *budget, uint32_t delay)
{
    ctx->runtime.delay_us(ctx->runtime.user, delay);
    budget->requested += delay;
}

static nsx_sensor_status_t wire_read(bme688_context_t *ctx, uint8_t reg,
                                     uint8_t *data, size_t length)
{
    nsx_sensor_transfer_t transfer = { &reg, 1, data, length };
    return nsx_sensor_bus_transfer(&ctx->bus, &transfer);
}
static nsx_sensor_status_t wire_write(bme688_context_t *ctx, uint8_t reg, uint8_t value)
{
    uint8_t bytes[] = { reg, value };
    nsx_sensor_transfer_t transfer = { bytes, sizeof(bytes), NULL, 0 };
    return nsx_sensor_bus_transfer(&ctx->bus, &transfer);
}

/* Always read the page selector rather than cache it. A failed write may have
 * reached silicon; the next operation re-reads actual state and can recover.
 * Page register is accessible on either page. Logical >0x7f uses page bit 0.
 */
static nsx_sensor_status_t select_page(bme688_context_t *ctx, uint8_t reg)
{
    uint8_t page, desired = reg > 0x7f ? 0 : 0x10;
    nsx_sensor_status_t status;
    if (ctx->bus.kind != NSX_SENSOR_BUS_SPI) return NSX_SENSOR_OK;
    status = wire_read(ctx, REG_PAGE | 0x80, &page, 1);
    if (status != NSX_SENSOR_OK) return status;
    if ((page & 0x10) == desired) return NSX_SENSOR_OK;
    page = (uint8_t)((page & ~0x10) | desired);
    return wire_write(ctx, REG_PAGE, page);
}

static nsx_sensor_status_t read_reg(bme688_context_t *ctx, uint8_t reg, uint8_t *data, size_t length)
{
    nsx_sensor_status_t status = select_page(ctx, reg);
    if (status != NSX_SENSOR_OK) return status;
    if (ctx->bus.kind == NSX_SENSOR_BUS_SPI) reg |= 0x80;
    return wire_read(ctx, reg, data, length);
}

/* Single register transactions deliberately avoid the part's unusual
 * interleaved address/data burst-write format on both I2C and SPI.
 */
static nsx_sensor_status_t write_reg(bme688_context_t *ctx, uint8_t reg, uint8_t value)
{
    nsx_sensor_status_t status = select_page(ctx, reg);
    if (status != NSX_SENSOR_OK) return status;
    if (ctx->bus.kind == NSX_SENSOR_BUS_SPI) reg &= 0x7f;
    return wire_write(ctx, reg, value);
}

static nsx_sensor_status_t update_reg(bme688_context_t *ctx, uint8_t reg, uint8_t mask, uint8_t value)
{
    uint8_t old;
    nsx_sensor_status_t status = read_reg(ctx, reg, &old, 1);
    if (status != NSX_SENSOR_OK) return status;
    return write_reg(ctx, reg, (uint8_t)((old & ~mask) | (value & mask)));
}

static bool valid_config(const bme688_config_t *c)
{
    return c != NULL && (unsigned)c->temperature >= BME688_OS_1X && (unsigned)c->temperature <= BME688_OS_16X &&
           (unsigned)c->pressure <= BME688_OS_16X &&
           (unsigned)c->humidity <= BME688_OS_16X &&
           (unsigned)c->filter <= BME688_FILTER_127;
}

static uint32_t measurement_time(const bme688_config_t *c)
{
    /* Bosch BME68x API v4.4.8 forced-mode estimate, in microseconds. */
    static const uint8_t cycles[] = { 0, 1, 2, 4, 8, 16 };
    uint32_t count = (uint32_t)cycles[c->temperature] + cycles[c->pressure] + cycles[c->humidity];
    return count * 1963u + 477u * 4u + 477u * 5u + 1000u;
}

static uint8_t encode_duration(uint16_t duration)
{
    uint8_t factor = 0;
    if (duration >= 4032) return 255;
    while (duration > 63) { duration /= 4; ++factor; }
    return (uint8_t)(duration + factor * 64);
}

static uint16_t decode_duration(uint8_t code)
{
    return (uint16_t)((code & 0x3f) * (1u << (2 * (code >> 6))));
}

nsx_sensor_status_t bme688_get_id(bme688_context_t *ctx, uint8_t *chip_id, uint8_t *variant_id)
{
    uint8_t chip, variant;
    nsx_sensor_status_t status;
    if (ctx == NULL || !ctx->_bound || chip_id == NULL || variant_id == NULL) return NSX_SENSOR_E_ARGUMENT;
    status = read_reg(ctx, REG_CHIP_ID, &chip, 1);
    if (status == NSX_SENSOR_OK) status = read_reg(ctx, REG_VARIANT_ID, &variant, 1);
    if (status == NSX_SENSOR_OK) { *chip_id = chip; *variant_id = variant; }
    return status;
}

nsx_sensor_status_t bme688_bind(bme688_context_t *ctx, const nsx_sensor_bus_t *bus,
                              const nsx_sensor_runtime_t *runtime)
{
    if (!ctx || nsx_sensor_bus_validate(bus) != NSX_SENSOR_OK) return NSX_SENSOR_E_ARGUMENT;
    if (bus->max_tx_size < 2 || bus->max_rx_size < 23) return NSX_SENSOR_E_UNSUPPORTED;
    nsx_sensor_bus_t bus_copy = *bus;
    nsx_sensor_runtime_t runtime_copy = {0};
    if (runtime) runtime_copy = *runtime;
    memset(ctx, 0, sizeof(*ctx));
    ctx->bus = bus_copy;
    ctx->runtime = runtime_copy;
    ctx->_bound = true;
    return NSX_SENSOR_OK;
}

bme688_config_t bme688_default_config(void)
{
    bme688_config_t config = { BME688_OS_2X, BME688_OS_16X, BME688_OS_1X, BME688_FILTER_3 };
    return config;
}

nsx_sensor_status_t bme688_validate_config(const bme688_config_t *config)
{
    return valid_config(config) ? NSX_SENSOR_OK : NSX_SENSOR_E_ARGUMENT;
}

nsx_sensor_status_t bme688_probe(bme688_context_t *ctx, bme688_identity_t *identity)
{
    bme688_identity_t result;
    if (!identity) return NSX_SENSOR_E_ARGUMENT;
    nsx_sensor_status_t status = bme688_get_id(ctx, &result.chip_id, &result.variant_id);
    if (status != NSX_SENSOR_OK) return status;
    if (result.chip_id != 0x61 || result.variant_id != 1) return NSX_SENSOR_E_DEVICE;
    *identity = result;
    return NSX_SENSOR_OK;
}

nsx_sensor_status_t bme688_reset(bme688_context_t *ctx)
{
    if (!ctx || !ctx->_bound) return NSX_SENSOR_E_STATE;
    if (!ctx->runtime.delay_us) return NSX_SENSOR_E_ARGUMENT;
    ctx->_initialized = ctx->_config_valid = ctx->_heater_valid = ctx->_pending = false;
    ctx->_gas_enabled = false;
    ctx->_heater_ms = 0;
    nsx_sensor_status_t status = write_reg(ctx, REG_RESET, 0xb6);
    if (status != NSX_SENSOR_OK) return status;
    ctx->runtime.delay_us(ctx->runtime.user, 10000);
    return NSX_SENSOR_OK;
}

nsx_sensor_status_t bme688_init(bme688_context_t *ctx, const bme688_config_t *config)
{
    uint8_t bytes[42];
    bme688_identity_t identity;
    bme688_heater_t heater = { .enabled = false };
    if (!ctx || !ctx->_bound) return NSX_SENSOR_E_STATE;
    if (!valid_config(config) || !ctx->runtime.delay_us) return NSX_SENSOR_E_ARGUMENT;
    bme688_config_t copy = *config;
    ctx->_initialized = ctx->_config_valid = ctx->_heater_valid = ctx->_pending = false;
    nsx_sensor_status_t status = bme688_probe(ctx, &identity);
    if (status != NSX_SENSOR_OK) return status;
    status = bme688_reset(ctx);
    if (status != NSX_SENSOR_OK) return status;
    status = read_reg(ctx, 0x8a, bytes, 23);
    if (status == NSX_SENSOR_OK) status = read_reg(ctx, 0xe1, bytes + 23, 14);
    if (status == NSX_SENSOR_OK) status = read_reg(ctx, 0x00, bytes + 37, 5);
    if (status != NSX_SENSOR_OK) return status;
    bme688_decode_calibration(&ctx->_calibration, bytes);
    /* Reject unprogrammed / disconnected calibration banks. */
    if (ctx->_calibration.par_t1 == 0 || ctx->_calibration.par_t1 == UINT16_MAX ||
        ctx->_calibration.par_t2 == 0 || ctx->_calibration.par_p1 == 0 ||
        ctx->_calibration.par_p1 == UINT16_MAX) return NSX_SENSOR_E_CALIBRATION;
    ctx->_initialized = true;
    status = bme688_set_config(ctx, &copy);
    if (status == NSX_SENSOR_OK) status = bme688_set_heater(ctx, &heater);
    if (status != NSX_SENSOR_OK) ctx->_initialized = false;
    return status;
}

nsx_sensor_status_t bme688_sleep(bme688_context_t *ctx, uint32_t timeout_us)
{
    uint8_t ctrl;
    nsx_sensor_status_t status;
    if (!ready(ctx)) return NSX_SENSOR_E_STATE;
    ctx->_pending = false;
    wait_budget_t budget = wait_begin(ctx, timeout_us);
    for (;;) {
        status = read_reg(ctx, REG_CTRL_MEAS, &ctrl, 1);
        if (status != NSX_SENSOR_OK) return status;
        if ((ctrl & 3) == 0) return NSX_SENSOR_OK;
        status = write_reg(ctx, REG_CTRL_MEAS, (uint8_t)(ctrl & ~3));
        if (status != NSX_SENSOR_OK) return status;
        /* Writes may be ignored until the current conversion completes. */
        status = read_reg(ctx, REG_CTRL_MEAS, &ctrl, 1);
        if (status != NSX_SENSOR_OK) return status;
        if ((ctrl & 3) == 0) return NSX_SENSOR_OK;
        uint32_t remaining = wait_remaining(ctx, &budget);
        if (remaining == 0) return NSX_SENSOR_E_TIMEOUT;
        uint32_t delay = remaining < POLL_US ? remaining : POLL_US;
        wait_delay(ctx, &budget, delay);
    }
}

nsx_sensor_status_t bme688_set_config(bme688_context_t *ctx, const bme688_config_t *config)
{
    nsx_sensor_status_t status;
    if (!ready(ctx)) return NSX_SENSOR_E_STATE;
    if (!valid_config(config)) return NSX_SENSOR_E_ARGUMENT;
    status = bme688_sleep(ctx, SLEEP_TIMEOUT_US);
    if (status != NSX_SENSOR_OK) return status;
    ctx->_config_valid = false;
    /* Humidity setting is latched by the subsequent ctrl_meas write. */
    status = update_reg(ctx, REG_CTRL_HUM, 7, (uint8_t)config->humidity);
    if (status == NSX_SENSOR_OK) status = update_reg(ctx, REG_CONFIG, 0x1c, (uint8_t)(config->filter << 2));
    if (status == NSX_SENSOR_OK) status = update_reg(ctx, REG_CTRL_MEAS, 0xff,
        (uint8_t)((config->temperature << 5) | (config->pressure << 2)));
    if (status == NSX_SENSOR_OK) {
        ctx->_config = *config;
        ctx->_config_valid = true;
    }
    return status;
}

nsx_sensor_status_t bme688_get_config(bme688_context_t *ctx, bme688_config_t *config)
{
    uint8_t bytes[4];
    bme688_config_t result;
    nsx_sensor_status_t status;
    if (!ready(ctx)) return NSX_SENSOR_E_STATE;
    if (config == NULL) return NSX_SENSOR_E_ARGUMENT;
    status = read_reg(ctx, REG_CTRL_HUM, bytes, sizeof(bytes));
    if (status != NSX_SENSOR_OK) return status;
    result.humidity = (bme688_oversampling_t)(bytes[0] & 7);
    result.temperature = (bme688_oversampling_t)(bytes[2] >> 5);
    result.pressure = (bme688_oversampling_t)((bytes[2] >> 2) & 7);
    result.filter = (bme688_filter_t)((bytes[3] >> 2) & 7);
    if (!valid_config(&result)) return NSX_SENSOR_E_STATE;
    *config = result;
    return NSX_SENSOR_OK;
}

nsx_sensor_status_t bme688_set_heater(bme688_context_t *ctx, const bme688_heater_t *heater)
{
    uint8_t resistance = 0, duration = 0;
    nsx_sensor_status_t status;
    if (!ready(ctx)) return NSX_SENSOR_E_STATE;
    if (heater == NULL || (heater->enabled && (heater->temperature_c > 400 ||
        heater->duration_ms == 0 || heater->duration_ms > 4032 ||
        !isfinite(heater->ambient_temperature_c) || heater->ambient_temperature_c < -40 ||
        heater->ambient_temperature_c > 85))) return NSX_SENSOR_E_ARGUMENT;
    if (heater->enabled) {
        resistance = bme688_heater_resistance(&ctx->_calibration, heater->temperature_c,
                                              heater->ambient_temperature_c);
        duration = encode_duration(heater->duration_ms);
    }
    status = bme688_sleep(ctx, SLEEP_TIMEOUT_US);
    if (status != NSX_SENSOR_OK) return status;
    ctx->_heater_valid = false;
    status = write_reg(ctx, REG_RES_HEAT0, resistance);
    if (status == NSX_SENSOR_OK) status = write_reg(ctx, REG_GAS_WAIT0, duration);
    if (status == NSX_SENSOR_OK) status = update_reg(ctx, REG_CTRL_GAS0, 0x08, heater->enabled ? 0 : 0x08);
    /* BME688 run_gas=2; nb_conv=0 selects only profile zero in forced mode. */
    if (status == NSX_SENSOR_OK) status = update_reg(ctx, REG_CTRL_GAS1, 0x3f, heater->enabled ? 0x20 : 0);
    if (status == NSX_SENSOR_OK) {
        ctx->_heater_valid = true;
        ctx->_gas_enabled = heater->enabled;
        ctx->_heater_ms = decode_duration(duration);
    }
    return status;
}

nsx_sensor_status_t bme688_get_heater(bme688_context_t *ctx, bme688_heater_state_t *heater)
{
    uint8_t controls[2];
    bme688_heater_state_t result;
    nsx_sensor_status_t status;
    if (!ready(ctx)) return NSX_SENSOR_E_STATE;
    if (heater == NULL) return NSX_SENSOR_E_ARGUMENT;
    status = read_reg(ctx, REG_CTRL_GAS0, controls, 2);
    if (status == NSX_SENSOR_OK) status = read_reg(ctx, REG_RES_HEAT0, &result.resistance_code, 1);
    if (status == NSX_SENSOR_OK) status = read_reg(ctx, REG_GAS_WAIT0, &result.duration_code, 1);
    if (status != NSX_SENSOR_OK) return status;
    result.enabled = (controls[0] & 8) == 0 && (controls[1] & 0x30) == 0x20;
    result.duration_ms = decode_duration(result.duration_code);
    *heater = result;
    return NSX_SENSOR_OK;
}

nsx_sensor_status_t bme688_start(bme688_context_t *ctx, uint32_t *wait_us)
{
    uint8_t ctrl, discarded_field[17];
    nsx_sensor_status_t status;
    if (!ready(ctx) || !ctx->_config_valid || !ctx->_heater_valid) return NSX_SENSOR_E_STATE;
    if (wait_us == NULL) return NSX_SENSOR_E_ARGUMENT;
    if (ctx->_pending) return NSX_SENSOR_E_BUSY;
    status = read_reg(ctx, REG_CTRL_MEAS, &ctrl, 1);
    if (status != NSX_SENSOR_OK) return status;
    if ((ctrl & 3) != 0) return NSX_SENSOR_E_BUSY;
    /* Drain any unread previous result before triggering. Freshness is the
     * new_data flag, not sub_meas_index (documented for parallel mode only). */
    status = read_reg(ctx, REG_FIELD0, discarded_field, sizeof(discarded_field));
    if (status != NSX_SENSOR_OK) return status;
    status = write_reg(ctx, REG_CTRL_MEAS, (uint8_t)((ctrl & ~3) | 1));
    if (status != NSX_SENSOR_OK) return status;
    ctx->_measurement_us = measurement_time(&ctx->_config) + (uint32_t)ctx->_heater_ms * 1000u + 1000u;
    ctx->_pending = true;
    *wait_us = ctx->_measurement_us;
    return NSX_SENSOR_OK;
}

nsx_sensor_status_t bme688_collect(bme688_context_t *ctx, bme688_sample_t *sample)
{
    uint8_t field[17];
    bme688_sample_t result = { 0 };
    nsx_sensor_status_t status;
    if (!ready(ctx) || !ctx->_pending) return NSX_SENSOR_E_STATE;
    if (sample == NULL) return NSX_SENSOR_E_ARGUMENT;
    status = read_reg(ctx, REG_FIELD0, field, sizeof(field));
    if (status != NSX_SENSOR_OK) return status;
    if ((field[0] & 0x80) == 0 || (field[0] & 0x60) != 0) return NSX_SENSOR_AGAIN;
    result.measurement_index = field[1];
    result.gas_index = field[0] & 15;
    if (result.gas_index != 0) return NSX_SENSOR_E_STATE;
    result.pressure_valid = ctx->_config.pressure != BME688_OS_OFF;
    result.humidity_valid = ctx->_config.humidity != BME688_OS_OFF;
    result.gas_valid = ctx->_gas_enabled && (field[16] & 0x20) != 0;
    result.heater_stable = ctx->_gas_enabled && (field[16] & 0x10) != 0;
    bme688_compensate(&ctx->_calibration, field, &result);
    if (!isfinite(result.temperature_c) || !isfinite(result.pressure_pa) ||
        !isfinite(result.humidity_percent) || !isfinite(result.gas_resistance_ohm)) return NSX_SENSOR_E_CALIBRATION;
    *sample = result;
    ctx->_pending = false;
    return NSX_SENSOR_OK;
}

nsx_sensor_status_t bme688_measure(bme688_context_t *ctx, bme688_sample_t *sample, uint32_t timeout_us)
{
    uint32_t wait;
    nsx_sensor_status_t status;
    if (sample == NULL) return NSX_SENSOR_E_ARGUMENT;
    status = bme688_start(ctx, &wait);
    if (status != NSX_SENSOR_OK) return status;
    wait_budget_t budget = wait_begin(ctx, timeout_us);
    if (wait > timeout_us) wait = timeout_us;
    if (wait != 0) wait_delay(ctx, &budget, wait);
    for (;;) {
        status = bme688_collect(ctx, sample);
        if (status != NSX_SENSOR_AGAIN) return status;
        uint32_t remaining = wait_remaining(ctx, &budget);
        if (remaining == 0) return NSX_SENSOR_E_TIMEOUT;
        uint32_t delay = remaining < POLL_US ? remaining : POLL_US;
        wait_delay(ctx, &budget, delay);
    }
}
