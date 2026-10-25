/* SPDX-License-Identifier: BSD-3-Clause
 * Original MPU6050 driver: Adam Page, Copyright (c) 2022.
 * Portable migration of the Ambiq-authored MPU6050 driver. */
#ifndef NSX_SENSORS_MPU6050_H
#define NSX_SENSORS_MPU6050_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <nsx/sensors/bus.h>
#include <nsx/sensors/runtime.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef enum {
    MPU6050_DLPF_260HZ = 0, // Accel: 260 Hz, Gyro: 256 Hz, Delay: 1 ms
    MPU6050_DLPF_184HZ = 1, // Accel: 184 Hz, Gyro: 188 Hz, Delay: 2 ms
    MPU6050_DLPF_094HZ = 2, // Accel:  94 Hz, Gyro:  98 Hz, Delay: 3 ms
    MPU6050_DLPF_044HZ = 3, // Accel:  44 Hz, Gyro:  42 Hz, Delay: 5 ms
    MPU6050_DLPF_021HZ = 4, // Accel:  21 Hz, Gyro:  20 Hz, Delay: 9 ms
    MPU6050_DLPF_010HZ = 5, // Accel:  10 Hz, Gyro:  10 Hz, Delay: 14 ms
    MPU6050_DLPF_005HZ = 6, // Accel:   5 Hz, Gyro:   5 Hz, Delay: 19 ms

} mpu6050_dlpf_cfg_t;

/*! Gyroscope full-scale range */
typedef enum {
    MPU6050_GYRO_FS_250DPS = 0,  //!< +/- 250 º/s  -> 131 LSB/(º/s)
    MPU6050_GYRO_FS_500DPS = 1,  //!< +/- 500 º/s  -> 65.5 LSB/(º/s)
    MPU6050_GYRO_FS_1000DPS = 2, //!< +/- 1000 º/s -> 32.8 LSB/(º/s)
    MPU6050_GYRO_FS_2000DPS = 3  //!< +/- 2000 º/s -> 16.4 LSB/(º/s)
} mpu6050_gyro_fs_t;

/*! Accel full-scale range */
typedef enum {
    MPU6050_ACCEL_FS_2G = 0, //!< +/- 2 g  -> 16384 LSB/g
    MPU6050_ACCEL_FS_4G = 1, //!< +/- 4 g  -> 8192 LSB/g
    MPU6050_ACCEL_FS_8G = 2, //!< +/- 8 g  -> 4096 LSB/g
    MPU6050_ACCEL_FS_16G = 3 //!< +/- 16 g -> 2048 LSB/g
} mpu6050_accel_fs_t;

/*! Clock selection */
typedef enum {
    MPU6050_CLOCK_INTERNAL = 0,  // Internal 8 MHz oscillator
    MPU6050_CLOCK_GX_PLL = 1,    // Gyroscope x axis PLL
    MPU6050_CLOCK_GY_PLL = 2,    // Gyroscope y axis PLL
    MPU6050_CLOCK_GZ_PLL = 3,    // Gyroscope z axis PLL
    MPU6050_CLOCK_32_REF = 4,    // PLL with external 32.768kHz reference
    MPU6050_CLOCK_19_REF = 5,    // PLL with external 19.2MHz reference
    MPU6050_CLOCK_KEEP_RESET = 7 //!< Stops the clock and keeps timing generator in reset
} mpu6050_clock_src_t;

/*! FIFO configuration */
typedef struct {
    uint8_t tempEnable;
    uint8_t xgEnable;
    uint8_t ygEnable;
    uint8_t zgEnable;
    uint8_t accelEnable;
    uint8_t slv2Enable;
    uint8_t slv1Enable;
    uint8_t slv0Enable;
} mpu6050_fifo_config_t;

/*! Interrupt configuration */
typedef struct {
    uint8_t intLevel;
    uint8_t intOpen;
    uint8_t latchEnable;
    uint8_t rdClear;
    uint8_t fsyncLevel;
    uint8_t fsyncEnable;
} mpu6050_int_config_t;

