/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef NSX_SENSORS_ICM42670_H
#define NSX_SENSORS_ICM42670_H
#include <stdbool.h>
#include <stdint.h>
#include <nsx/sensors/bus.h>
#include <nsx/sensors/runtime.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef enum {
    ICM42670_ACCEL_16G=0, ICM42670_ACCEL_8G=1,
    ICM42670_ACCEL_4G=2, ICM42670_ACCEL_2G=3
} icm42670_accel_range_t;
typedef enum {
    ICM42670_GYRO_2000DPS=0, ICM42670_GYRO_1000DPS=1,
    ICM42670_GYRO_500DPS=2, ICM42670_GYRO_250DPS=3
} icm42670_gyro_range_t;
typedef enum {
    ICM42670_ODR_1600HZ=5, ICM42670_ODR_800HZ=6, ICM42670_ODR_400HZ=7,
    ICM42670_ODR_200HZ=8, ICM42670_ODR_100HZ=9, ICM42670_ODR_50HZ=10,
    ICM42670_ODR_25HZ=11, ICM42670_ODR_12_5HZ=12
} icm42670_odr_t;
typedef enum {
    ICM42670_FILTER_BYPASS=0, ICM42670_FILTER_180HZ=1,
    ICM42670_FILTER_121HZ=2, ICM42670_FILTER_73HZ=3,
    ICM42670_FILTER_53HZ=4, ICM42670_FILTER_34HZ=5,
    ICM42670_FILTER_25HZ=6, ICM42670_FILTER_16HZ=7
} icm42670_filter_t;
typedef struct {
    icm42670_accel_range_t accel_range;
    icm42670_gyro_range_t gyro_range;
    icm42670_odr_t odr; /* Both sensors, low-noise mode. */
    icm42670_filter_t accel_filter, gyro_filter;
    bool drdy_interrupt; /* INT1 active-high push-pull pulse; caller owns GPIO. */
} icm42670_config_t;
typedef struct { int16_t accel[3], gyro[3], temperature; } icm42670_raw_sample_t;
typedef struct { float accel_g[3], gyro_dps[3], temperature_c; } icm42670_sample_t;
typedef struct {
    nsx_sensor_bus_t bus;
    nsx_sensor_runtime_t runtime;
    icm42670_config_t _config;
    bool _bound, _initialized, _running;
} icm42670_context_t;
/* Caller-owned context and borrowed callback users. Serialize whole operations;
 * exclusive device ownership is required. No heap, GPIO ownership or ISR I/O.
 * Fields with underscores are private. Failed reads preserve outputs; failed
 * mutations invalidate initialization (partial writes possible). Bad arguments
 * and state preflight failures do not invalidate initialization. */
icm42670_config_t icm42670_default_config(void);
nsx_sensor_status_t icm42670_validate_config(const icm42670_config_t *config);
/* Pure bind: TX>=2, RX>=14; delay_us required, now_us unused. Endpoint owns I2C
 * address (0x68/0x69, <=1 MHz) or 4-wire SPI CS (mode 0/3, <=24 MHz and datasheet
 * CS timing). Supply must be stable before init. */
nsx_sensor_status_t icm42670_bind(icm42670_context_t *ctx, const nsx_sensor_bus_t *bus,
                                const nsx_sensor_runtime_t *runtime);
nsx_sensor_status_t icm42670_probe(icm42670_context_t *ctx, uint8_t *identity);
/* Resets and configures, finishes stopped. Conservative 60 ms pre-reset guard
 * permits recovery from an uncertain power transition. Only 42670-P accepted. */
nsx_sensor_status_t icm42670_init(icm42670_context_t *ctx, const icm42670_config_t *config);
/* Reset leaves context bound but uninitialized; init required afterwards. */
nsx_sensor_status_t icm42670_reset(icm42670_context_t *ctx);
/* Configuration and setters require initialized, stopped device. */
nsx_sensor_status_t icm42670_configure(icm42670_context_t *ctx, const icm42670_config_t *config);
nsx_sensor_status_t icm42670_set_accel_range(icm42670_context_t *ctx, icm42670_accel_range_t value);
nsx_sensor_status_t icm42670_set_gyro_range(icm42670_context_t *ctx, icm42670_gyro_range_t value);
nsx_sensor_status_t icm42670_set_odr(icm42670_context_t *ctx, icm42670_odr_t value);
nsx_sensor_status_t icm42670_set_accel_filter(icm42670_context_t *ctx, icm42670_filter_t value);
nsx_sensor_status_t icm42670_set_gyro_filter(icm42670_context_t *ctx, icm42670_filter_t value);
nsx_sensor_status_t icm42670_set_drdy_interrupt(icm42670_context_t *ctx, bool enable);
/* Start waits 60 ms, stop waits 21 ms, including uncertain write failures.
 * Repeated start/stop is a no-op. These guards satisfy gyro minimum-on time,
 * write quiet time and off-to-on delay; they do not guarantee a fresh sample. */
nsx_sensor_status_t icm42670_start(icm42670_context_t *ctx);
nsx_sensor_status_t icm42670_stop(icm42670_context_t *ctx);
/* Running only. Latest snapshot, not FIFO or freshness guarantee. */
nsx_sensor_status_t icm42670_read_raw(icm42670_context_t *ctx, icm42670_raw_sample_t *sample);
nsx_sensor_status_t icm42670_read(icm42670_context_t *ctx, icm42670_sample_t *sample);
/* Running only; consumes read-to-clear DRDY status even without pin routing.
 * A failed transfer may also consume status. Call from worker, not ISR. */
nsx_sensor_status_t icm42670_read_data_ready(icm42670_context_t *ctx, bool *ready);
#ifdef __cplusplus
}
#endif
#endif
