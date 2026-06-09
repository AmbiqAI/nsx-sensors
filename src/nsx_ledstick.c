/**
 * @file ledstick.c
 * @author Adam Page (adam.page@ambiq.com)
 * @brief Control SparkFun Qwiic LED Stick (https://www.sparkfun.com/products/18354)
 * @version 0.1
 * @date 2023-12-13
 *
 * @copyright Copyright (c) 2023
 *
 */
#include <stdint.h>

#include "nsx_i2c.h"
#include "nsx_i2c_register_driver.h"
#include "nsx_ledstick.h"

#define COMMAND_CHANGE_ADDRESS (0xC7)
#define COMMAND_CHANGE_LED_LENGTH (0x70)
#define COMMAND_WRITE_SINGLE_LED_COLOR (0x71)
#define COMMAND_WRITE_ALL_LED_COLOR (0x72)
#define COMMAND_WRITE_RED_ARRAY (0x73)
#define COMMAND_WRITE_GREEN_ARRAY (0x74)
#define COMMAND_WRITE_BLUE_ARRAY (0x75)
#define COMMAND_WRITE_SINGLE_LED_BRIGHTNESS (0x76)
#define COMMAND_WRITE_ALL_LED_BRIGHTNESS (0x77)
#define COMMAND_WRITE_ALL_LED_OFF (0x78)

uint32_t
ledstick_init(ledstick_context_t *ctx, nsx_i2c_config_t *i2c_config, uint16_t addr) {
  ctx->i2c_config = i2c_config;
  ctx->addr = addr;
  return 0;
}

uint32_t
ledstick_set_color(ledstick_context_t *ctx, uint8_t number, uint8_t red, uint8_t green, uint8_t blue) {
  uint8_t buf[4] = { number, red, green, blue};
  return nsx_i2c_write_sequential_regs(ctx->i2c_config, ctx->addr, COMMAND_WRITE_SINGLE_LED_COLOR, buf, 4);
}

uint32_t
ledstick_set_all_colors(ledstick_context_t *ctx, uint8_t red, uint8_t green, uint8_t blue) {
  uint8_t buf[3] = { red, green, blue };
  return nsx_i2c_write_sequential_regs(ctx->i2c_config, ctx->addr, COMMAND_WRITE_ALL_LED_COLOR, buf, 3);
}

uint32_t
ledstick_set_all_off(ledstick_context_t *ctx) {
  uint8_t buf[1] = { COMMAND_WRITE_ALL_LED_OFF };
  return nsx_i2c_write(ctx->i2c_config, buf, 1, ctx->addr);
}

uint32_t
ledstick_change_address(ledstick_context_t *ctx, uint8_t newAddress) {
  uint8_t buf[1] = { newAddress };
  return nsx_i2c_write_sequential_regs(ctx->i2c_config, ctx->addr, COMMAND_CHANGE_ADDRESS, buf, 1);
}

uint32_t
ledstick_set_led_length(ledstick_context_t *ctx, uint8_t newLength) {
  uint8_t buf[1] = { newLength };
  return nsx_i2c_write_sequential_regs(ctx->i2c_config, ctx->addr, COMMAND_CHANGE_LED_LENGTH, buf, 1);
}

uint32_t
ledstick_set_all_brightness(ledstick_context_t *ctx, uint8_t brightness) {
  uint8_t buf = brightness & 0x1F;
  return nsx_i2c_write_sequential_regs(ctx->i2c_config, ctx->addr, COMMAND_WRITE_ALL_LED_BRIGHTNESS, &buf, 1);
}

uint32_t
ledstick_set_brightness(ledstick_context_t *ctx, uint8_t number, uint8_t brightness) {
  uint8_t buf[2] = { number, brightness };
  buf[1] &= 0x1F;
  return nsx_i2c_write_sequential_regs(ctx->i2c_config, ctx->addr, COMMAND_WRITE_SINGLE_LED_BRIGHTNESS, buf, 2);
}
