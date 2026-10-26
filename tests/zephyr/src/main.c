/* SPDX-License-Identifier: BSD-3-Clause */
#include <errno.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/ztest.h>
#include <nsx/sensors/port_zephyr.h>
#include <nsx/sensors/bme688.h>
#include <nsx/sensors/ledstick.h>
#include <nsx/sensors/ina228.h>
#include <nsx/sensors/mpu6050.h>
#include <nsx/sensors/max86150.h>
#include <nsx/sensors/icm45605.h>
#include <nsx/sensors/tsl254x.h>
#include <nsx/sensors/t5838.h>
#include "fixture.h"

/* Real Zephyr devices and APIs; only the physical controllers are simulated. */
struct mock_sensor {
    uint8_t memory[256];
    unsigned calls, starts, resets;
    int failure;
    bool stale;
};
static struct mock_sensor sensors[3];
static uint8_t spi_page = 0xa0;
static unsigned spi_calls;
static int spi_failure;
static uint8_t spi_wire[512];
static size_t spi_size;
static uint16_t spi_slave;

static int mock_i2c_init(const struct device *dev)
{
    ARG_UNUSED(dev);
    for (size_t i = 0; i < ARRAY_SIZE(sensors); ++i) fixture(sensors[i].memory);
    return 0;
}
static int mock_i2c_configure(const struct device *dev, uint32_t config)
{
    ARG_UNUSED(dev); ARG_UNUSED(config); return 0;
}
static int mock_i2c_transfer(const struct device *dev, struct i2c_msg *msgs,
                             uint8_t count, uint16_t address)
{
    ARG_UNUSED(dev);
    if (address < 0x76 || address > 0x77) return -ENXIO;
    struct mock_sensor *s = &sensors[address - 0x76];
    ++s->calls;
    if (s->failure) {
        int result = s->failure; s->failure = 0;
        /* Controllers may modify RX before reporting failure. */
        if (msgs[count - 1].flags & I2C_MSG_READ)
            memset(msgs[count - 1].buf, 0xee, msgs[count - 1].len);
        return result;
    }
    if (count == 2) {
        zassert_equal(msgs[0].flags & (I2C_MSG_READ | I2C_MSG_STOP), 0);
        zassert_equal(msgs[1].flags & (I2C_MSG_READ | I2C_MSG_RESTART | I2C_MSG_STOP),
                      I2C_MSG_READ | I2C_MSG_RESTART | I2C_MSG_STOP);
        zassert_equal(msgs[0].len, 1);
        uint8_t reg = msgs[0].buf[0];
        zassert_true((size_t)reg + msgs[1].len <= sizeof(s->memory));
        memcpy(msgs[1].buf, s->memory + reg, msgs[1].len);
        if (reg == 0x1d && msgs[1].len == 17) s->memory[0x1d] &= 0x7f;
    } else {
        zassert_equal(count, 1);
        zassert_true(msgs[0].flags & I2C_MSG_STOP);
        if (msgs[0].flags & I2C_MSG_READ) {
            memset(msgs[0].buf, 0x5a, msgs[0].len);
            return 0;
        }
        zassert_equal(msgs[0].len, 2);
        uint8_t reg = msgs[0].buf[0], value = msgs[0].buf[1];
        if (reg == 0xe0) {
            ++s->resets;
            memset(s->memory + 0x50, 0, 0x29);
        } else {
            s->memory[reg] = value;
            if (reg == 0x74 && (value & 3) == 1) {
                ++s->starts;
                /* Finish immediately; core still performs its real kernel wait. */
                s->memory[reg] &= (uint8_t)~3;
                if (!s->stale)
                    raw_field(s->memory, address == 0x76 ? 519888 : 600000,
                              address == 0x76 ? 415148 : 300000,
                              address == 0x76 ? 35000 : 65535, 512, 5,
                              (uint8_t)s->starts);
            }
        }
    }
    return 0;
}
static DEVICE_API(i2c, mock_i2c_api) = {
    .configure = mock_i2c_configure, .transfer = mock_i2c_transfer,
};
DEVICE_DT_DEFINE(DT_NODELABEL(mock_i2c), mock_i2c_init, NULL, NULL, NULL,
                 POST_KERNEL, 40, &mock_i2c_api);

