/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef SPARK_T5838_PAIR_H
#define SPARK_T5838_PAIR_H
#include <nsx/sensors/t5838.h>
#include <nsx/sensors/runtime.h>
typedef struct {
    void *user;
    /* Set actual clock, return only once stable. For D1 transition to capture,
     * preserve clock continuity: never leave CLK low for >=70 us. */
    nsx_sensor_status_t (*set_clock_hz)(void *user, uint32_t hz);
    nsx_sensor_runtime_t runtime;
} spark_microphone_clock_t;
/* Example orchestration, not a board HAL: stop capture/DMA first, hold a group
 * lock, ensure power is stable and SELECT wiring is fixed. Both contexts must
 * already be bound to separate THSEL endpoints. Shared THSEL is not supported.
 * Both use the same AAD mode because their CLK is shared. OFF selects ordinary
 * 750 kHz capture; D1 selects AAD+capture at 750 kHz; Analog/D2 stop CLK.
 * On error both caches are invalidated and clock/device state is unknown:
 * caller must recover the whole pair before enabling capture. */
nsx_sensor_status_t spark_t5838_configure_pair(t5838_context_t *left,
    t5838_context_t *right, const t5838_config_t *config,
    const spark_microphone_clock_t *clock);
#endif
