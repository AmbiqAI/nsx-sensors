/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef NSX_SENSORS_BUS_H
#define NSX_SENSORS_BUS_H
#include <stddef.h>
#include <stdint.h>
#include <nsx/sensors/status.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef enum { NSX_SENSOR_BUS_I2C, NSX_SENSOR_BUS_SPI } nsx_sensor_bus_kind_t;
typedef struct {
    const uint8_t *tx;
    size_t tx_size;
    uint8_t *rx;
    size_t rx_size;
} nsx_sensor_transfer_t;
/* Synchronous: buffers are borrowed only until return. A zero-length phase
 * ignores its pointer (NULL is allowed); a nonzero phase requires a buffer.
 * At least one phase must be nonempty. Both phases nonempty
 * mean repeated START without STOP on I2C, or CS held throughout on SPI.
 * SPI discards RX during TX; the adapter documents RX MOSI behavior (fill byte or hardware-defined).
 * Never split a transfer or substitute STOP-separated reads. Unsupported
 * operations fail before I/O. Failures may consume data/change device state.
 * Endpoint bindings own addresses/CS, locking, native errors and bounded HAL
 * completion. Caller serializes whole driver operations on each device. */
typedef struct {
    nsx_sensor_status_t (*transfer)(void *user, const nsx_sensor_transfer_t *transfer);
} nsx_sensor_bus_ops_t;
typedef struct {
    const nsx_sensor_bus_ops_t *ops;
    void *user;
    nsx_sensor_bus_kind_t kind;
    size_t max_tx_size; /* Inclusive limit; zero means this direction unsupported. */
    size_t max_rx_size;
} nsx_sensor_bus_t;
nsx_sensor_status_t nsx_sensor_bus_validate(const nsx_sensor_bus_t *bus);
nsx_sensor_status_t nsx_sensor_bus_transfer(const nsx_sensor_bus_t *bus,
                                          const nsx_sensor_transfer_t *transfer);
#ifdef __cplusplus
}
#endif
#endif