/* Model Zephyr spi_context_configured(): hardware settings are only applied
 * when the configuration pointer changes, as in pointer-caching controllers. */
static const struct spi_config *spi_cached_config;
static uint32_t spi_effective_frequency;
static uint16_t spi_effective_slave;

static int mock_spi_transfer(const struct device *dev, const struct spi_config *config,
                             const struct spi_buf_set *tx, const struct spi_buf_set *rx)
{
    ARG_UNUSED(dev);
    if (config != spi_cached_config) {
        spi_cached_config = config;
        spi_effective_frequency = config->frequency;
        spi_effective_slave = config->slave;
    }
    if (spi_effective_slave == 2) {
        struct mock_sensor *s = &sensors[2];
        zassert_equal(tx->count, 1); zassert_equal(rx->count, 1);
        const uint8_t *wire = tx->buffers[0].buf;
        uint8_t *out = rx->buffers[0].buf;
        size_t length = tx->buffers[0].len;
        ++s->calls;
        if (s->failure) { int error = s->failure; s->failure = 0; return error; }
        memset(out, 0, length);
        uint8_t reg = wire[0] & 0x7f;
        if (reg == 0x73) {
            zassert_equal(length, 2);
            if (wire[0] & 0x80) out[1] = spi_page;
            else spi_page = wire[1];
            return 0;
        }
        if (!(spi_page & 0x10)) reg |= 0x80;
        if (wire[0] & 0x80) {
            zassert_true((size_t)reg + length - 1 <= sizeof(s->memory));
            memcpy(out + 1, s->memory + reg, length - 1);
            if (reg == 0x1d && length == 18) s->memory[0x1d] &= 0x7f;
        } else {
            zassert_equal(length, 2);
            if (reg == 0xe0) {
                ++s->resets; spi_page = 0xa0;
                memset(s->memory + 0x50, 0, 0x29);
            } else {
                s->memory[reg] = wire[1];
                if (reg == 0x74 && (wire[1] & 3) == 1) {
                    ++s->starts; s->memory[reg] &= (uint8_t)~3;
                    raw_field(s->memory, 519888, 415148, 35000, 512, 5, (uint8_t)s->starts);
                }
            }
        }
        return 0;
    }
    ++spi_calls;
    if (spi_failure) {
        memset(rx->buffers[0].buf, 0xee, rx->buffers[0].len);
        return spi_failure;
    }
    zassert_not_null(tx); zassert_not_null(rx);
    zassert_equal(tx->count, 1); zassert_equal(rx->count, 1);
    zassert_equal(tx->buffers[0].len, rx->buffers[0].len);
    spi_size = tx->buffers[0].len;
    zassert_true(spi_size <= sizeof(spi_wire));
    spi_slave = spi_effective_slave;
    memcpy(spi_wire, tx->buffers[0].buf, spi_size);
    uint8_t *out = rx->buffers[0].buf;
    for (size_t i = 0; i < spi_size; ++i) out[i] = (uint8_t)(0xa0 + i);
    return 0;
}
static int mock_spi_release(const struct device *dev, const struct spi_config *config)
{
    ARG_UNUSED(dev); ARG_UNUSED(config); return 0;
}
static DEVICE_API(spi, mock_spi_api) = {
    .transceive = mock_spi_transfer, .release = mock_spi_release,
};
DEVICE_DT_DEFINE(DT_NODELABEL(mock_spi), NULL, NULL, NULL, NULL,
                 POST_KERNEL, 40, &mock_spi_api);

static const struct device *const first = DEVICE_DT_GET(DT_NODELABEL(sensor0));
static const struct device *const second = DEVICE_DT_GET(DT_NODELABEL(sensor1));
static void expect_channel(const struct device *dev, enum sensor_channel channel,
                           double expected)
{
    struct sensor_value value;
    zassert_ok(sensor_channel_get(dev, channel, &value));
    zassert_within(sensor_value_to_double(&value), expected, 0.002);
}

