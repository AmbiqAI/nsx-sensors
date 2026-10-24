#ifndef TEST_NSX_SPI_H
#define TEST_NSX_SPI_H
#include "nsx_i2c.h"
typedef struct { void *iomHandle; am_hal_iom_config_t sIomCfg; } nsx_spi_config_t;
#ifdef __cplusplus
extern "C" {
#endif
uint32_t nsx_spi_read(nsx_spi_config_t *, const void *, uint32_t, uint64_t, uint32_t, uint32_t);
uint32_t nsx_spi_write(nsx_spi_config_t *, const void *, uint32_t, uint64_t, uint32_t, uint32_t);
#ifdef __cplusplus
}
#endif
#endif
