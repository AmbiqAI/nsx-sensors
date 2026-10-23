/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 Ambiq Micro, Inc.
 */
#ifndef NSX_BME688_NSX_H
#define NSX_BME688_NSX_H
#include "nsx_bme688.h"
#include "nsx_i2c.h"
#include "nsx_spi.h"
#ifdef __cplusplus
extern "C" {
#endif

/** Caller-owned bindings; keep alive at a stable address while ctx is in use.
 * Initialize the NSX bus first. Supply a delay hook (which may sleep/yield).
 * Each adapter retains its latest raw NSX transfer status.
 */
typedef struct {
    nsx_i2c_config_t *bus;
    uint16_t address; /**< 7-bit 0x76 or 0x77. */
    void (*delay_us)(void *user, uint32_t duration_us);
    void *timing_user;
    uint32_t transport_status;
} bme688_nsx_i2c_t;

typedef struct {
    nsx_spi_config_t *bus; /**< 4-wire SPI mode 0 or 3, <=10 MHz. */
    uint32_t chip_select;
    void (*delay_us)(void *user, uint32_t duration_us);
    void *timing_user;
    uint32_t transport_status;
} bme688_nsx_spi_t;

bme688_status_t bme688_init_i2c(bme688_context_t *ctx, bme688_nsx_i2c_t *binding);
bme688_status_t bme688_init_spi(bme688_context_t *ctx, bme688_nsx_spi_t *binding);
#ifdef __cplusplus
}
#endif
#endif
