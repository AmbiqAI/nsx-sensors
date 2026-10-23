#ifndef BME688_TEST_NSX_I2C_H
#define BME688_TEST_NSX_I2C_H
#include <stddef.h>
#include <stdint.h>
typedef struct { void *test_device; } nsx_i2c_config_t;
uint32_t nsx_i2c_write_read(nsx_i2c_config_t *, uint16_t, const void *, size_t, void *, size_t);
uint32_t nsx_i2c_write(nsx_i2c_config_t *, const void *, uint32_t, uint16_t);
#endif