#define MPU6050_I2C_ADDRESS_AD0_LOW 0x68
#define MPU6050_I2C_ADDRESS_AD0_HIGH 0x69


typedef struct {
    mpu6050_clock_src_t clock_src;
    mpu6050_dlpf_cfg_t dlpf_cfg;
    mpu6050_gyro_fs_t gyro_fullscale_range;
    mpu6050_accel_fs_t accel_fullscale_range;
    uint8_t sample_rate_divider; /* Rate = (DLPF == 0 ? 8000 : 1000)/(1+divider). */
} mpu6050_config_t;

enum {
    MPU6050_DATA_ACCEL = 1, MPU6050_DATA_TEMP = 2,
    MPU6050_DATA_GYRO_X = 4, MPU6050_DATA_GYRO_Y = 8, MPU6050_DATA_GYRO_Z = 16,
    MPU6050_DATA_ALL = 31,
    MPU6050_INTERRUPT_DATA_READY = 1, MPU6050_INTERRUPT_FIFO_OVERFLOW = 16
};
typedef struct {
    int16_t accel[3];
    int16_t temperature;
    int16_t gyro[3];
    uint8_t valid; /* MPU6050_DATA_* mask; absent channels are zero. */
} mpu6050_raw_sample_t;
typedef struct {
    float accel_g[3];
    float temperature_c;
    float gyro_dps[3];
    uint8_t valid;
} mpu6050_sample_t;
typedef struct {
    nsx_sensor_bus_t bus;
    nsx_sensor_runtime_t runtime;
    mpu6050_config_t _config; /* Successfully applied settings, not hardware readback. */
    uint8_t _fifo_mask;
    bool _bound, _configured, _sleeping, _lowpower, _temp_disabled;
    bool _fifo_enabled, _fifo_valid, _fifo_aligned, _any_read_clear;
} mpu6050_context_t;

/* Caller owns contexts/descriptors. Serialize complete operations per device.
 * Bus/runtime user storage must outlive ctx. No heap/global device state.
 * All scalar/sample outputs are unchanged on failure. Batch counts are the
 * exception: they report only successfully published data before an error. */
mpu6050_config_t mpu6050_default_config(void);
nsx_sensor_status_t mpu6050_validate_config(const mpu6050_config_t *config);
/* Bind is pure, I2C only, TX >=3 / RX >=14; runtime may be NULL for probe.
 * Successful init/reset and timed operations require delay_us. now_us is optional:
 * without it deadlines bound requested delays only, not transport/oversleep time.
 * HAL calls cannot be preempted. They must have their own finite timeout. */
nsx_sensor_status_t mpu6050_bind(mpu6050_context_t *ctx, const nsx_sensor_bus_t *bus,
                                const nsx_sensor_runtime_t *runtime);
nsx_sensor_status_t mpu6050_probe(mpu6050_context_t *ctx, uint8_t *identity);
/* Reset (200 ms budget), configure, disable FIFO/interrupts, leave asleep.
 * Start explicitly with set_sleep(ctx, 0). Calibration is never implicit.
 * An uncertain configuration write requires init to recover cached state. */
nsx_sensor_status_t mpu6050_init(mpu6050_context_t *ctx, const mpu6050_config_t *config);
/* Invalidate state before reset; timeout >=100000 us includes a 100 ms settling
 * delay, then bounded reset-bit polling. Does not resume sampling. */
nsx_sensor_status_t mpu6050_device_reset(mpu6050_context_t *ctx, uint32_t timeout_us);
nsx_sensor_status_t mpu6050_set_sleep(mpu6050_context_t *ctx, uint8_t sleep);
nsx_sensor_status_t mpu6050_set_clock_source(mpu6050_context_t *ctx, mpu6050_clock_src_t clock);
nsx_sensor_status_t mpu6050_set_temperature_disable(mpu6050_context_t *ctx, uint8_t disable);
/* Low-power mode starts accel cycling explicitly. Normal operation is restored
 * by init (idle), then wake. FIFO must be disabled before entering this mode.
 * wake_frequency 0/1/2/3 selects 1.25/5/20/40 Hz. Gyro/temp become invalid. */
