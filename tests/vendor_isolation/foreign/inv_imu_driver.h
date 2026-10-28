/* SPDX-License-Identifier: BSD-3-Clause */
/* Deliberately incompatible foreign-driver API: collision fixture, not a sensor model. */
#ifndef _INV_IMU_DRIVER_H_
#define _INV_IMU_DRIVER_H_
#include <stdint.h>
#define UI_SPI4 97
#define INV_IMU_VERSION_STRING "foreign-fixture"
typedef struct { uint8_t foreign_state[3]; } inv_imu_device_t;
#ifdef __cplusplus
extern "C" {
#endif
int inv_imu_read_reg(inv_imu_device_t *device);
#ifdef __cplusplus
}
#endif
#endif
