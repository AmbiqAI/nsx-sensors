/*
 *
 * Copyright (c) [2020] by InvenSense, Inc.
 * 
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 */

/** @defgroup Transport Transport
 *  @brief    Abstraction layer to communicate with device.
 *  @{
 */

/** @file inv_imu_transport.h */

#ifndef _INV_IMU_TRANSPORT_H_
#define _INV_IMU_TRANSPORT_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <nsx/sensors/detail/icm45605_types.h>

/* Callback and transport types come from the shared device-specific header. */

/* Available serial interface type. */
#define UI_I2C  0 /**< identifies I2C interface. */
#define UI_SPI4 1 /**< identifies 4-wire SPI interface. */
#define UI_SPI3 2 /**< identifies 3-wire SPI interface. */

/** @brief Serif type definition.
 *  @deprecated Kept for retrocompatibility. Replaced with `uint32_t` type
 *              in `inv_imu_transport_t` struct.
 */
typedef uint32_t nsx_icm45605_tdk_serif_type_t;


/** @brief Reads data from a register on IMU.
 *  @param[in] t     Pointer to transport (as void * so it can be called from any module).
 *  @param[in] reg   Register address to be read.
 *  @param[in] len   Number of byte to be read.
 *  @param[out] buf  Output data from the register.
 *  @return          0 on success, negative value on error.
 */
int nsx_icm45605_tdk_read_reg(void *t, uint32_t reg, uint32_t len, uint8_t *buf);

/** @brief Writes data to a register on IMU.
 *  @param[in] t    Pointer to transport (as void * so it can be called from any module).
 *  @param[in] reg  Register address to be written.
 *  @param[in] len  Number of byte to be written.
 *  @param[in] buf  Input data to write.
 *  @return         0 on success, negative value on error.
 */
int nsx_icm45605_tdk_write_reg(void *t, uint32_t reg, uint32_t len, const uint8_t *buf);

/** @brief Reads data from SRAM on IMU.
 *  @param[in] t     Pointer to transport (as void * so it can be called from any module). 
 *  @param[in] addr  Address to be read.
 *  @param[in] len   Number of byte to be read.
 *  @param[out] buf  Output data from the register.
 *  @return          0 on success, negative value on error.
 */
int nsx_icm45605_tdk_read_sram(void *t, uint32_t addr, uint32_t len, uint8_t *buf);

/** @brief Writes data to SRAM on IMU.
 *  @param[in] t     Pointer to transport (as void * so it can be called from any module).
 *  @param[in] addr  Address to be written.
 *  @param[in] len   Number of byte to be written.
 *  @param[in] buf   Input data to write.
 *  @return          0 on success, negative value on error.
 */
int nsx_icm45605_tdk_write_sram(void *t, uint32_t addr, uint32_t len, const uint8_t *buf);

#ifdef __cplusplus
}
#endif

#endif /* _INV_IMU_TRANSPORT_H_ */

/** @} */