nsx_sensor_status_t mpu6050_set_lowpower_accel_mode(mpu6050_context_t *ctx, uint8_t wake_frequency);
nsx_sensor_status_t mpu6050_set_lowpass_filter(mpu6050_context_t *ctx, mpu6050_dlpf_cfg_t filter);
nsx_sensor_status_t mpu6050_set_gyro_full_scale(mpu6050_context_t *ctx, mpu6050_gyro_fs_t range);
nsx_sensor_status_t mpu6050_set_accel_full_scale(mpu6050_context_t *ctx, mpu6050_accel_fs_t range);
nsx_sensor_status_t mpu6050_set_sample_rate_divider(mpu6050_context_t *ctx, uint8_t divider);
/* Exact integer Hz only; reject rates not representable by current DLPF/divider.
 * DLPF changes preserve the divider, so may change rate. No silent clamping. */
nsx_sensor_status_t mpu6050_set_sample_rate(mpu6050_context_t *ctx, uint16_t hz);
nsx_sensor_status_t mpu6050_get_sample_rate(mpu6050_context_t *ctx, float *hz); /* Normal-mode hardware readback. */

/* Coherent 14-byte burst; no freshness wait. Awake initialized context required.
 * Raw bytes retain sensor register order; buffer must have capacity >=14. */
nsx_sensor_status_t mpu6050_read_sensors(mpu6050_context_t *ctx, uint8_t *buffer, size_t capacity);
nsx_sensor_status_t mpu6050_read_raw(mpu6050_context_t *ctx, mpu6050_raw_sample_t *sample);
nsx_sensor_status_t mpu6050_read_sample(mpu6050_context_t *ctx, mpu6050_sample_t *sample);
nsx_sensor_status_t mpu6050_convert_sample(const mpu6050_raw_sample_t *raw,
    mpu6050_accel_fs_t accel_range, mpu6050_gyro_fs_t gyro_range, mpu6050_sample_t *sample);
nsx_sensor_status_t mpu6050_get_accel_values(mpu6050_context_t *ctx, int16_t *x, int16_t *y, int16_t *z);
nsx_sensor_status_t mpu6050_get_gyro_values(mpu6050_context_t *ctx, int16_t *x, int16_t *y, int16_t *z);
nsx_sensor_status_t mpu6050_get_temperature(mpu6050_context_t *ctx, int16_t *temperature);
nsx_sensor_status_t mpu6050_accel_fsr_value(mpu6050_accel_fs_t range, uint16_t *value);
nsx_sensor_status_t mpu6050_gyro_fsr_value(mpu6050_gyro_fs_t range, uint16_t *value);
nsx_sensor_status_t mpu6050_accel_resolution(mpu6050_accel_fs_t range, float *value);
nsx_sensor_status_t mpu6050_gyro_resolution(mpu6050_gyro_fs_t range, float *value);
nsx_sensor_status_t mpu6050_accel_to_gravity(int16_t raw, mpu6050_accel_fs_t range, float *value);
nsx_sensor_status_t mpu6050_gyro_to_deg_per_sec(int16_t raw, mpu6050_gyro_fs_t range, float *value);
nsx_sensor_status_t mpu6050_temperature_to_celsius(int16_t raw, float *value);

/* All FIFO format/range/rate changes require a FIFO reset before reading again.
 * configure_fifo disables FIFO first; reset_fifo leaves it disabled; enable
 * explicitly afterwards. Native frame order is accel XYZ, temp, gyro XYZ.
 * Slave flags are retained for raw FIFO users; auxiliary bus setup is outside
 * this driver. Typed reads reject slave channels because their lengths vary. */
