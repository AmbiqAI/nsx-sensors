/**
 * @file nsx_icm45605.c
 * @author Ambiq
 * @brief Driver for TDK ICM-45605 6-axis IMU (SPI)
 * @version 0.1
 * @date 2025-05-16
 *
 * @copyright Copyright (c) 2025
 *
 */

#include "nsx_icm45605.h"
#include "nsx_core.h"
#include "inv_imu_driver.h"
#include <string.h>

// Internal state - the TDK driver only supports a single active device handle
// per transport callback set, matching the legacy ns-imu behavior.
static inv_imu_device_t icm45605_dev;
static nsx_spi_config_t *icm45605_spi_config;
static uint32_t icm45605_cs_pin;

static int icm45605_read_reg(uint8_t reg, uint8_t *data, uint32_t len) {
    return nsx_spi_read(icm45605_spi_config, data, len, reg | 0x80, 1, icm45605_cs_pin);
}

static int icm45605_write_reg(uint8_t reg, const uint8_t *data, uint32_t len) {
    return nsx_spi_write(icm45605_spi_config, data, len, reg, 1, icm45605_cs_pin);
}

static void icm45605_sleep_us(uint32_t us) {
    nsx_delay_us(us);
}

static float icm45605_accel_map[] = {32, 16, 8, 4, 2};
static float icm45605_gyro_map[] = {4000, 2000, 1000, 500, 250, 125, 62.5, 31.25, 15.625};

uint32_t icm45605_init(icm45605_context_t *ctx) {
    uint8_t whoami;
    uint32_t status = ICM45605_STATUS_SUCCESS;
    if (ctx == NULL) {
        return ICM45605_STATUS_ERROR;
    }

    /* Transport layer initialization */
    ctx->imu_dev_handle = &icm45605_dev;
    icm45605_spi_config = ctx->spi_config;
    icm45605_cs_pin = ctx->cs_pin;

    icm45605_dev.transport.read_reg = icm45605_read_reg;
    icm45605_dev.transport.write_reg = icm45605_write_reg;
    icm45605_dev.transport.sleep_us = icm45605_sleep_us;
    icm45605_dev.transport.serif_type = UI_SPI4;

    // Check that device is present
    status = inv_imu_get_who_am_i(&icm45605_dev, &whoami);
    if (whoami != INV_IMU_WHOAMI) {
        return ICM45605_STATUS_ERROR;
    }

    // Trigger soft-reset
    status |= inv_imu_soft_reset(&icm45605_dev);
    nsx_delay_us(1000);

    // Set FSR, ODR and bandwidth
    status |= inv_imu_set_accel_fsr(&icm45605_dev, ctx->accel_fsr);
    status |= inv_imu_set_gyro_fsr(&icm45605_dev, ctx->gyro_fsr);
    status |= inv_imu_set_accel_frequency(&icm45605_dev, ctx->accel_odr);
    status |= inv_imu_set_gyro_frequency(&icm45605_dev, ctx->gyro_odr);
    status |= inv_imu_set_accel_ln_bw(&icm45605_dev, ctx->accel_ln_bw);
    status |= inv_imu_set_gyro_ln_bw(&icm45605_dev, ctx->gyro_ln_bw);
    status |= inv_imu_select_accel_lp_clk(&icm45605_dev, SMC_CONTROL_0_ACCEL_LP_CLK_RCOSC);
    status |= inv_imu_set_accel_mode(&icm45605_dev, PWR_MGMT0_ACCEL_MODE_LN);
    status |= inv_imu_set_gyro_mode(&icm45605_dev, PWR_MGMT0_GYRO_MODE_LN);

    if (ctx->calibrate) {
        status |= icm45605_calibrate(ctx);
    } else {
        ctx->calibrated = 0;
    }

    // If callback is set, configure interrupt. Invoker is responsible for INT pin setup.
    if (ctx->frame_available_cb != NULL) {
        ctx->frame_size = ctx->frame_size ? ctx->frame_size : 1;
        status |= icm45605_configure_interrupts(ctx);
    }

    return status;
}

uint32_t icm45605_get_data(icm45605_context_t *ctx, icm45605_sensor_data_t *data) {
    inv_imu_sensor_data_t d;
    uint32_t status = ICM45605_STATUS_SUCCESS;
    if (ctx == NULL || data == NULL) {
        return ICM45605_STATUS_ERROR;
    }

    status = inv_imu_get_register_data(ctx->imu_dev_handle, &d);

    // Scale the data to natural units
    for (int i = 0; i < 3; i++) {
        data->accel_g[i] = (float)(d.accel_data[i] * icm45605_accel_map[ctx->accel_fsr]) / 32768;
        data->gyro_dps[i] = (float)(d.gyro_data[i] * icm45605_gyro_map[ctx->gyro_fsr]) / 32768;
        if (ctx->calibrated) {
            data->accel_g[i] -= ctx->accel_bias[i];
            data->gyro_dps[i] -= ctx->gyro_bias[i];
        }
    }
    data->temp_degc = (float)25 + ((float)d.temp_data / 128);
    return status;
}

