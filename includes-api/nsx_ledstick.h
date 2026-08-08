/**
 * @file ledstick.h
 * @author Adam Page (adam.page@ambiq.com)
 * @brief Control SparkFun Qwiic LED Stick (https://www.sparkfun.com/products/18354)
 * @version 0.1
 * @date 2023-12-13
 *
 * @copyright Copyright (c) 2023
 *
 */
#ifndef NSX_LEDSTICK_H
#define NSX_LEDSTICK_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "nsx_i2c.h"

typedef struct {
	nsx_i2c_config_t *i2c_config;
	uint16_t addr;
} ledstick_context_t;

/**
 * @brief Initialize the LED stick
 *
 * @param ctx Driver context
 * @param i2c_config I2C configuration
 * @param addr I2C device address
 * @return uint32_t
 */
uint32_t
ledstick_init(ledstick_context_t *ctx, nsx_i2c_config_t *i2c_config, uint16_t addr);

/**
 * @brief Set the color of a single LED
 *
 * @param ctx Driver context
 * @param number LED number
 * @param red Red value
 * @param green Green value
 * @param blue Blue value
 * @return uint32_t
 */
uint32_t
ledstick_set_color(ledstick_context_t *ctx, uint8_t number, uint8_t red, uint8_t green, uint8_t blue);

/**
 * @brief Set the color of all LEDs
 *
 * @param ctx Driver context
 * @param red Red value
 * @param green Green value
 * @param blue Blue value
 * @return uint32_t
 */
uint32_t
ledstick_set_all_colors(ledstick_context_t *ctx, uint8_t red, uint8_t green, uint8_t blue);

/**
 * @brief Turn off all LEDs
 *
 * @param ctx Driver context
 * @return uint32_t
 */
uint32_t
ledstick_set_all_off(ledstick_context_t *ctx);

/**
 * @brief Change the I2C address of the LED stick
 *
 * @param ctx Driver context
 * @param newAddress New I2C address
 * @return uint32_t
 */
uint32_t
ledstick_change_address(ledstick_context_t *ctx, uint8_t newAddress);

/**
 * @brief Change the number of LEDs
 *
 * @param ctx Driver context
 * @param newLength New number of LEDs
 * @return uint32_t
 */
uint32_t
ledstick_set_led_length(ledstick_context_t *ctx, uint8_t newLength);

/**
 * @brief Set the brightness of all LEDs
 *
 * @param ctx Driver context
 * @param brightness Brightness value (0-31)
 * @return uint32_t
 */
uint32_t
ledstick_set_all_brightness(ledstick_context_t *ctx, uint8_t brightness);

/**
 * @brief Set the brightness of a single LED
 *
 * @param ctx Driver context
 * @param number LED number
 * @param brightness Brightness value (0-31)
 * @return uint32_t
 */
uint32_t
ledstick_set_brightness(ledstick_context_t *ctx, uint8_t number, uint8_t brightness);

#ifdef __cplusplus
}
#endif

#endif // NSX_LEDSTICK_H