nsx_sensor_status_t mpu6050_configure_fifo(mpu6050_context_t *ctx, const mpu6050_fifo_config_t *config);
nsx_sensor_status_t mpu6050_set_fifo_enable(mpu6050_context_t *ctx, uint8_t enable);
nsx_sensor_status_t mpu6050_reset_fifo(mpu6050_context_t *ctx);
nsx_sensor_status_t mpu6050_get_fifo_count(mpu6050_context_t *ctx, uint16_t *count);
/* FIFO must be enabled for typed, byte, or word consumption.
 * Snapshot reads, bounded by available bytes and caller capacity. Empty FIFO or
 * no complete frame returns AGAIN. Overflow or ambiguous consumption on failure
 * requires reset_fifo; no automatic retry. Complete frames are read in bursts
 * of up to 14 bytes; a failed burst publishes none of its frames.
 * Published frames remain usable, but
 * transport errors may consume additional unknown bytes. INT_STATUS reads clear
 * flags; the driver remembers observed overflow until reset. rdClear=1 is rejected
 * while FIFO is in use because unrelated reads could erase overflow evidence. */
nsx_sensor_status_t mpu6050_read_fifo(mpu6050_context_t *ctx, mpu6050_raw_sample_t *frames,
                                    size_t capacity, size_t *completed);
nsx_sensor_status_t mpu6050_read_fifo_bytes(mpu6050_context_t *ctx, uint8_t *bytes,
                                          size_t capacity, size_t *completed);
/* Read exactly one signed word from FIFO_R_W. Mixing word/raw access with typed
 * frames requires reset_fifo first (frame alignment is no longer known). */
nsx_sensor_status_t mpu6050_fifo_pop(mpu6050_context_t *ctx, int16_t *value);

nsx_sensor_status_t mpu6050_configure_interrupt(mpu6050_context_t *ctx, const mpu6050_int_config_t *config);
nsx_sensor_status_t mpu6050_set_interrupt_enable(mpu6050_context_t *ctx, uint8_t overflow, uint8_t ready);
/* Consumes read-to-clear interrupt status. Any-read clearing, if configured,
 * also makes all register reads consuming; do not enable it for FIFO use. */
nsx_sensor_status_t mpu6050_get_interrupt_status(mpu6050_context_t *ctx, uint8_t *status);
nsx_sensor_status_t mpu6050_reset_signal_paths(mpu6050_context_t *ctx);
nsx_sensor_status_t mpu6050_reset_signal_conds(mpu6050_context_t *ctx);

/* Signed offset register units. Accel setter preserves factory compensation
 * bit 0; getter masks it off. Gyro offsets are signed 16-bit words. */
nsx_sensor_status_t mpu6050_set_accel_offset(mpu6050_context_t *ctx, uint8_t axis, int16_t offset);
nsx_sensor_status_t mpu6050_set_gyro_offset(mpu6050_context_t *ctx, uint8_t axis, int16_t offset);
nsx_sensor_status_t mpu6050_get_accel_offset(mpu6050_context_t *ctx, uint8_t axis, int16_t *offset);
nsx_sensor_status_t mpu6050_get_gyro_offset(mpu6050_context_t *ctx, uint8_t axis, int16_t *offset);
/* Fresh DRDY-paced burst samples, no FIFO or low-power mode. Discards stale
 * DRDY before each sample wait, so conversions may be skipped but a consumed
 * sample is not accepted again from a pending flag. samples=1..4096; bounded by
 * timeout and finite sample count. */
nsx_sensor_status_t mpu6050_mean_sensors(mpu6050_context_t *ctx, uint16_t samples,
                                       uint32_t timeout_us, mpu6050_raw_sample_t *mean);
typedef struct {
    uint16_t samples, max_iterations;
    uint16_t accel_deadzone, gyro_deadzone;
    uint32_t timeout_us;
} mpu6050_calibration_config_t;
typedef struct { int16_t accel[3], gyro[3]; uint16_t iterations; } mpu6050_offsets_t;
mpu6050_calibration_config_t mpu6050_default_calibration_config(void);
/* Explicit, bounded stationary +Z-up calibration, +/-2g and +/-250dps only.
 * Must already be awake in normal mode with FIFO disabled and DLPF enabled. Assumes physical
 * stillness; cannot verify it. Preserves +1g on Z. Failure may leave updated
 * offsets; result is unchanged. Restore saved offsets or init/reset to recover. */
nsx_sensor_status_t mpu6050_calibrate(mpu6050_context_t *ctx,
    const mpu6050_calibration_config_t *config, mpu6050_offsets_t *result);
#ifdef __cplusplus
}
#endif
#endif