K_THREAD_STACK_ARRAY_DEFINE(fetch_stacks, 2, 4096);
static struct k_thread fetch_threads[2];
static struct k_sem fetch_gate;
static int fetch_results[2];
static void concurrent_fetch(void *index, void *unused1, void *unused2)
{
    ARG_UNUSED(unused1); ARG_UNUSED(unused2);
    k_sem_take(&fetch_gate, K_FOREVER);
    fetch_results[(size_t)index] = sensor_sample_fetch(first);
}

ZTEST(nsx_zephyr, test_wrapper_instances_units_failure_and_freshness)
{
    bme688_config_t defaults = bme688_default_config();
    zassert_equal(bme688_validate_config(&defaults), NSX_SENSOR_OK);
    const struct device *spi_sensor = DEVICE_DT_GET(DT_NODELABEL(sensor_spi));
    struct sensor_value value = { 123, 456 };
    zassert_true(device_is_ready(spi_sensor));
    zassert_ok(sensor_sample_fetch(spi_sensor));
    expect_channel(spi_sensor, SENSOR_CHAN_PRESS, 78.6838438);
    sensors[2].failure = -EIO;
    zassert_equal(sensor_sample_fetch(spi_sensor), -EIO);
    zassert_equal(sensor_channel_get(spi_sensor, SENSOR_CHAN_PRESS, &value), -ENODATA);
    zassert_ok(sensor_sample_fetch(spi_sensor));
    expect_channel(spi_sensor, SENSOR_CHAN_AMBIENT_TEMP, 25.1583958);
    zassert_true(device_is_ready(first)); zassert_true(device_is_ready(second));
    zassert_equal(sensor_channel_get(first, SENSOR_CHAN_AMBIENT_TEMP, &value), -ENODATA);
    zassert_ok(sensor_sample_fetch(first));
    expect_channel(first, SENSOR_CHAN_AMBIENT_TEMP, 25.1583958);
    expect_channel(first, SENSOR_CHAN_PRESS, 78.6838438); /* Zephyr kPa, core Pa. */
    expect_channel(first, SENSOR_CHAN_HUMIDITY, 66.0606384);
    zassert_equal(sensor_channel_get(second, SENSOR_CHAN_PRESS, &value), -ENODATA);
    zassert_ok(sensor_sample_fetch_chan(second, SENSOR_CHAN_PRESS));
    expect_channel(second, SENSOR_CHAN_AMBIENT_TEMP, 50.4145508);
    expect_channel(second, SENSOR_CHAN_PRESS, 103.514922);
    expect_channel(second, SENSOR_CHAN_HUMIDITY, 100.0);
    unsigned calls = sensors[0].calls;
    zassert_equal(sensor_sample_fetch_chan(first, SENSOR_CHAN_GAS_RES), -ENOTSUP);
    zassert_equal(sensor_channel_get(first, SENSOR_CHAN_GAS_RES, &value), -ENOTSUP);
    zassert_equal(sensors[0].calls, calls);
    expect_channel(first, SENSOR_CHAN_PRESS, 78.6838438);

    sensors[0].failure = -EIO;
    zassert_equal(sensor_sample_fetch(first), -EIO);
    zassert_equal(sensor_channel_get(first, SENSOR_CHAN_PRESS, &value), -ENODATA);
    expect_channel(second, SENSOR_CHAN_PRESS, 103.514922);
    unsigned resets = sensors[0].resets;
    zassert_ok(sensor_sample_fetch(first));
    zassert_true(sensors[0].resets > resets);
    expect_channel(first, SENSOR_CHAN_PRESS, 78.6838438);

    /* A pre-existing field must be drained; absence of a new field times out. */
    raw_field(sensors[0].memory, 600000, 300000, 65535, 512, 5, 99);
    sensors[0].stale = true;
    zassert_equal(sensor_sample_fetch(first), -ETIMEDOUT);
    zassert_equal(sensor_channel_get(first, SENSOR_CHAN_PRESS, &value), -ENODATA);
    sensors[0].stale = false;
    zassert_ok(sensor_sample_fetch(first));
    expect_channel(first, SENSOR_CHAN_PRESS, 78.6838438);

    /* Both threads enter fetch together. Its kernel wait would expose core
     * pending/busy state to the other thread without whole-operation locking. */
    k_sem_init(&fetch_gate, 0, 2);
    unsigned starts = sensors[0].starts;
    for (size_t i = 0; i < 2; ++i)
        k_thread_create(&fetch_threads[i], fetch_stacks[i], K_THREAD_STACK_SIZEOF(fetch_stacks[i]),
                        concurrent_fetch, (void *)i, NULL, NULL, 5, 0, K_NO_WAIT);
    k_sem_give(&fetch_gate); k_sem_give(&fetch_gate);
    for (size_t i = 0; i < 2; ++i) {
        zassert_ok(k_thread_join(&fetch_threads[i], K_SECONDS(3)));
        zassert_ok(fetch_results[i]);
    }
    zassert_equal(sensors[0].starts, starts + 2);
}

