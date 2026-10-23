#ifndef BME688_TEST_NSX_SPI_H
#define BME688_TEST_NSX_SPI_H
#include <stdint.h>
typedef struct { void *test_device; } nsx_spi_config_t;
uint32_t nsx_spi_read(nsx_spi_config_t *, const void *, uint32_t, uint64_t, uint32_t, uint32_t);
uint32_t nsx_spi_write(nsx_spi_config_t *, const void *, uint32_t, uint64_t, uint32_t, uint32_t);
#endif
