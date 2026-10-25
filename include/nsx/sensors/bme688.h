/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 Ambiq Micro, Inc.
 */
#ifndef NSX_BME688_H
#define NSX_BME688_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <nsx/sensors/bus.h>
#include <nsx/sensors/runtime.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BME688_OS_OFF, BME688_OS_1X, BME688_OS_2X,
    BME688_OS_4X, BME688_OS_8X, BME688_OS_16X
} bme688_oversampling_t;

typedef enum {
    BME688_FILTER_OFF, BME688_FILTER_1, BME688_FILTER_3, BME688_FILTER_7,
    BME688_FILTER_15, BME688_FILTER_31, BME688_FILTER_63, BME688_FILTER_127
} bme688_filter_t;

typedef struct {
    bme688_oversampling_t temperature; /**< Must be enabled for compensation. */
    bme688_oversampling_t pressure;
    bme688_oversampling_t humidity;
    bme688_filter_t filter;
} bme688_config_t;

typedef struct {
    bool enabled;
    uint16_t temperature_c; /**< 0..400 degrees C; used only when enabled. */
    uint16_t duration_ms;   /**< 1..4032 ms when enabled; rounded DOWN by HW. */
    float ambient_temperature_c; /**< -40..85 C, for heater compensation. */
} bme688_heater_t;

/** Actual register state. Heater target temperature cannot be read back. */
typedef struct {
    bool enabled;
    uint8_t resistance_code;
    uint8_t duration_code;
    uint16_t duration_ms;
} bme688_heater_state_t;

typedef struct {
    float temperature_c;
    float pressure_pa;
    float humidity_percent;
    float gas_resistance_ohm; /**< Raw compensated resistance, never IAQ/VOC. */
    bool pressure_valid; /**< False for disabled channels; numeric value is 0. */
    bool humidity_valid;
    bool gas_valid; /**< Heater enabled and sensor gas_valid bit set. */
    bool heater_stable; /**< Independent of gas_valid; require both for use. */
    uint8_t measurement_index;
    uint8_t gas_index;
} bme688_sample_t;

/* Driver-owned calibration/state, exposed only to permit caller-owned storage.
 * Do not modify these fields. No heap, static device state, or shared page cache.
 */
typedef struct {
    uint16_t par_t1, par_p1, par_h1, par_h2;
    int16_t par_t2, par_p2, par_p4, par_p5, par_p8, par_p9, par_gh2;
    int8_t par_t3, par_p3, par_p6, par_p7, par_h3, par_h4, par_h5, par_h7;
    int8_t par_gh1, par_gh3, res_heat_val;
    uint8_t par_p10, par_h6, res_heat_range;
} bme688_calibration_t;

typedef struct {
    nsx_sensor_bus_t bus;
    nsx_sensor_runtime_t runtime;
    bool _bound;
    bme688_calibration_t _calibration;
    bme688_config_t _config;
    uint32_t _measurement_us;
    uint16_t _heater_ms;
    bool _initialized, _config_valid, _heater_valid, _gas_enabled, _pending;
} bme688_context_t;

typedef struct { uint8_t chip_id, variant_id; } bme688_identity_t;

/* Context is caller-owned; zero-initialize before first use. bind performs no
 * I/O, copies descriptors, and clears prior state. Bindings/ops must outlive it.
 * Do not rebind while a conversion is pending. Serialize all device operations.
 * Outputs remain unchanged on errors. SPI probe may select a register page. */
nsx_sensor_status_t bme688_bind(bme688_context_t *ctx, const nsx_sensor_bus_t *bus,
                              const nsx_sensor_runtime_t *runtime);
bme688_config_t bme688_default_config(void);
nsx_sensor_status_t bme688_validate_config(const bme688_config_t *config);
nsx_sensor_status_t bme688_probe(bme688_context_t *ctx, bme688_identity_t *identity);
/* Validate, probe, reset, read calibration, apply explicit config, heater off.
 * Requires delay_us. Once init begins I/O, any failure leaves the device
 * unusable until init succeeds. Invalid arguments leave existing state intact. No platform bus initialization or automatic sampling. */
nsx_sensor_status_t bme688_init(bme688_context_t *ctx, const bme688_config_t *config);
/* Reset invalidates configuration and calibration; call init before reuse. */
nsx_sensor_status_t bme688_reset(bme688_context_t *ctx);
nsx_sensor_status_t bme688_get_id(bme688_context_t *ctx, uint8_t *chip_id, uint8_t *variant_id);

/** Request sleep and poll at 1 ms intervals, bounded by timeout_us.
 * Uses elapsed time when now_us is supplied, otherwise requested delays.
 * A synchronous transport call cannot be preempted by the deadline.
 * Cancels pending collection even on timeout. Configuration setters invoke
 * this with a 5 s bound (the maximum forced conversion is below 4.2 s).
 * Configuration can block until the current conversion ends.
 */
nsx_sensor_status_t bme688_sleep(bme688_context_t *ctx, uint32_t timeout_us);
nsx_sensor_status_t bme688_set_config(bme688_context_t *ctx, const bme688_config_t *config);
nsx_sensor_status_t bme688_get_config(bme688_context_t *ctx, bme688_config_t *config);
nsx_sensor_status_t bme688_set_heater(bme688_context_t *ctx, const bme688_heater_t *heater);
nsx_sensor_status_t bme688_get_heater(bme688_context_t *ctx, bme688_heater_state_t *heater);

/** Trigger one forced conversion without waiting. wait_us is a conservative
 * Bosch conversion estimate + the actual encoded heater duration + 1 ms guard.
 * Sleeping the MCU for this interval is allowed. Start requires sleep mode and
 * no pending conversion; call sleep() to cancel one. Drains any unread previous
 * field before triggering. No parallel scan/BSEC.
 */
nsx_sensor_status_t bme688_start(bme688_context_t *ctx, uint32_t *wait_us);
/** Single burst read, no delay or polling. AGAIN means no fresh completed
 * sample yet, based on new_data and measuring flags. The measurement index
 * need not advance in forced mode. A successful read consumes this driver's
 * pending conversion. A failed bus read may still consume the hardware's
 * freshness flag; the pending conversion remains but retries may return AGAIN.
 * Call sleep() to cancel, then start() a new conversion if freshness was lost.
 */
nsx_sensor_status_t bme688_collect(bme688_context_t *ctx, bme688_sample_t *sample);
/** Bounded blocking convenience: start, delay, collect. timeout_us bounds the
 * elapsed time after start when now_us is supplied, otherwise requested delays.
 * One collection attempt is allowed at/after expiry; no further wait is issued.
 * Synchronous transfer latency cannot be preempted.
 * Timeout leaves the conversion pending for collect() or sleep().
 */
nsx_sensor_status_t bme688_measure(bme688_context_t *ctx, bme688_sample_t *sample,
                               uint32_t timeout_us);

#ifdef __cplusplus
}
#endif
#endif
