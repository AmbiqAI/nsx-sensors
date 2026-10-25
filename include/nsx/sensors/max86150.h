/* SPDX-License-Identifier: BSD-3-Clause
 * Original MAX86150 driver: Adam Page, Copyright (c) 2022. */
#ifndef NSX_SENSORS_MAX86150_H
#define NSX_SENSORS_MAX86150_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <nsx/sensors/bus.h>
#include <nsx/sensors/runtime.h>
#ifdef __cplusplus
extern "C" {
#endif
#define MAX86150_I2C_ADDRESS 0x5e
#define MAX86150_FIFO_DEPTH 32
#define MAX86150_MAX_SLOTS 4
typedef enum {
    MAX86150_SLOT_OFF = 0, MAX86150_SLOT_PPG_LED1 = 1, MAX86150_SLOT_PPG_LED2 = 2,
    MAX86150_SLOT_PILOT_LED1 = 5, MAX86150_SLOT_PILOT_LED2 = 6, MAX86150_SLOT_ECG = 9
} max86150_slot_t;
/* Native register encodings, MAX86150 Rev 2 pp. 29-35. Requested PPG rates
 * may be adjusted by hardware. ECG CLK0/CLK1 names preserve the ADC clock bit.
 * Averaging encodings 6/7 also mean 32; new configurations should use _32.
 * LED range names are nominal ranges; amplitude 255 gives 51/102 mA. */
typedef enum {
    MAX86150_PPG_ADC_RANGE_4096_NA = 0,
    MAX86150_PPG_ADC_RANGE_8192_NA = 1,
    MAX86150_PPG_ADC_RANGE_16384_NA = 2,
    MAX86150_PPG_ADC_RANGE_32768_NA = 3
} max86150_ppg_adc_range_t;

typedef enum {
    MAX86150_PPG_SAMPLE_RATE_10_HZ = 0,
    MAX86150_PPG_SAMPLE_RATE_20_HZ = 1,
    MAX86150_PPG_SAMPLE_RATE_50_HZ = 2,
    MAX86150_PPG_SAMPLE_RATE_84_HZ = 3,
    MAX86150_PPG_SAMPLE_RATE_100_HZ = 4,
    MAX86150_PPG_SAMPLE_RATE_200_HZ = 5,
    MAX86150_PPG_SAMPLE_RATE_400_HZ = 6,
    MAX86150_PPG_SAMPLE_RATE_800_HZ = 7,
    MAX86150_PPG_SAMPLE_RATE_1000_HZ = 8,
    MAX86150_PPG_SAMPLE_RATE_1600_HZ = 9,
    MAX86150_PPG_SAMPLE_RATE_3200_HZ = 10,
    MAX86150_PPG_SAMPLE_RATE_10_HZ_2_PULSES = 11,
    MAX86150_PPG_SAMPLE_RATE_20_HZ_2_PULSES = 12,
    MAX86150_PPG_SAMPLE_RATE_50_HZ_2_PULSES = 13,
    MAX86150_PPG_SAMPLE_RATE_84_HZ_2_PULSES = 14,
    MAX86150_PPG_SAMPLE_RATE_100_HZ_2_PULSES = 15
} max86150_ppg_sample_rate_t;

typedef enum {
    MAX86150_PPG_PULSE_WIDTH_50_US = 0,
    MAX86150_PPG_PULSE_WIDTH_100_US = 1,
    MAX86150_PPG_PULSE_WIDTH_200_US = 2,
    MAX86150_PPG_PULSE_WIDTH_400_US = 3
} max86150_ppg_pulse_width_t;

typedef enum {
    MAX86150_PPG_SAMPLE_AVERAGE_1 = 0,
    MAX86150_PPG_SAMPLE_AVERAGE_2 = 1,
    MAX86150_PPG_SAMPLE_AVERAGE_4 = 2,
    MAX86150_PPG_SAMPLE_AVERAGE_8 = 3,
    MAX86150_PPG_SAMPLE_AVERAGE_16 = 4,
    MAX86150_PPG_SAMPLE_AVERAGE_32 = 5
} max86150_ppg_sample_average_t;

typedef enum {
    MAX86150_LED_RANGE_50_MA = 0,
    MAX86150_LED_RANGE_100_MA = 1
} max86150_led_range_t;

typedef enum {
    MAX86150_ECG_SAMPLE_RATE_1600_HZ_CLK0 = 0,
    MAX86150_ECG_SAMPLE_RATE_800_HZ_CLK0 = 1,
    MAX86150_ECG_SAMPLE_RATE_400_HZ_CLK0 = 2,
    MAX86150_ECG_SAMPLE_RATE_200_HZ_CLK0 = 3,
    MAX86150_ECG_SAMPLE_RATE_3200_HZ_CLK1 = 4,
    MAX86150_ECG_SAMPLE_RATE_1600_HZ_CLK1 = 5,
    MAX86150_ECG_SAMPLE_RATE_800_HZ_CLK1 = 6,
    MAX86150_ECG_SAMPLE_RATE_400_HZ_CLK1 = 7
} max86150_ecg_sample_rate_t;

typedef enum {
    MAX86150_ECG_PGA_GAIN_1 = 0,
    MAX86150_ECG_PGA_GAIN_2 = 1,
    MAX86150_ECG_PGA_GAIN_4 = 2,
    MAX86150_ECG_PGA_GAIN_8 = 3
} max86150_ecg_pga_gain_t;

typedef enum {
    MAX86150_ECG_IA_GAIN_5 = 0,
    MAX86150_ECG_IA_GAIN_9_5 = 1,
    MAX86150_ECG_IA_GAIN_20 = 2,
    MAX86150_ECG_IA_GAIN_50 = 3
} max86150_ecg_ia_gain_t;

enum {
    MAX86150_INT1_ALMOST_FULL=0x80, MAX86150_INT1_PPG_READY=0x40,
    MAX86150_INT1_ALC_OVERFLOW=0x20, MAX86150_INT1_PROXIMITY=0x10,
    MAX86150_INT1_POWER_READY=0x01, MAX86150_INT2_VDD_OUT_OF_RANGE=0x80,
    MAX86150_INT2_ECG_READY=0x04
};
typedef struct {
    max86150_slot_t slots[4]; /* Contiguous PPG slots, then ECG, then Off. */
    max86150_ppg_adc_range_t ppg_adc_range;
    max86150_ppg_sample_rate_t ppg_sample_rate;
    max86150_ppg_pulse_width_t ppg_pulse_width;
    max86150_ppg_sample_average_t ppg_sample_average;
    uint8_t led_amplitude[2], pilot_amplitude, proximity_threshold;
    max86150_led_range_t led_range[2];
    max86150_ecg_sample_rate_t ecg_sample_rate;
    max86150_ecg_pga_gain_t ecg_pga_gain;
    max86150_ecg_ia_gain_t ecg_ia_gain;
    uint8_t almost_full_threshold; /* Free slots, 0..15. */
    bool fifo_rollover, almost_full_clear_on_data, almost_full_once;
} max86150_config_t;
typedef struct {
    int32_t value[4]; /* Slot order: unsigned 19-bit PPG, signed 18-bit ECG codes. */
    max86150_slot_t slots[4];
    uint8_t count; /* Inactive entries are zero/Off. No physical/clinical units. */
} max86150_frame_t;
typedef struct { uint8_t status1, status2; } max86150_interrupts_t;
typedef struct { uint8_t write_pointer, overflow_count, read_pointer; } max86150_fifo_state_t;
typedef struct {
    nsx_sensor_bus_t bus;
    nsx_sensor_runtime_t runtime;
    max86150_config_t _config;
    uint8_t _remaining;
    bool _bound, _configured, _shutdown, _fifo_enabled, _fifo_valid;
    bool _afull_seen, _snapshot_ready;
} max86150_context_t;
/* Caller owns context, descriptor backing storage and operation serialization.
 * Outputs unchanged on failure except completed, which counts published frames.
 * Failed configuration application requires init. No heap, NSX, or global device state. */
max86150_config_t max86150_default_config(void);
nsx_sensor_status_t max86150_validate_config(const max86150_config_t *config);
/* Pure binding: I2C only; TX>=3, RX>=12. delay_us required for reset/init;
 * now_us optional. Without now_us, timeouts bound requested waits only. */
nsx_sensor_status_t max86150_bind(max86150_context_t *ctx, const nsx_sensor_bus_t *bus,
                                 const nsx_sensor_runtime_t *runtime);
nsx_sensor_status_t max86150_probe(max86150_context_t *ctx, uint8_t *part_id);
/* Init resets, configures and finishes shutdown with FIFO and interrupts off.
 * Default LED amplitudes are zero; select currents explicitly before start. */
nsx_sensor_status_t max86150_init(max86150_context_t *ctx, const max86150_config_t *config);
nsx_sensor_status_t max86150_reset(max86150_context_t *ctx, uint32_t timeout_us);
/* Configure requires shutdown with FIFO disabled. Changes discard the snapshot.
 * Hardware may adjust PPG rate for timing/synchronization; requested settings
 * are not a promise of effective FIFO rate. Read back PPG config as needed. */
nsx_sensor_status_t max86150_configure(max86150_context_t *ctx, const max86150_config_t *config);
/* Start a new acquisition: flush FIFO and wake. Unread frames are discarded. */
nsx_sensor_status_t max86150_start(max86150_context_t *ctx);
nsx_sensor_status_t max86150_powerup(max86150_context_t *ctx);
nsx_sensor_status_t max86150_shutdown(max86150_context_t *ctx);
nsx_sensor_status_t max86150_set_fifo_enable(max86150_context_t *ctx, uint8_t enable);
/* FIFO-only recovery; ends shutdown/FIFO disabled. Does not write read-only OVF.
 * Lost configuration (including a failed interrupt-status read) requires init. */
nsx_sensor_status_t max86150_clear_fifo(max86150_context_t *ctx);
/* Continuous raw acquisition, FIFO enabled/awake and rollover OFF required.
 * Requires repeating A_FULL and no clear-on-data to retain full-state evidence.
 * Retains one queued frame to disambiguate full/empty pointers without stale
 * A_FULL evidence. Adds one frame of latency; AGAIN means fewer than two frames.
 * Use read_fifo_snapshot to drain the final frame after acquisition.
 * Non-rollover preserves queued frames if production catches up to the reader.
 * Observed overflow requires recovery. Hardware clears OVF on every popped
 * frame, so loss occurring during a burst cannot always be observed: success
 * does not guarantee gapless capture. Use snapshot reads for loss detection
 * without that race. Capacity/partial publication rules match snapshot below. */
nsx_sensor_status_t max86150_read_fifo(max86150_context_t *ctx,
    max86150_frame_t *frames, size_t capacity, size_t *completed);
/* Capture-and-drain API: first disable FIFO pushes, then snapshot and drain up
 * to capacity frames. FIFO remains disabled; repeated calls drain remaining
 * frames. Start explicitly for a new capture (flushes unread frames). This is
 * intentionally NOT gapless streaming: conversions during draining are omitted.
 * Freezing avoids OVF being erased by a pop before loss can be detected.
 * Overflow or ambiguous FIFO-data consumption requires clear_fifo/start;
 * failed interrupt-status reads require init. No automatic retries.
 * Each burst contains complete frames, published only if the burst succeeds.
 * Empty returns AGAIN. Full/empty discrimination retains read-to-clear A_FULL;
 * do not access device registers outside this context. */
nsx_sensor_status_t max86150_read_fifo_snapshot(max86150_context_t *ctx,
    max86150_frame_t *frames, size_t capacity, size_t *completed);
nsx_sensor_status_t max86150_get_fifo_state(max86150_context_t *ctx, max86150_fifo_state_t *state);
/* These reads consume flags. A_FULL evidence is retained for the snapshot;
 * unexpected power-ready or a failed status read invalidates configuration
 * (init required), since failure may have consumed reset evidence.
 * The legacy get_int1/get_int2 conveniences consume BOTH status registers. */
nsx_sensor_status_t max86150_get_interrupts(max86150_context_t *ctx, max86150_interrupts_t *status);
nsx_sensor_status_t max86150_get_int1(max86150_context_t *ctx, uint8_t *status);
nsx_sensor_status_t max86150_get_int2(max86150_context_t *ctx, uint8_t *status);
/* Masks: status1 enables 0xe0 (A_FULL, PPG_RDY, ALC_OVF); status2 0x84.
 * Automatic proximity mode is unsupported because its transitions flush FIFO. */
nsx_sensor_status_t max86150_set_interrupts(max86150_context_t *ctx, uint8_t mask1, uint8_t mask2);
/* Raw hardware readback of PPG_CONFIG1/2, not cached requested settings. */
nsx_sensor_status_t max86150_get_ppg_config(max86150_context_t *ctx, uint8_t *config1, uint8_t *config2);
nsx_sensor_status_t max86150_set_fifo_slots(max86150_context_t *ctx, const max86150_slot_t slots[4]);
nsx_sensor_status_t max86150_set_fifo_slot(max86150_context_t *ctx, uint8_t slot, max86150_slot_t type);
nsx_sensor_status_t max86150_disable_slots(max86150_context_t *ctx);
/* Direct setters below require shutdown/FIFO disabled, validate values and
 * modify only the corresponding register fields. Use configure to replace
 * all settings. A write failure requires init; read failures propagate. */
nsx_sensor_status_t max86150_set_ppg_adc_range(max86150_context_t *ctx, max86150_ppg_adc_range_t value);
nsx_sensor_status_t max86150_set_ppg_sample_rate(max86150_context_t *ctx, max86150_ppg_sample_rate_t value);
nsx_sensor_status_t max86150_set_ppg_pulse_width(max86150_context_t *ctx, max86150_ppg_pulse_width_t value);
nsx_sensor_status_t max86150_set_ppg_sample_average(max86150_context_t *ctx, max86150_ppg_sample_average_t value);
nsx_sensor_status_t max86150_set_proximity_threshold(max86150_context_t *ctx, uint8_t value);
nsx_sensor_status_t max86150_set_led_pulse_amplitude(max86150_context_t *ctx, uint8_t led, uint8_t value);
nsx_sensor_status_t max86150_set_led_current_range(max86150_context_t *ctx, uint8_t led, max86150_led_range_t value);
nsx_sensor_status_t max86150_set_ecg_sample_rate(max86150_context_t *ctx, max86150_ecg_sample_rate_t value);
nsx_sensor_status_t max86150_set_ecg_pga_gain(max86150_context_t *ctx, max86150_ecg_pga_gain_t value);
nsx_sensor_status_t max86150_set_ecg_ia_gain(max86150_context_t *ctx, max86150_ecg_ia_gain_t value);
nsx_sensor_status_t max86150_set_almost_full_threshold(max86150_context_t *ctx, uint8_t value);
nsx_sensor_status_t max86150_set_almost_full_rollover(max86150_context_t *ctx, uint8_t value);
nsx_sensor_status_t max86150_set_almost_full_int_options(max86150_context_t *ctx, uint8_t value);
nsx_sensor_status_t max86150_set_almost_full_flag_options(max86150_context_t *ctx, uint8_t value);
#ifdef __cplusplus
}
#endif
#endif
