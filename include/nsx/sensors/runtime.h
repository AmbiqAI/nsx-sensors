/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef NSX_SENSORS_RUNTIME_H
#define NSX_SENSORS_RUNTIME_H
#include <stdint.h>
typedef struct {
    void *user;
    /* Optional monotonic microseconds; must not move backwards during a call. */
    uint64_t (*now_us)(void *user);
    /* Wait at least duration_us elapsed time, including sub-tick requests. */
    void (*delay_us)(void *user, uint32_t duration_us);
} nsx_sensor_runtime_t;
#endif
