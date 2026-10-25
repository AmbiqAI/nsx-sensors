/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef NSX_SENSORS_TSL254X_H
#define NSX_SENSORS_TSL254X_H
#include <stdbool.h>
#include <nsx/sensors/bus.h>
#ifdef __cplusplus
extern "C" {
#endif
#define TSL254X_I2C_ADDRESS 0x39u
typedef enum { TSL254X_TSL2540, TSL254X_TSL2541 } tsl254x_variant_t;
typedef enum {
    TSL254X_GAIN_HALF, TSL254X_GAIN_1, TSL254X_GAIN_4,
    TSL254X_GAIN_16, TSL254X_GAIN_64, TSL254X_GAIN_128
} tsl254x_gain_t;
typedef struct {
    tsl254x_gain_t gain;
    uint16_t integration_cycles; /* 1..256, nominal 2.81 ms each. */
    uint16_t low_threshold, high_threshold; /* Raw visible counts. */
    uint8_t persistence; /* Hardware code 0..15; 0=every cycle, 1..3=1..3,
                           4..15=5,10,...60 consecutive out-of-range cycles. */
    bool threshold_interrupt, saturation_interrupt;
} tsl254x_config_t;
typedef struct { uint8_t id, revision, auxiliary; } tsl254x_identity_t;
typedef struct {
    uint16_t visible, infrared;
    bool analog_saturated, digital_saturated;
    bool threshold_interrupt, calibration_complete;
} tsl254x_sample_t;
typedef struct {
    nsx_sensor_bus_t bus;
    tsl254x_variant_t _variant;
    tsl254x_config_t _config;
    float _visible_scale;
    bool _bound, _configured, _running;
} tsl254x_context_t;
/* Zero-initialize caller storage. Bind copies the descriptor, borrows its ops/user
 * and performs no I/O. Requires I2C, TX>=2 and RX>=4. Serialize whole operations.
 * Explicit variant is checked against auxiliary ID; no silent substitution. */
nsx_sensor_status_t tsl254x_bind(tsl254x_context_t *ctx, const nsx_sensor_bus_t *bus,
                               tsl254x_variant_t variant);
tsl254x_config_t tsl254x_default_config(void);
nsx_sensor_status_t tsl254x_validate_config(const tsl254x_config_t *config);
nsx_sensor_status_t tsl254x_probe(tsl254x_context_t *ctx, tsl254x_identity_t *identity);
/* Programs configuration with ALS off and leaves the device asleep. No soft
 * reset command exists. Any failed mutation requires init; invalid arguments
 * preserve existing state. Caller owns power/reset and must reinit after either. */
nsx_sensor_status_t tsl254x_init(tsl254x_context_t *ctx, const tsl254x_config_t *config);
nsx_sensor_status_t tsl254x_start(tsl254x_context_t *ctx);
nsx_sensor_status_t tsl254x_stop(tsl254x_context_t *ctx);
/* Latest register snapshot, NOT a freshness guarantee. Wait for integration/
 * autozero completion or use interrupts before reading. Both channels are read
 * in one burst; no device-wide latch is promised across status/data transfers.
 * Saturation flags annotate successful samples; outputs unchanged on error.
 * Status is non-consuming (init disables clear-on-read); ASAT/AINT/CINT remain
 * sticky until explicitly acknowledged. */
nsx_sensor_status_t tsl254x_read_sample(tsl254x_context_t *ctx, tsl254x_sample_t *sample);
/* Acknowledge selected sticky flags. Application owns GPIO and deferred service. */
nsx_sensor_status_t tsl254x_clear_status(tsl254x_context_t *ctx, bool saturation,
                                       bool threshold, bool calibration);
/* Cached factory visible-count multiplier: 1 for TSL2540, signed-percent VISADJ
 * for TSL2541. Not lux, glass correction, or gain/integration normalization. */
nsx_sensor_status_t tsl254x_get_visible_scale(const tsl254x_context_t *ctx, float *scale);
#ifdef __cplusplus
}
#endif
#endif
