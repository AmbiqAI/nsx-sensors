/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef NSX_SENSORS_PORT_ZEPHYR_H
#define NSX_SENSORS_PORT_ZEPHYR_H
#include <nsx/sensors/bus.h>
#include <nsx/sensors/runtime.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/spi.h>
#ifdef __cplusplus
extern "C" {
#endif
#define NSX_SENSOR_ZEPHYR_MAX_TRANSFER 256U
/* Caller-owned stable-address endpoint. Bind copies the DT spec; controller,
 * GPIO devices and SPI configuration dependencies must outlive it. Serialize
 * whole device operations, including access to scratch and last_native_status.
 * Zephyr controllers serialize physical bus access. Calls are synchronous;
 * the selected controller must provide finite transfer completion. Timeouts
 * are the controller driver's responsibility (no portable Zephyr override). */
typedef struct {
    nsx_sensor_bus_t bus;
    struct i2c_dt_spec _i2c;
    struct spi_dt_spec _spi;
    int last_native_status; /* Last I/O result; validation does not overwrite it. */
    uint8_t _tx[2 * NSX_SENSOR_ZEPHYR_MAX_TRANSFER];
    uint8_t _rx[2 * NSX_SENSOR_ZEPHYR_MAX_TRANSFER];
} nsx_sensor_zephyr_context_t;
/* Pure bindings (no I/O/readiness probe). Failed binding leaves ctx unchanged.
 * 7-bit I2C 0x08..0x77; SPI master/full duplex/8-bit/single-line/Motorola only, without
 * HOLD_ON_CS or LOCK_ON. Combined I2C uses repeated START and final STOP;
 * SPI uses one transceive and sends zero bytes during the RX phase.
 * Each direction is bounded to MAX_TRANSFER. RX is unchanged on failure.
 * Invoke in thread context; application must check controller readiness. */
nsx_sensor_status_t nsx_sensor_zephyr_bind_i2c(nsx_sensor_zephyr_context_t *ctx,
                                            const struct i2c_dt_spec *spec);
nsx_sensor_status_t nsx_sensor_zephyr_bind_spi(nsx_sensor_zephyr_context_t *ctx,
                                            const struct spi_dt_spec *spec);
nsx_sensor_status_t nsx_sensor_zephyr_status(int error);
int nsx_sensor_zephyr_errno(nsx_sensor_status_t status);
nsx_sensor_runtime_t nsx_sensor_zephyr_runtime(void);
#ifdef __cplusplus
}
#endif
#endif
