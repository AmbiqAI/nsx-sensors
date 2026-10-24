/* SPDX-License-Identifier: BSD-3-Clause */
#include <nsx/sensors/port_nsx.h>
#include <string.h>
#ifndef NSX_I2C_COMBINED_MAX_WRITE_SIZE
#error "NSX sensor port requires nsx-i2c with nsx_i2c_write_read_combined (SDK PR #130)"
#endif

static nsx_sensor_status_t transfer(void *user, const nsx_sensor_transfer_t *x)
{
    nsx_sensor_nsx_context_t *ctx = user;
    /* Validate even direct ops calls; never truncate, split or silently retry. */
    if (!ctx || !x || (!x->tx_size && !x->rx_size) ||
        (!!x->tx != !!x->tx_size) || (!!x->rx != !!x->rx_size))
        return NSX_SENSOR_E_ARGUMENT;
    if (x->tx_size > sizeof(ctx->_scratch) || x->rx_size > sizeof(ctx->_scratch) ||
        (x->tx_size && x->rx_size && x->tx_size > AM_HAL_IOM_MAX_OFFSETSIZE))
        return NSX_SENSOR_E_UNSUPPORTED;
    if ((!ctx->_i2c && !ctx->_spi) || (ctx->_i2c && ctx->_spi))
        return NSX_SENSOR_E_STATE;
    if (ctx->lock.acquire) {
        nsx_sensor_status_t status = ctx->lock.acquire(ctx->lock.user);
        if (status != NSX_SENSOR_OK) return status;
    }
    /* HAL TX may load a whole final word. Scratch supplies alignment and padding;
     * staging RX prevents publishing partial data on failure. */
    memset(ctx->_scratch, 0, sizeof(ctx->_scratch));
    uint32_t status;
    if (ctx->_i2c) {
        if (x->tx_size && x->rx_size) {
            status = nsx_i2c_write_read_combined(ctx->_i2c, ctx->_address,
                x->tx, x->tx_size, ctx->_scratch, x->rx_size);
        } else if (x->rx_size) {
            status = nsx_i2c_read(ctx->_i2c, ctx->_scratch, (uint32_t)x->rx_size, ctx->_address);
        } else {
            memcpy(ctx->_scratch, x->tx, x->tx_size);
            status = nsx_i2c_write(ctx->_i2c, ctx->_scratch, (uint32_t)x->tx_size, ctx->_address);
        }
    } else if (x->rx_size) {
        uint64_t prefix = 0;
        for (size_t i = 0; i < x->tx_size; ++i) prefix = (prefix << 8) | x->tx[i];
        status = nsx_spi_read(ctx->_spi, ctx->_scratch, (uint32_t)x->rx_size,
            prefix, (uint32_t)x->tx_size, ctx->_chip_select);
    } else {
        memcpy(ctx->_scratch, x->tx, x->tx_size);
        status = nsx_spi_write(ctx->_spi, ctx->_scratch, (uint32_t)x->tx_size,
            0, 0, ctx->_chip_select);
    }
    ctx->last_sdk_status = status;
    if (!status && x->rx_size) memcpy(x->rx, ctx->_scratch, x->rx_size);
    if (ctx->lock.release) ctx->lock.release(ctx->lock.user);
    /* Legacy SDK helpers collapse errors to 1. Do not mislabel these as a
     * particular HAL error (e.g. timeout); preserve the helper result above. */
    return status ? NSX_SENSOR_E_IO : NSX_SENSOR_OK;
}
static const nsx_sensor_bus_ops_t ops = { transfer };

static nsx_sensor_status_t bind(nsx_sensor_nsx_context_t *ctx,
    nsx_i2c_config_t *i2c, nsx_spi_config_t *spi, uint16_t address,
    uint32_t chip_select, const nsx_sensor_nsx_lock_t *lock)
{
    if (!ctx || (lock && (!!lock->acquire != !!lock->release))) return NSX_SENSOR_E_ARGUMENT;
    nsx_sensor_nsx_lock_t copy = lock ? *lock : (nsx_sensor_nsx_lock_t){0};
    *ctx = (nsx_sensor_nsx_context_t){
        .bus = { &ops, ctx, spi ? NSX_SENSOR_BUS_SPI : NSX_SENSOR_BUS_I2C,
                 sizeof(ctx->_scratch), sizeof(ctx->_scratch) },
        .lock = copy, ._i2c = i2c, ._spi = spi, ._address = address, ._chip_select = chip_select
    };
    return NSX_SENSOR_OK;
}
nsx_sensor_status_t nsx_sensor_nsx_bind_i2c(nsx_sensor_nsx_context_t *ctx,
    nsx_i2c_config_t *config, uint16_t address, const nsx_sensor_nsx_lock_t *lock)
{
    if (!config || !config->iomHandle || config->sIomCfg.eInterfaceMode != AM_HAL_IOM_I2C_MODE ||
        address < 0x08 || address > 0x77) return NSX_SENSOR_E_ARGUMENT;
    return bind(ctx, config, NULL, address, 0, lock);
}
nsx_sensor_status_t nsx_sensor_nsx_bind_spi(nsx_sensor_nsx_context_t *ctx,
    nsx_spi_config_t *config, uint32_t chip_select, const nsx_sensor_nsx_lock_t *lock)
{
    if (!config || !config->iomHandle || config->sIomCfg.eInterfaceMode != AM_HAL_IOM_SPI_MODE ||
        chip_select > 3) return NSX_SENSOR_E_ARGUMENT;
    return bind(ctx, NULL, config, 0, chip_select, lock);
}
