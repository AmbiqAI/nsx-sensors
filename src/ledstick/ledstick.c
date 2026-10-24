/* SPDX-License-Identifier: BSD-3-Clause
 * Migrated from src/nsx_ledstick.c; see PROVENANCE.md for port lineage. */
#include <nsx/sensors/ledstick.h>
#define CMD_ADDRESS 0xc7
#define CMD_LENGTH 0x70
#define CMD_COLOR 0x71
#define CMD_ALL_COLORS 0x72
#define CMD_BRIGHTNESS 0x76
#define CMD_ALL_BRIGHTNESS 0x77
#define CMD_OFF 0x78

static nsx_sensor_status_t send(ledstick_context_t *ctx, const uint8_t *bytes, size_t size)
{
    if (!ctx || !ctx->_bound) return NSX_SENSOR_E_STATE;
    nsx_sensor_transfer_t transfer = { bytes, size, NULL, 0 };
    return nsx_sensor_bus_transfer(&ctx->bus, &transfer);
}
static bool ready(const ledstick_context_t *ctx)
{
    return ctx && ctx->_bound && ctx->_configured;
}
nsx_sensor_status_t ledstick_bind(ledstick_context_t *ctx, const nsx_sensor_bus_t *bus)
{
    if (!ctx || nsx_sensor_bus_validate(bus) != NSX_SENSOR_OK) return NSX_SENSOR_E_ARGUMENT;
    if (bus->kind != NSX_SENSOR_BUS_I2C || bus->max_tx_size < 5) return NSX_SENSOR_E_UNSUPPORTED;
    nsx_sensor_bus_t copy = *bus;
    *ctx = (ledstick_context_t){ .bus = copy, ._bound = true };
    return NSX_SENSOR_OK;
}
nsx_sensor_status_t ledstick_validate_config(const ledstick_config_t *config)
{
    return config && config->length ? NSX_SENSOR_OK : NSX_SENSOR_E_ARGUMENT;
}
nsx_sensor_status_t ledstick_set_led_length(ledstick_context_t *ctx, uint8_t length)
{
    if (!ctx || !ctx->_bound) return NSX_SENSOR_E_STATE;
    if (!length) return NSX_SENSOR_E_ARGUMENT;
    uint8_t bytes[] = { CMD_LENGTH, length };
    ctx->_configured = false;
    nsx_sensor_status_t status = send(ctx, bytes, sizeof(bytes));
    if (status == NSX_SENSOR_OK) { ctx->_length = length; ctx->_configured = true; }
    return status;
}
nsx_sensor_status_t ledstick_init(ledstick_context_t *ctx, const ledstick_config_t *config)
{
    if (ledstick_validate_config(config) != NSX_SENSOR_OK) return NSX_SENSOR_E_ARGUMENT;
    nsx_sensor_status_t status = ledstick_set_led_length(ctx, config->length);
    if (status == NSX_SENSOR_OK) {
        status = ledstick_set_all_off(ctx);
        if (status != NSX_SENSOR_OK) ctx->_configured = false;
    }
    return status;
}
nsx_sensor_status_t ledstick_set_color(ledstick_context_t *ctx, uint8_t number,
                                     uint8_t red, uint8_t green, uint8_t blue)
{
    if (!ready(ctx)) return NSX_SENSOR_E_STATE;
    if (!number || number > ctx->_length) return NSX_SENSOR_E_ARGUMENT;
    uint8_t bytes[] = { CMD_COLOR, number, red, green, blue };
    return send(ctx, bytes, sizeof(bytes));
}
nsx_sensor_status_t ledstick_set_all_colors(ledstick_context_t *ctx, uint8_t red,
                                          uint8_t green, uint8_t blue)
{
    if (!ready(ctx)) return NSX_SENSOR_E_STATE;
    uint8_t bytes[] = { CMD_ALL_COLORS, red, green, blue };
    return send(ctx, bytes, sizeof(bytes));
}
nsx_sensor_status_t ledstick_set_all_off(ledstick_context_t *ctx)
{
    if (!ready(ctx)) return NSX_SENSOR_E_STATE;
    uint8_t command = CMD_OFF;
    return send(ctx, &command, 1);
}
nsx_sensor_status_t ledstick_set_all_brightness(ledstick_context_t *ctx, uint8_t brightness)
{
    if (!ready(ctx)) return NSX_SENSOR_E_STATE;
    if (brightness > 31) return NSX_SENSOR_E_ARGUMENT;
    uint8_t bytes[] = { CMD_ALL_BRIGHTNESS, brightness };
    return send(ctx, bytes, sizeof(bytes));
}
nsx_sensor_status_t ledstick_set_brightness(ledstick_context_t *ctx, uint8_t number, uint8_t brightness)
{
    if (!ready(ctx)) return NSX_SENSOR_E_STATE;
    if (!number || number > ctx->_length || brightness > 31) return NSX_SENSOR_E_ARGUMENT;
    uint8_t bytes[] = { CMD_BRIGHTNESS, number, brightness };
    return send(ctx, bytes, sizeof(bytes));
}
nsx_sensor_status_t ledstick_change_address(ledstick_context_t *ctx, uint8_t address)
{
    if (!ready(ctx)) return NSX_SENSOR_E_STATE;
    if (address < 8 || address > 0x77) return NSX_SENSOR_E_ARGUMENT;
    uint8_t bytes[] = { CMD_ADDRESS, address };
    nsx_sensor_status_t status = send(ctx, bytes, sizeof(bytes));
    ctx->_bound = ctx->_configured = false;
    return status;
}
