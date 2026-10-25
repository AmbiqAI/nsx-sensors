/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef NSX_SENSORS_T5838_H
#define NSX_SENSORS_T5838_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <nsx/sensors/status.h>
#ifdef __cplusplus
extern "C" {
#endif
#define T5838_WRITE_SYMBOLS 26u
/* One START, 24 MSB-first payload bits, one STOP. Durations are PDM CLK
 * cycles, not microseconds. Drive THSEL low before/after, high for each symbol,
 * and low between symbols. No read turnaround or device ACK is implemented. */
typedef struct {
    uint16_t high_cycles[T5838_WRITE_SYMBOLS];
    uint16_t space_cycles, pre_cycles, post_cycles;
} t5838_write_frame_t;
typedef struct {
    /* Synchronous bounded delivery; frame borrowed until return. The endpoint
     * must maintain continuous CLK >50 kHz during all writes and guarantee
     * pulse timing (timer/DMA or otherwise qualified implementation). Ordinary
     * RTOS delays are not sufficient. A timing failure must return an error. */
    nsx_sensor_status_t (*write_frame)(void *user, const t5838_write_frame_t *frame);
    /* Optional instantaneous WAKE pin sample. Not an interrupt queue. */
    nsx_sensor_status_t (*read_wake)(void *user, bool *active);
} t5838_control_ops_t;
typedef struct { const t5838_control_ops_t *ops; void *user; } t5838_control_t;
typedef enum { T5838_AAD_OFF, T5838_AAD_ANALOG, T5838_AAD_DIGITAL1,
               T5838_AAD_DIGITAL2 } t5838_aad_mode_t;
typedef enum {
    T5838_LPF_4400_HZ = 1, T5838_LPF_2000_HZ, T5838_LPF_1900_HZ,
    T5838_LPF_1800_HZ, T5838_LPF_1600_HZ, T5838_LPF_1300_HZ, T5838_LPF_1100_HZ
} t5838_analog_lpf_t;
typedef enum { T5838_THRESHOLD_RELATIVE = 1, T5838_THRESHOLD_ABSOLUTE = 2,
               T5838_THRESHOLD_BOTH = 3 } t5838_threshold_algorithm_t;
typedef enum { T5838_AUDIO_SLEEP, T5838_AUDIO_LOW_POWER,
               T5838_AUDIO_HIGH_QUALITY, T5838_AUDIO_ULTRASONIC } t5838_audio_mode_t;
typedef struct {
    t5838_aad_mode_t mode;
    t5838_analog_lpf_t analog_lpf;
    uint8_t analog_threshold; /* 0..15: 60 + 2.5*code dB SPL (typical). */
    t5838_threshold_algorithm_t algorithm;
    uint16_t absolute_threshold, floor; /* Register codes 0x00f..0x7bc. */
    uint8_t relative_threshold; /* Register code 0x24..0xff. */
    uint16_t relative_pulse_min, absolute_pulse_min; /* Codes 0..300 / 0..3500. */
} t5838_config_t;
typedef struct {
    t5838_control_t control;
    t5838_config_t _config;
    bool _bound, _configured, _enabled;
} t5838_context_t;
/* No allocation, global device, I2C/SPI dependency, or audio buffers. Caller
 * zero-initializes storage, owns CLK/power/SELECT/THSEL/WAKE, and serializes
 * operations across the entire shared-clock microphone group. Ops/user borrowed.
 * No identity register exists; success means transport delivery, not discovery. */
nsx_sensor_status_t t5838_bind(t5838_context_t *ctx, const t5838_control_t *control);
t5838_config_t t5838_default_config(void);
nsx_sensor_status_t t5838_validate_config(const t5838_config_t *config);
nsx_sensor_status_t t5838_encode_write(uint8_t reg, uint8_t value, t5838_write_frame_t *frame);
/* Init/configure: supply continuous 50<CLK<200 kHz first, with THSEL low;
 * wait at least 2 ms when coming from capture. Unlocks and writes configuration,
 * leaves AAD disabled. Configure all microphones before changing shared CLK.
 * Invalid requests preserve state; any attempted init or failed write makes
 * prior state unusable until full init succeeds. */
nsx_sensor_status_t t5838_init(t5838_context_t *ctx, const t5838_config_t *config);
/* Enable AAD using the last config, while the programming clock still runs.
 * Then for Analog/D2 wait >=2 ms at 50<CLK<200 kHz and stop CLK low; for D1
 * transition continuously to 400..800 kHz (no low gap >=70 us). Configure/enable
 * both microphones before that shared transition. OFF writes zero.
 * API does not change clocks or wait for the ~12 us enable pulse on WAKE. */
nsx_sensor_status_t t5838_enable(t5838_context_t *ctx);
/* Disable after restoring the programming clock. Invalidates cached config;
 * init before enabling again. Stop CLK >=1 ms with Analog/D2 disabled to reset.
 * A power cycle, external register write or uncoordinated clock loss requires
 * invalidate on every affected mic. */
nsx_sensor_status_t t5838_disable(t5838_context_t *ctx);
void t5838_invalidate(t5838_context_t *ctx);
nsx_sensor_status_t t5838_read_wake(t5838_context_t *ctx, bool *active);
/* Pure checks for the final operating clock, not a clock generator or a safe
 * transition implementation. Sleep permits 0 or 50<CLK<200 kHz. Analog/D2
 * require 0; D1 requires Low Power. OFF permits any valid audio mode. */
nsx_sensor_status_t t5838_validate_operating_clock(t5838_aad_mode_t aad,
                                                 t5838_audio_mode_t audio,
                                                 uint32_t clock_hz);
#ifdef __cplusplus
}
#endif
#endif
