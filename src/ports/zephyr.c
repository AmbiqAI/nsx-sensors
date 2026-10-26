/* SPDX-License-Identifier: BSD-3-Clause */
#include <nsx/sensors/port_zephyr.h>
#include <zephyr/kernel.h>
#include <errno.h>
#include <string.h>

nsx_sensor_status_t nsx_sensor_zephyr_status(int error)
{
    switch (error) {
    case 0: return NSX_SENSOR_OK;
    case -EAGAIN: return NSX_SENSOR_AGAIN;
    case -EINVAL: return NSX_SENSOR_E_ARGUMENT;
    case -ENODEV: return NSX_SENSOR_E_DEVICE;
    case -EBUSY: return NSX_SENSOR_E_BUSY;
    case -ETIMEDOUT: return NSX_SENSOR_E_TIMEOUT;
    case -ENOSYS: case -ENOTSUP: return NSX_SENSOR_E_UNSUPPORTED;
    case -EOVERFLOW: return NSX_SENSOR_E_OVERFLOW;
    default: return NSX_SENSOR_E_IO;
    }
}
int nsx_sensor_zephyr_errno(nsx_sensor_status_t status)
{
    switch (status) {
    case NSX_SENSOR_OK: return 0;
    case NSX_SENSOR_AGAIN: return -EAGAIN;
    case NSX_SENSOR_E_ARGUMENT: return -EINVAL;
    case NSX_SENSOR_E_DEVICE: return -ENODEV;
    case NSX_SENSOR_E_STATE: return -EACCES;
    case NSX_SENSOR_E_BUSY: return -EBUSY;
    case NSX_SENSOR_E_TIMEOUT: return -ETIMEDOUT;
    case NSX_SENSOR_E_UNSUPPORTED: return -ENOTSUP;
    case NSX_SENSOR_E_OVERFLOW: return -EOVERFLOW;
    default: return -EIO;
    }
}
static nsx_sensor_status_t transfer(void *user, const nsx_sensor_transfer_t *x)
{
    nsx_sensor_zephyr_context_t *ctx = user;
    if (!ctx || !x || (!x->tx_size && !x->rx_size) ||
        (x->tx_size && !x->tx) || (x->rx_size && !x->rx))
        return NSX_SENSOR_E_ARGUMENT;
    if (x->tx_size > NSX_SENSOR_ZEPHYR_MAX_TRANSFER ||
        x->rx_size > NSX_SENSOR_ZEPHYR_MAX_TRANSFER)
        return NSX_SENSOR_E_UNSUPPORTED;
    int result = -ENOTSUP;
    if (ctx->bus.kind == NSX_SENSOR_BUS_I2C) {
#ifdef CONFIG_I2C
        struct i2c_msg msgs[2];
        uint8_t count = 0;
        if (x->tx_size) {
            memcpy(ctx->_tx, x->tx, x->tx_size);
            msgs[count++] = (struct i2c_msg){ .buf = ctx->_tx,
                .len = x->tx_size, .flags = x->rx_size ? I2C_MSG_WRITE : I2C_MSG_STOP };
        }
        if (x->rx_size) {
            msgs[count++] = (struct i2c_msg){ .buf = ctx->_rx,
                .len = x->rx_size, .flags = I2C_MSG_READ | I2C_MSG_STOP |
                    (x->tx_size ? I2C_MSG_RESTART : 0) };
        }
        result = i2c_transfer_dt(&ctx->_i2c, msgs, count);
#endif
    } else if (ctx->bus.kind == NSX_SENSOR_BUS_SPI) {
#ifdef CONFIG_SPI
        size_t length = x->tx_size + x->rx_size;
        memset(ctx->_tx, 0, length);
        if (x->tx_size) memcpy(ctx->_tx, x->tx, x->tx_size);
        struct spi_buf tx = { ctx->_tx, length }, rx = { ctx->_rx, length };
        struct spi_buf_set tx_set = { &tx, 1 }, rx_set = { &rx, 1 };
        result = spi_transceive_dt(ctx->_spi, &tx_set, &rx_set);
#endif
    } else return NSX_SENSOR_E_STATE;
    ctx->last_native_status = result;
    if (!result && x->rx_size)
        memcpy(x->rx, ctx->_rx + (ctx->bus.kind == NSX_SENSOR_BUS_SPI ? x->tx_size : 0), x->rx_size);
    return nsx_sensor_zephyr_status(result);
}
static const nsx_sensor_bus_ops_t ops = { transfer };
nsx_sensor_status_t nsx_sensor_zephyr_bind_i2c(nsx_sensor_zephyr_context_t *ctx,
                                            const struct i2c_dt_spec *spec)
{
    if (!ctx || !spec || !spec->bus || spec->addr < 0x08 || spec->addr > 0x77)
        return NSX_SENSOR_E_ARGUMENT;
#ifndef CONFIG_I2C
    return NSX_SENSOR_E_UNSUPPORTED;
#else
    struct i2c_dt_spec copy = *spec;
    *ctx = (nsx_sensor_zephyr_context_t){
        .bus = { &ops, ctx, NSX_SENSOR_BUS_I2C,
                 NSX_SENSOR_ZEPHYR_MAX_TRANSFER, NSX_SENSOR_ZEPHYR_MAX_TRANSFER },
        ._i2c = copy };
    return NSX_SENSOR_OK;
#endif
}
nsx_sensor_status_t nsx_sensor_zephyr_bind_spi(nsx_sensor_zephyr_context_t *ctx,
                                            const struct spi_dt_spec *spec)
{
    if (!ctx || !spec || !spec->bus || !spec->config.frequency)
        return NSX_SENSOR_E_ARGUMENT;
    if (SPI_WORD_SIZE_GET(spec->config.operation) != 8 ||
        (spec->config.operation & (SPI_OP_MODE_SLAVE | SPI_TRANSFER_LSB |
         SPI_HALF_DUPLEX | SPI_HOLD_ON_CS | SPI_LOCK_ON | SPI_LINES_MASK |
         SPI_FRAME_FORMAT_TI)))
        return NSX_SENSOR_E_UNSUPPORTED;
#ifndef CONFIG_SPI
    return NSX_SENSOR_E_UNSUPPORTED;
#else
    *ctx = (nsx_sensor_zephyr_context_t){
        .bus = { &ops, ctx, NSX_SENSOR_BUS_SPI,
                 NSX_SENSOR_ZEPHYR_MAX_TRANSFER, NSX_SENSOR_ZEPHYR_MAX_TRANSFER },
        /* Preserve caller configuration identity: Zephyr controllers may cache
         * this pointer and skip reconfiguration when the address is unchanged. */
        ._spi = spec };
    return NSX_SENSOR_OK;
#endif
}
static uint64_t now_us(void *user)
{
    (void)user;
    return k_ticks_to_us_floor64(k_uptime_ticks());
}
static void delay_us(void *user, uint32_t duration_us)
{
    (void)user;
    /* k_usleep may return early when explicitly woken; enforce elapsed ticks.
     * Round up to ensure the portable runtime's minimum-delay contract. */
    int64_t end = k_uptime_ticks() + k_us_to_ticks_ceil64(duration_us) + 1;
    for (;;) {
        int64_t remaining = end - k_uptime_ticks();
        if (remaining <= 0) break;
        k_sleep(K_TICKS(remaining));
    }
}
nsx_sensor_runtime_t nsx_sensor_zephyr_runtime(void)
{
    return (nsx_sensor_runtime_t){ .now_us = now_us, .delay_us = delay_us };
}
