/**
 * @file nsx_icm45605.h
 * @author Ambiq
 * @brief Simple driver for TDK ICM-45605 6-axis IMU (SPI)
 * @version 0.1
 * @date 2025-05-16
 *
 * @copyright Copyright (c) 2025
 *
 *  \addtogroup ns-ICM45605
 *  @{
 *  @ingroup ns-spi
 */

#ifndef NSX_ICM45605_H
#define NSX_ICM45605_H

#include "nsx_spi.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { ICM45605_STATUS_SUCCESS = 0, ICM45605_STATUS_ERROR = 1 } icm45605_status_e;

/*! 6DOF sensor sample in natural units (g, dps, degC) */
typedef struct {
    float accel_g[3];
    float gyro_dps[3];
    float temp_degc;
} icm45605_sensor_data_t;

/*!
 * ICM-45605 device context.
 *
 * Single-instance contract: this driver keeps the TDK `inv_imu_device_t`
 * instance and its SPI transport binding in file-static storage, so exactly
 * one ICM-45605 may be active per firmware image. `icm45605_init()` rebinds
 * that shared state to the supplied context, and the most recent successful
 * `icm45605_init()` wins. Initializing a second context is not supported in
 * `v0.1.0`.
 *
 * This driver does not buffer samples. Callers own sample storage, framing,
 * and any batching policy.
 */
typedef struct {
    nsx_spi_config_t *spi_config; // Pre-configured SPI bus (caller owns init)
    uint32_t cs_pin;              // Chip-select pin used for SPI transactions

    // IMU Configuration
    uint32_t accel_fsr;
    uint32_t gyro_fsr;
    uint32_t accel_odr;
    uint32_t gyro_odr;
    uint32_t accel_ln_bw;
    uint32_t gyro_ln_bw;
    uint32_t calibrate;              // true to calibrate the IMU during init
    uint32_t enable_drdy_interrupt;  // true to configure the INT2 data-ready interrupt in init

    // Internal state
    void *imu_dev_handle; // TDK inv_imu_device_t handle
    uint32_t calibrated;
    float accel_bias[3]; // bias to subtract from accel_g
    float gyro_bias[3];  // bias to subtract from gyro_dps
} icm45605_context_t;

/**
 * @brief Initialize the ICM-45605 (soft reset, WHOAMI check, FSR/ODR/bandwidth config,
 *        optional calibration and interrupt setup). Caller must have already initialized
 *        ctx->spi_config via nsx_spi_interface_init() and configured any GPIO/interrupt
 *        pin wiring.
 *
 * @note Binds the shared single-instance device state to @p ctx. See
 *       ::icm45605_context_t for the single-instance contract.
 *
 * @param ctx Device context
 * @return uint32_t status
 */
uint32_t icm45605_init(icm45605_context_t *ctx);

/**
 * @brief Retrieve 6DOF data from the ICM-45605 in natural units (g, dps, degC)
 *
 * @param ctx Device context
 * @param data Data output
 * @return uint32_t status
 */
uint32_t icm45605_get_data(icm45605_context_t *ctx, icm45605_sensor_data_t *data);

/**
 * @brief Retrieve raw (unscaled) 6DOF data from the ICM-45605
 *
 * @param ctx Device context
 * @param data Data output (raw LSB values)
 * @return uint32_t status
 */
uint32_t icm45605_get_raw_data(icm45605_context_t *ctx, icm45605_sensor_data_t *data);

/**
 * @brief Configure the data-ready interrupt on INT2. Caller is responsible for
 *        configuring the corresponding GPIO/NVIC wiring.
 *
 * @param ctx Device context
 * @return uint32_t status
 */
uint32_t icm45605_configure_interrupts(icm45605_context_t *ctx);

/**
 * @brief Check and clear the data-ready interrupt status.
 *
 * Context-free by design so it can be called directly from an ISR. It always
 * targets the single active device bound by the most recent successful
 * ::icm45605_init call; see ::icm45605_context_t.
 *
 * @return uint32_t 1 if data-ready interrupt is set, 0 otherwise
 */
uint32_t icm45605_handle_interrupt(void);

/**
 * @brief Calibrate the ICM-45605 by averaging samples while stationary. Device must be
 *        still and level (accel should read [0,0,1g], gyro [0,0,0]).
 *
 * @param ctx Device context
 * @return uint32_t status
 */
uint32_t icm45605_calibrate(icm45605_context_t *ctx);

#ifdef __cplusplus
}
#endif
/** @}*/
#endif // NSX_ICM45605_H
