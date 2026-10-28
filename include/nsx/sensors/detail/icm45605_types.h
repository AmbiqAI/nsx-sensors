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

/* NSX private typed storage, shared by the public context and vendor core.
 * Implementation detail: applications must not access these fields directly. */
#ifndef NSX_SENSORS_DETAIL_ICM45605_TYPES_H
#define NSX_SENSORS_DETAIL_ICM45605_TYPES_H
#include <stdint.h>
typedef int (*nsx_icm45605_tdk_read_reg_t)(void *user, uint8_t reg, uint8_t *buf, uint32_t len);

typedef int (*nsx_icm45605_tdk_write_reg_t)(void *user, uint8_t reg, const uint8_t *buf, uint32_t len);

typedef struct {
	/* NSX local patch: caller-owned instance passed to every callback. */
	void *user;
	/* Serial interface variables (should be initialized by application) */
	nsx_icm45605_tdk_read_reg_t  read_reg; /**< Function pointer to read register(s). */
	nsx_icm45605_tdk_write_reg_t write_reg; /**< Function pointer to write register(s). */
	uint32_t            serif_type; /**< Serial interface type. */

	/** @brief Callback to sleep function.
	 *  @param[in] us  Time to sleep in microseconds.
	 */
	void (*sleep_us)(void *user, uint32_t us);
} nsx_icm45605_tdk_transport_t;

typedef struct {
	/** @brief Transport structure */
	nsx_icm45605_tdk_transport_t transport;

	/** @brief The calculated FIFO frame size in Bytes. */
	uint8_t fifo_frame_size;

	/** @brief Keeps track of data endianness mode 
	 * 0 : data in Sensor Registers and FIFO are in Little Endian format
	 * 1 : data in Sensor Registers and FIFO are in Big Endian format
	 */
	uint8_t endianness_data;

	/** @brief Memory area reserved for advanced module.
	 *         (only required when using advanced feature set).
	 *  @note  In case advanced module is not used, this field can be removed.
	 */
	uint64_t adv_var[6];

} nsx_icm45605_tdk_device_t;
#endif