ZTEST(nsx_zephyr, test_i2c_port_transactions_limits_and_errors)
{
    nsx_sensor_zephyr_context_t ctx;
    const struct i2c_dt_spec spec = { DEVICE_DT_GET(DT_NODELABEL(mock_i2c)), 0x76 };
    zassert_equal(nsx_sensor_zephyr_bind_i2c(&ctx, &spec), NSX_SENSOR_OK);
    uint8_t reg = 0xd0, out = 0;
    nsx_sensor_transfer_t t = { &reg, 1, &out, 1 };
    zassert_equal(nsx_sensor_bus_transfer(&ctx.bus, &t), NSX_SENSOR_OK);
    zassert_equal(out, 0x61);
    unsigned calls = sensors[0].calls;
    t.tx_size = NSX_SENSOR_ZEPHYR_MAX_TRANSFER + 1;
    zassert_not_equal(nsx_sensor_bus_transfer(&ctx.bus, &t), NSX_SENSOR_OK);
    zassert_equal(sensors[0].calls, calls);
    t.tx_size = 1; t.tx = NULL;
    zassert_equal(nsx_sensor_bus_transfer(&ctx.bus, &t), NSX_SENSOR_E_ARGUMENT);
    zassert_equal(sensors[0].calls, calls);
    t.tx = &reg;
    static const struct { int native; nsx_sensor_status_t status; } errors[] = {
        { -EIO, NSX_SENSOR_E_IO }, { -ETIMEDOUT, NSX_SENSOR_E_TIMEOUT },
        { -EBUSY, NSX_SENSOR_E_BUSY }, { -ENOTSUP, NSX_SENSOR_E_UNSUPPORTED },
    };
    for (size_t i = 0; i < ARRAY_SIZE(errors); ++i) {
        out = 0x7b;
        sensors[0].failure = errors[i].native;
        zassert_equal(nsx_sensor_bus_transfer(&ctx.bus, &t), errors[i].status);
        zassert_equal(ctx.last_native_status, errors[i].native);
        zassert_equal(out, 0x7b);
    }
    t.tx = NULL; t.tx_size = 0;
    zassert_equal(nsx_sensor_bus_transfer(&ctx.bus, &t), NSX_SENSOR_OK);
    zassert_equal(out, 0x5a);
}

