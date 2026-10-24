/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 Ambiq Micro, Inc.
 */
#include "nsx_bme688_nsx.h"
#include <string.h>

/* Blocking NSX IOM APIs pass buffers as uint32_t pointers. Keep both base
 * alignment and final-word padding even for one-byte transfers. Core reads
 * at most 23 bytes (the first calibration bank). */
#define READ_BUFFER_SIZE 24

static uint32_t i2c_read(void *user, uint8_t reg, uint8_t *data, size_t length)
{
    bme688_nsx_i2c_t *binding = user;
    _Alignas(uint32_t) uint8_t command[4] = { reg };
    _Alignas(uint32_t) uint8_t scratch[READ_BUFFER_SIZE] = { 0 };
    if (length > sizeof(scratch)) return 1;
    binding->transport_status = nsx_i2c_write_read(binding->bus, binding->address,
                                                  command, 1, scratch, length);
    if (binding->transport_status == 0) memcpy(data, scratch, length);
    return binding->transport_status;
}

static uint32_t i2c_write(void *user, uint8_t reg, const uint8_t *data, size_t length)
{
    bme688_nsx_i2c_t *binding = user;
    _Alignas(uint32_t) uint8_t packet[4] = { reg, data[0] };
    (void)length; /* Core contract: exactly one register/value per write. */
    binding->transport_status = nsx_i2c_write(binding->bus, packet, 2, binding->address);
    return binding->transport_status;
}

static void i2c_delay(void *user, uint32_t duration)
{
    bme688_nsx_i2c_t *binding = user;
    binding->delay_us(binding->timing_user, duration);
}

static uint32_t spi_read(void *user, uint8_t reg, uint8_t *data, size_t length)
{
    bme688_nsx_spi_t *binding = user;
    _Alignas(uint32_t) uint8_t scratch[READ_BUFFER_SIZE] = { 0 };
    if (length > sizeof(scratch)) return 1;
    binding->transport_status = nsx_spi_read(binding->bus, scratch, (uint32_t)length, reg, 1,
                                             binding->chip_select);
    if (binding->transport_status == 0) memcpy(data, scratch, length);
    return binding->transport_status;
}

static uint32_t spi_write(void *user, uint8_t reg, const uint8_t *data, size_t length)
{
    bme688_nsx_spi_t *binding = user;
    _Alignas(uint32_t) uint8_t scratch[4] = { data[0] };
    (void)length;
    binding->transport_status = nsx_spi_write(binding->bus, scratch, 1, reg, 1, binding->chip_select);
    return binding->transport_status;
}

static void spi_delay(void *user, uint32_t duration)
{
    bme688_nsx_spi_t *binding = user;
    binding->delay_us(binding->timing_user, duration);
}

bme688_status_t bme688_init_i2c(bme688_context_t *ctx, bme688_nsx_i2c_t *binding)
{
    bme688_transport_t transport = { i2c_read, i2c_write, i2c_delay, binding, BME688_I2C };
    if (binding == NULL || binding->bus == NULL || binding->delay_us == NULL ||
        (binding->address != 0x76 && binding->address != 0x77)) return BME688_E_ARGUMENT;
    return bme688_init(ctx, &transport);
}

bme688_status_t bme688_init_spi(bme688_context_t *ctx, bme688_nsx_spi_t *binding)
{
    bme688_transport_t transport = { spi_read, spi_write, spi_delay, binding, BME688_SPI };
    if (binding == NULL || binding->bus == NULL || binding->delay_us == NULL) return BME688_E_ARGUMENT;
    return bme688_init(ctx, &transport);
}
