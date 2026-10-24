#ifndef TEST_NSX_I2C_H
#define TEST_NSX_I2C_H
#include <stddef.h>
#include <stdint.h>
#ifndef AM_HAL_IOM_MAX_OFFSETSIZE
#define AM_HAL_IOM_MAX_OFFSETSIZE 3
#endif
#define NSX_I2C_COMBINED_MAX_WRITE_SIZE AM_HAL_IOM_MAX_OFFSETSIZE
#define AM_HAL_IOM_I2C_MODE 1
#define AM_HAL_IOM_SPI_MODE 2
typedef struct { int eInterfaceMode; } am_hal_iom_config_t;
typedef struct { void *iomHandle; am_hal_iom_config_t sIomCfg; } nsx_i2c_config_t;
#ifdef __cplusplus
extern "C" {
#endif
uint32_t nsx_i2c_read(nsx_i2c_config_t *, const void *, uint32_t, uint16_t);
uint32_t nsx_i2c_write(nsx_i2c_config_t *, const void *, uint32_t, uint16_t);
uint32_t nsx_i2c_write_read_combined(nsx_i2c_config_t *, uint16_t, const void *, size_t, void *, size_t);
#ifdef __cplusplus
}
#endif
#endif
