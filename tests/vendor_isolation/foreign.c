/* SPDX-License-Identifier: BSD-3-Clause */
#include <inv_imu_driver.h>
int inv_imu_read_reg(inv_imu_device_t *device) { return device->foreign_state[0]; }