ZTEST(nsx_zephyr, test_spi_single_transaction_fill_and_ownership)
{
    nsx_sensor_zephyr_context_t a, b;
    static const struct spi_dt_spec spec_a = {
        .bus = DEVICE_DT_GET(DT_NODELABEL(mock_spi)),
        .config = { .frequency = 1000000, .operation = SPI_WORD_SET(8), .slave = 0 } };
    static const struct spi_dt_spec spec_b = {
        .bus = DEVICE_DT_GET(DT_NODELABEL(mock_spi)),
        .config = { .frequency = 1000000, .operation = SPI_WORD_SET(8), .slave = 1 } };
    zassert_equal(nsx_sensor_zephyr_bind_spi(&a, &spec_a), NSX_SENSOR_OK);
    zassert_equal(nsx_sensor_zephyr_bind_spi(&b, &spec_b), NSX_SENSOR_OK);
    uint8_t tx[] = { 0x81, 0x42 }, rx[3] = {0};
    nsx_sensor_transfer_t t = { tx, sizeof(tx), rx, sizeof(rx) };
    unsigned calls = spi_calls;
    zassert_equal(nsx_sensor_bus_transfer(&a.bus, &t), NSX_SENSOR_OK);
    zassert_equal(spi_calls, calls + 1); zassert_equal(spi_size, 5);
    zassert_equal(spi_slave, 0); zassert_mem_equal(spi_wire, tx, 2);
    zassert_equal(spi_wire[2] | spi_wire[3] | spi_wire[4], 0);
    const uint8_t expected[] = { 0xa2, 0xa3, 0xa4 };
    zassert_mem_equal(rx, expected, sizeof(rx));
    zassert_equal(nsx_sensor_bus_transfer(&b.bus, &t), NSX_SENSOR_OK);
    zassert_equal(spi_slave, 1);
    spi_failure = -ETIMEDOUT;
    zassert_equal(nsx_sensor_bus_transfer(&a.bus, &t), NSX_SENSOR_E_TIMEOUT);
    zassert_equal(a.last_native_status, -ETIMEDOUT);
    zassert_mem_equal(rx, expected, sizeof(rx));
    zassert_equal(b.last_native_status, 0);
    spi_failure = 0;
    calls = spi_calls;
    struct spi_dt_spec spec = spec_b;
    spec.config.operation |= SPI_HOLD_ON_CS;
    zassert_equal(nsx_sensor_zephyr_bind_spi(&b, &spec), NSX_SENSOR_E_UNSUPPORTED);
    zassert_equal(spi_calls, calls);
    spec.config.operation = SPI_WORD_SET(8) | SPI_MODE_LOOP;
    zassert_equal(nsx_sensor_zephyr_bind_spi(&b, &spec), NSX_SENSOR_E_UNSUPPORTED);
    zassert_equal(spi_calls, calls);
    zassert_equal(b._spi, &spec_b);
    zassert_equal(b.last_native_status, 0);
    t.rx_size = NSX_SENSOR_ZEPHYR_MAX_TRANSFER + 1;
    zassert_not_equal(nsx_sensor_bus_transfer(&a.bus, &t), NSX_SENSOR_OK);
    zassert_equal(spi_calls, calls);
    uint8_t max_tx[NSX_SENSOR_ZEPHYR_MAX_TRANSFER] = {0};
    uint8_t max_rx[NSX_SENSOR_ZEPHYR_MAX_TRANSFER];
    t = (nsx_sensor_transfer_t){ max_tx, sizeof(max_tx), max_rx, sizeof(max_rx) };
    zassert_equal(nsx_sensor_bus_transfer(&a.bus, &t), NSX_SENSOR_OK);
    zassert_equal(spi_size, sizeof(max_tx) + sizeof(max_rx));
}
ZTEST(nsx_zephyr, test_spi_rebind_configuration_identity)
{
    static const struct spi_dt_spec slow = {
        .bus = DEVICE_DT_GET(DT_NODELABEL(mock_spi)),
        .config = { .frequency = 1000000, .operation = SPI_WORD_SET(8), .slave = 0 } };
    static const struct spi_dt_spec fast = {
        .bus = DEVICE_DT_GET(DT_NODELABEL(mock_spi)),
        .config = { .frequency = 2000000, .operation = SPI_WORD_SET(8), .slave = 0 } };
    nsx_sensor_zephyr_context_t endpoint;
    uint8_t command = 0x81, value;
    nsx_sensor_transfer_t transfer = { &command, 1, &value, 1 };
    /* Isolate the first transaction from earlier tests' cached configuration. */
    spi_cached_config = NULL;
    zassert_equal(nsx_sensor_zephyr_bind_spi(&endpoint, &slow), NSX_SENSOR_OK);
    zassert_equal(nsx_sensor_bus_transfer(&endpoint.bus, &transfer), NSX_SENSOR_OK);
    zassert_equal(spi_effective_frequency, 1000000);
    zassert_equal(nsx_sensor_zephyr_bind_spi(&endpoint, &fast), NSX_SENSOR_OK);
    zassert_equal(nsx_sensor_bus_transfer(&endpoint.bus, &transfer), NSX_SENSOR_OK);
    zassert_equal(spi_effective_frequency, 2000000,
                  "Rebind retained the controller's previous frequency");
}
ZTEST_SUITE(nsx_zephyr, NULL, NULL, NULL, NULL, NULL);
