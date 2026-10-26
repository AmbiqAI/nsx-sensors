/* SPDX-License-Identifier: BSD-3-Clause */
#define DT_DRV_COMPAT ambiq_nsx_bme688
#include <nsx/sensors/bme688.h>
#include <nsx/sensors/port_zephyr.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>
#include <errno.h>

struct nsx_bme688_config {
    bool spi;
    union { struct i2c_dt_spec i2c; struct spi_dt_spec spi; } bus;
};
struct nsx_bme688_data {
    struct k_mutex lock;
    nsx_sensor_zephyr_context_t endpoint;
    bme688_context_t core;
    bme688_sample_t sample;
    bool valid;
    bool recover;
};
static bool supported(enum sensor_channel channel)
{
    return channel == SENSOR_CHAN_ALL || channel == SENSOR_CHAN_AMBIENT_TEMP ||
           channel == SENSOR_CHAN_PRESS || channel == SENSOR_CHAN_HUMIDITY;
}
static int initialize_core(struct nsx_bme688_data *data)
{
    bme688_config_t config = bme688_default_config();
    return nsx_sensor_zephyr_errno(bme688_init(&data->core, &config));
}
static int fetch(const struct device *dev, enum sensor_channel channel)
{
    if (!supported(channel)) return -ENOTSUP;
    struct nsx_bme688_data *data = dev->data;
    k_mutex_lock(&data->lock, K_FOREVER);
    data->valid = false;
    int result = data->recover ? initialize_core(data) : 0;
    bme688_sample_t sample;
    if (!result) result = nsx_sensor_zephyr_errno(bme688_measure(&data->core, &sample, 1000000));
    if (!result && (!sample.pressure_valid || !sample.humidity_valid)) result = -EIO;
    if (!result) {
        data->sample = sample;
        data->valid = true;
    }
    /* A failed collect may consume hardware freshness while leaving pending
     * state. Reset/reinitialize on the next fetch, including after bus errors.
     * Never publish a partial sample or require application-level core access. */
    data->recover = result != 0;
    k_mutex_unlock(&data->lock);
    return result;
}
static int channel_get(const struct device *dev, enum sensor_channel channel,
                       struct sensor_value *value)
{
    if (channel == SENSOR_CHAN_ALL || !supported(channel)) return -ENOTSUP;
    if (!value) return -EINVAL;
    struct nsx_bme688_data *data = dev->data;
    k_mutex_lock(&data->lock, K_FOREVER);
    int result = -ENODATA;
    if (data->valid) {
        float reading = channel == SENSOR_CHAN_AMBIENT_TEMP ? data->sample.temperature_c :
            channel == SENSOR_CHAN_PRESS ? data->sample.pressure_pa / 1000.0f :
                                          data->sample.humidity_percent;
        /* Zephyr's pressure unit is kPa. Build a local value so error paths
         * cannot alter the caller's output. */
        struct sensor_value converted;
        result = sensor_value_from_double(&converted, (double)reading);
        if (!result) *value = converted;
    }
    k_mutex_unlock(&data->lock);
    return result;
}
static int initialize(const struct device *dev)
{
    const struct nsx_bme688_config *config = dev->config;
    struct nsx_bme688_data *data = dev->data;
    k_mutex_init(&data->lock);
    nsx_sensor_status_t status;
    if (config->spi) {
#ifdef CONFIG_SPI
        if (!spi_is_ready_dt(&config->bus.spi)) return -ENODEV;
#endif
        status = nsx_sensor_zephyr_bind_spi(&data->endpoint, &config->bus.spi);
    } else {
#ifdef CONFIG_I2C
        if (!i2c_is_ready_dt(&config->bus.i2c)) return -ENODEV;
#endif
        status = nsx_sensor_zephyr_bind_i2c(&data->endpoint, &config->bus.i2c);
    }
    if (status != NSX_SENSOR_OK) return nsx_sensor_zephyr_errno(status);
    nsx_sensor_runtime_t runtime = nsx_sensor_zephyr_runtime();
    status = bme688_bind(&data->core, &data->endpoint.bus, &runtime);
    if (status != NSX_SENSOR_OK) return nsx_sensor_zephyr_errno(status);
    return initialize_core(data);
}
static DEVICE_API(sensor, nsx_bme688_api) = {
    .sample_fetch = fetch,
    .channel_get = channel_get,
};
#define NSX_BME688_CONFIG(inst) \
    COND_CODE_1(DT_INST_ON_BUS(inst, spi), \
        (.spi = true, .bus.spi = SPI_DT_SPEC_INST_GET(inst, SPI_WORD_SET(8))), \
        (.spi = false, .bus.i2c = I2C_DT_SPEC_INST_GET(inst)))
#define NSX_BME688_DEFINE(inst) \
    static struct nsx_bme688_data nsx_bme688_data_##inst; \
    static const struct nsx_bme688_config nsx_bme688_config_##inst = { \
        NSX_BME688_CONFIG(inst) \
    }; \
    SENSOR_DEVICE_DT_INST_DEFINE(inst, initialize, NULL, &nsx_bme688_data_##inst, \
        &nsx_bme688_config_##inst, POST_KERNEL, CONFIG_SENSOR_INIT_PRIORITY, &nsx_bme688_api);
DT_INST_FOREACH_STATUS_OKAY(NSX_BME688_DEFINE)