uint32_t icm45605_get_raw_data(icm45605_context_t *ctx, icm45605_sensor_data_t *data) {
    inv_imu_sensor_data_t d;
    if (ctx == NULL || data == NULL) {
        return ICM45605_STATUS_ERROR;
    }
    if (ICM45605_STATUS_SUCCESS != inv_imu_get_register_data(ctx->imu_dev_handle, &d)) {
        return ICM45605_STATUS_ERROR;
    }
    // Copy raw data to the output structure
    for (int i = 0; i < 3; i++) {
        data->accel_g[i] = (float)d.accel_data[i]; // raw data in LSB
        data->gyro_dps[i] = (float)d.gyro_data[i]; // raw data in LSB
    }
    data->temp_degc = (float)d.temp_data; // raw data in LSB
    return ICM45605_STATUS_SUCCESS;
}

uint32_t icm45605_configure_interrupts(icm45605_context_t *ctx) {
    inv_imu_int_pin_config_t int_pin_config;
    inv_imu_int_state_t int_config;
    uint32_t status;
    if (ctx == NULL || ctx->imu_dev_handle == NULL) {
        return ICM45605_STATUS_ERROR;
    }
    int_pin_config.int_polarity = INTX_CONFIG2_INTX_POLARITY_HIGH;
    int_pin_config.int_mode = INTX_CONFIG2_INTX_MODE_PULSE;
    int_pin_config.int_drive = INTX_CONFIG2_INTX_DRIVE_PP;
    status = inv_imu_set_pin_config_int(ctx->imu_dev_handle, INV_IMU_INT2, &int_pin_config);
    if (status != ICM45605_STATUS_SUCCESS) {
        return status;
    }
    /* Interrupts configuration */
    memset(&int_config, INV_IMU_DISABLE, sizeof(int_config));
    int_config.INV_UI_DRDY = INV_IMU_ENABLE;
    status = inv_imu_set_config_int(ctx->imu_dev_handle, INV_IMU_INT2, &int_config);

    return status;
}

uint32_t icm45605_handle_interrupt(void) {
    inv_imu_int_state_t int_state;
    inv_imu_get_int_status(&icm45605_dev, INV_IMU_INT2, &int_state);
    return int_state.INV_UI_DRDY ? 1 : 0;
}

/**
 * Calibrate ICM-45605 by averaging 250 samples.
 * While stationary, accel should read [0,0,1g] and gyro [0,0,0].
 */
uint32_t icm45605_calibrate(icm45605_context_t *ctx) {
    uint32_t count = 0;
    double sum_acc[3] = {0, 0, 0};
    double sum_gyro[3] = {0, 0, 0};
    icm45605_sensor_data_t d;

    // Get rid of some garbage data
    for (int i = 0; i < 10; i++) {
        if (icm45605_get_data(ctx, &d) != ICM45605_STATUS_SUCCESS) {
            return ICM45605_STATUS_ERROR;
        }
        nsx_delay_us(20000); // 20ms delay to get 50Hz
    }

    while (count < 250) {
        if (icm45605_get_data(ctx, &d) != ICM45605_STATUS_SUCCESS) {
            return ICM45605_STATUS_ERROR;
        }
        sum_acc[0] += d.accel_g[0];
        sum_acc[1] += d.accel_g[1];
        sum_acc[2] += d.accel_g[2];
        sum_gyro[0] += d.gyro_dps[0];
        sum_gyro[1] += d.gyro_dps[1];
        sum_gyro[2] += d.gyro_dps[2];
        count++;
        nsx_delay_us(20000); // 20ms delay to get 50Hz
    }

    // compute and store biases
    for (int i = 0; i < 3; i++) {
        float avg_acc = sum_acc[i] / count;
        float avg_gyro = sum_gyro[i] / count;
        // X/Y accel bias = avg;  Z accel bias = (avg - 1 g)
        ctx->accel_bias[i] = avg_acc - (i == 2 ? 1.0f : 0.0f);
        ctx->gyro_bias[i] = avg_gyro;
    }
    ctx->calibrated = 1;
    return ICM45605_STATUS_SUCCESS;
}
