/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef NSX_SENSORS_ICM45605_H
#define NSX_SENSORS_ICM45605_H
#include <stdbool.h>
#include <stdint.h>
#include <nsx/sensors/bus.h>
#include <nsx/sensors/runtime.h>
/* Typed implementation storage; supplied by the target, not an application API. */
#include <inv_imu_driver.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef enum {
    ICM45605_ACCEL_16G=1, ICM45605_ACCEL_8G=2,
    ICM45605_ACCEL_4G=3, ICM45605_ACCEL_2G=4
} icm45605_accel_range_t;
typedef enum {
    ICM45605_GYRO_2000DPS=1, ICM45605_GYRO_1000DPS=2, ICM45605_GYRO_500DPS=3,
    ICM45605_GYRO_250DPS=4, ICM45605_GYRO_125DPS=5, ICM45605_GYRO_62_5DPS=6,
    ICM45605_GYRO_31_25DPS=7, ICM45605_GYRO_15_625DPS=8
} icm45605_gyro_range_t;
typedef enum {
    ICM45605_ODR_6400HZ=3, ICM45605_ODR_3200HZ=4, ICM45605_ODR_1600HZ=5,
    ICM45605_ODR_800HZ=6, ICM45605_ODR_400HZ=7, ICM45605_ODR_200HZ=8,
    ICM45605_ODR_100HZ=9, ICM45605_ODR_50HZ=10, ICM45605_ODR_25HZ=11,
    ICM45605_ODR_12_5HZ=12
} icm45605_odr_t;
typedef enum {
    ICM45605_FILTER_BYPASS=0, ICM45605_FILTER_ODR_DIV4=1,
    ICM45605_FILTER_ODR_DIV8=2, ICM45605_FILTER_ODR_DIV16=3,
    ICM45605_FILTER_ODR_DIV32=4, ICM45605_FILTER_ODR_DIV64=5,
    ICM45605_FILTER_ODR_DIV128=6
} icm45605_filter_t;
typedef struct {
    icm45605_accel_range_t accel_range;
    icm45605_gyro_range_t gyro_range;
    icm45605_odr_t odr; /* Both sensors, low-noise mode only. */
    icm45605_filter_t accel_filter, gyro_filter;
    bool drdy_interrupt; /* INT2, active-high push-pull pulse. GPIO belongs to caller. */
} icm45605_config_t;
typedef struct { int16_t accel[3], gyro[3], temperature; } icm45605_raw_sample_t;
typedef struct { float accel_g[3], gyro_dps[3], temperature_c; } icm45605_sample_t;
typedef struct { float accel_g[3], gyro_dps[3]; } icm45605_bias_t;
typedef struct {
    nsx_sensor_bus_t bus;
    nsx_sensor_runtime_t runtime;
    inv_imu_device_t _device;
    icm45605_config_t _config;
    icm45605_bias_t _bias;
    nsx_sensor_status_t _io_status;
    bool _bound, _initialized, _running;
} icm45605_context_t;
/* Caller-owned, address-stable context: do not copy/move after bind. Serialize
 * all calls per device and shared bus; no heap, singleton, GPIO ownership or ISR
 * bus calls. Implementation fields are private by convention. Outputs remain
 * unchanged on error. Failed mutation requires init (partial writes possible). */
icm45605_config_t icm45605_default_config(void);
nsx_sensor_status_t icm45605_validate_config(const icm45605_config_t *config);
/* Pure bind; 4-wire SPI only, TX>=4 and RX>=14. Caller configures SPI mode 0/3,
 * <=24 MHz and CS timing. delay_us is required by init/start/calibrate; now_us
 * optional (without it, timeout bounds requested waits, not bus wall time). */
nsx_sensor_status_t icm45605_bind(icm45605_context_t *ctx, const nsx_sensor_bus_t *bus,
                                 const nsx_sensor_runtime_t *runtime);
nsx_sensor_status_t icm45605_probe(icm45605_context_t *ctx, uint8_t *identity);
/* Waits for power-on access, probes, resets and configures; finishes idle.
 * Clears software bias. No automatic calibration or acquisition. */
nsx_sensor_status_t icm45605_init(icm45605_context_t *ctx, const icm45605_config_t *config);
/* Configuration and direct range/filter/rate setters require idle. */
nsx_sensor_status_t icm45605_configure(icm45605_context_t *ctx, const icm45605_config_t *config);
nsx_sensor_status_t icm45605_set_accel_range(icm45605_context_t *ctx, icm45605_accel_range_t value);
nsx_sensor_status_t icm45605_set_gyro_range(icm45605_context_t *ctx, icm45605_gyro_range_t value);
nsx_sensor_status_t icm45605_set_odr(icm45605_context_t *ctx, icm45605_odr_t value);
nsx_sensor_status_t icm45605_set_accel_filter(icm45605_context_t *ctx, icm45605_filter_t value);
nsx_sensor_status_t icm45605_set_gyro_filter(icm45605_context_t *ctx, icm45605_filter_t value);
nsx_sensor_status_t icm45605_set_drdy_interrupt(icm45605_context_t *ctx, bool enable);
nsx_sensor_status_t icm45605_start(icm45605_context_t *ctx);
nsx_sensor_status_t icm45605_stop(icm45605_context_t *ctx);
/* Latest register snapshot, not a FIFO or freshness guarantee. Use data-ready
 * status in a worker for pacing; direct reads may repeat/skip samples. */
nsx_sensor_status_t icm45605_read_raw(icm45605_context_t *ctx, icm45605_raw_sample_t *sample);
nsx_sensor_status_t icm45605_read(icm45605_context_t *ctx, icm45605_sample_t *sample);
/* Requires drdy_interrupt enabled: its hardware mask gates the flag as well
 * as the pin. Consumes INT1 (reset evidence) and INT2 (DRDY) status. An observed
 * unexpected reset requires init. Not ISR-safe; call from a worker. */
nsx_sensor_status_t icm45605_read_data_ready(icm45605_context_t *ctx, bool *ready);
nsx_sensor_status_t icm45605_set_bias(icm45605_context_t *ctx, const icm45605_bias_t *bias);
/* Explicit, blocking stationary +Z calibration: 1..4096 fresh observations,
 * running with drdy_interrupt enabled; overall requested-wait budget.
 * Replaces bias only on full success, uses raw
 * samples (so repeated calibration does not subtract the previous bias).
 * Caller must ensure stillness/level orientation; no motion-quality estimator. */
nsx_sensor_status_t icm45605_calibrate(icm45605_context_t *ctx, uint16_t samples, uint32_t timeout_us);
#ifdef __cplusplus
}
#endif
#endif
