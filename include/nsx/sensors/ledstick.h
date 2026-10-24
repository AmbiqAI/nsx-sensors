/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef NSX_SENSORS_LEDSTICK_H
#define NSX_SENSORS_LEDSTICK_H
#include <stdbool.h>
#include <nsx/sensors/bus.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct { uint8_t length; } ledstick_config_t;
typedef struct {
    nsx_sensor_bus_t bus;
    uint8_t _length;
    bool _bound, _configured;
} ledstick_context_t;
/* Caller-owned zero-initialized storage; descriptors copied, endpoint borrowed.
 * Bind performs no I/O. No identity probe is available on this accessory. */
nsx_sensor_status_t ledstick_bind(ledstick_context_t *ctx, const nsx_sensor_bus_t *bus);
/* Length is a nonzero protocol byte; caller ensures attached hardware supports
 * that length. init programs length, then clears all colors (visible effect). */
nsx_sensor_status_t ledstick_validate_config(const ledstick_config_t *config);
nsx_sensor_status_t ledstick_init(ledstick_context_t *ctx, const ledstick_config_t *config);
/* LED numbers are one-based, within the successfully configured length. */
nsx_sensor_status_t ledstick_set_color(ledstick_context_t *ctx, uint8_t number,
                                     uint8_t red, uint8_t green, uint8_t blue);
nsx_sensor_status_t ledstick_set_all_colors(ledstick_context_t *ctx, uint8_t red,
                                          uint8_t green, uint8_t blue);
nsx_sensor_status_t ledstick_set_all_off(ledstick_context_t *ctx);
/* Brightness must be 0..31; invalid values fail without I/O. */
nsx_sensor_status_t ledstick_set_all_brightness(ledstick_context_t *ctx, uint8_t brightness);
nsx_sensor_status_t ledstick_set_brightness(ledstick_context_t *ctx, uint8_t number, uint8_t brightness);
/* Failed length writes invalidate configured state: reinitialize to recover. */
nsx_sensor_status_t ledstick_set_led_length(ledstick_context_t *ctx, uint8_t length);
/* Accepts non-reserved 7-bit addresses (0x08..0x77). An attempted address write
 * invalidates the binding even on success. Rebind explicitly to the new
 * endpoint and initialize again. On error the address may have changed: use
 * application recovery to establish the address before rebinding. */
nsx_sensor_status_t ledstick_change_address(ledstick_context_t *ctx, uint8_t address);
#ifdef __cplusplus
}
#endif
#endif
