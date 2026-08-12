/**
 * Host-test stub of the nsx-i2c API surface used by src/nsx_ina228.c.
 *
 * The real nsx_i2c.h drags in AmbiqSuite headers, which do not exist on a
 * host CI runner. This stub declares only the types and transfer functions
 * the INA228 driver calls; the fake register-map implementation lives in
 * test_nsx_ina228.c. It reuses the real header's include guard so it cannot
 * be combined with the real transport by accident.
 */
#ifndef NSX_I2C
#define NSX_I2C

#include <stddef.h>
#include <stdint.h>

typedef struct {
    int8_t iom;
} nsx_i2c_config_t;

uint32_t nsx_i2c_write(nsx_i2c_config_t *cfg, const void *buf, uint32_t size, uint16_t addr);

uint32_t nsx_i2c_write_read(
    nsx_i2c_config_t *cfg, uint16_t addr, const void *writeBuf, size_t numWrite, void *readBuf,
    size_t numRead);

#endif // NSX_I2C
