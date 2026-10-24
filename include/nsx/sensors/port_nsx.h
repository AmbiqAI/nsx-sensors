/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef NSX_SENSORS_PORT_NSX_H
#define NSX_SENSORS_PORT_NSX_H
#include <nsx/sensors/bus.h>
#include <nsx_i2c.h>
#include <nsx_spi.h>
#ifdef __cplusplus
extern "C" {
#endif

/* A lock protects the entire IOM, including other endpoints and DMA users.
 * NULL hooks mean the application guarantees exclusive bus access. Supply both
 * hooks or neither. acquire returns OK only when held; release cannot fail.
 * Device operations still require serialization above the bus level. */
typedef struct {
    void *user;
    nsx_sensor_status_t (*acquire)(void *user);
    void (*release)(void *user);
} nsx_sensor_nsx_lock_t;

/* Caller-owned, stable-address context: do not copy/move after binding a driver.
 * Configuration and scratch are private after bind. The SDK configuration must
 * outlive this context and remain initialized with compatible fixed settings.
 * last_sdk_status is the last invoked SDK helper result (not always a native HAL
 * code); validation/lock failures leave it unchanged. Read under serialization. */
typedef struct {
    nsx_sensor_bus_t bus;
    nsx_sensor_nsx_lock_t lock;
    nsx_i2c_config_t *_i2c;
    nsx_spi_config_t *_spi;
    uint16_t _address;
    uint32_t _chip_select;
    uint32_t last_sdk_status;
    uint32_t _scratch[64];
} nsx_sensor_nsx_context_t;

/* Pure bindings, no I/O or controller initialization. Failed binds leave ctx
 * unchanged. I2C uses unshifted addresses 0x08..0x77. SPI chip_select is the
 * hardware IOM nCE selector (0..3), NOT a GPIO pin. lock may be NULL.
 * Transfers support up to sizeof(_scratch) bytes per direction. Combined TX
 * prefixes are additionally limited to AM_HAL_IOM_MAX_OFFSETSIZE bytes.
 * SPI RX uses the hardware read phase: MOSI is not a selectable fill byte.
 * Devices requiring a defined dummy byte need a different adapter. */
nsx_sensor_status_t nsx_sensor_nsx_bind_i2c(nsx_sensor_nsx_context_t *ctx,
    nsx_i2c_config_t *config, uint16_t address, const nsx_sensor_nsx_lock_t *lock);
nsx_sensor_status_t nsx_sensor_nsx_bind_spi(nsx_sensor_nsx_context_t *ctx,
    nsx_spi_config_t *config, uint32_t chip_select, const nsx_sensor_nsx_lock_t *lock);
#ifdef __cplusplus
}
#endif
#endif
