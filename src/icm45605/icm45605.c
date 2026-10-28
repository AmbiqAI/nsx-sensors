/* SPDX-License-Identifier: BSD-3-Clause */
#include <nsx/sensors/icm45605.h>
#include "inv_imu_driver.h"
#include <math.h>
#include <stddef.h>
#include <string.h>
/* TDK casts the device address to its first transport member. */
_Static_assert(offsetof(nsx_icm45605_tdk_device_t, transport)==0, "TDK transport must be first");
_Static_assert(sizeof(nsx_icm45605_tdk_sensor_data_t)==14, "UI sample must be 14 bytes");
static nsx_sensor_status_t ready(const icm45605_context_t *c)
{
    if (!c) return NSX_SENSOR_E_ARGUMENT;
    return c->_bound && c->_initialized ? NSX_SENSOR_OK : NSX_SENSOR_E_STATE;
}
static void begin(icm45605_context_t *c) { c->_io_status=NSX_SENSOR_OK; }
static nsx_sensor_status_t result(icm45605_context_t *c, int status)
{
    if (c->_io_status!=NSX_SENSOR_OK) return c->_io_status;
    return status==0 ? NSX_SENSOR_OK : NSX_SENSOR_E_DEVICE;
}
static nsx_sensor_status_t mutation(icm45605_context_t *c, int status)
{
    nsx_sensor_status_t s=result(c,status);
    if (s!=NSX_SENSOR_OK) { c->_initialized=false; c->_running=false; }
    return s;
}
static int read_reg(void *user, uint8_t reg, uint8_t *data, uint32_t len)
{
    icm45605_context_t *c=user;
    /* Vendor routines may continue after a failed read. Initialize their
     * temporaries and latch the first failure; suppress later physical I/O. */
    memset(data,0,len);
    if (c->_io_status!=NSX_SENSOR_OK) return -1;
    uint8_t address=reg|0x80;
    const nsx_sensor_transfer_t t={&address,1,data,len};
    c->_io_status=nsx_sensor_bus_transfer(&c->bus,&t);
    if (c->_io_status!=NSX_SENSOR_OK) { memset(data,0,len); return -1; }
    return 0;
}
static int write_reg(void *user, uint8_t reg, const uint8_t *data, uint32_t len)
{
    icm45605_context_t *c=user;
    if (c->_io_status!=NSX_SENSOR_OK) return -1;
    uint8_t bytes[4];
    if (len>sizeof(bytes)-1) { c->_io_status=NSX_SENSOR_E_UNSUPPORTED; return -1; }
    bytes[0]=reg; memcpy(bytes+1,data,len);
    const nsx_sensor_transfer_t t={bytes,len+1,NULL,0};
    c->_io_status=nsx_sensor_bus_transfer(&c->bus,&t);
    return c->_io_status==NSX_SENSOR_OK?0:-1;
}
static void delay(void *user, uint32_t us)
{
    icm45605_context_t *c=user;
    if (c->_io_status==NSX_SENSOR_OK) c->runtime.delay_us(c->runtime.user,us);
}
icm45605_config_t icm45605_default_config(void)
{
    const icm45605_config_t c={ICM45605_ACCEL_4G,ICM45605_GYRO_500DPS,
        ICM45605_ODR_100HZ,ICM45605_FILTER_ODR_DIV4,ICM45605_FILTER_ODR_DIV4,false};
    return c;
}
nsx_sensor_status_t icm45605_validate_config(const icm45605_config_t *c)
{
    if (!c || (unsigned)c->accel_range<1 || (unsigned)c->accel_range>4 ||
        (unsigned)c->gyro_range<1 || (unsigned)c->gyro_range>8 ||
        (unsigned)c->odr<3 || (unsigned)c->odr>12 ||
        (unsigned)c->accel_filter>6 || (unsigned)c->gyro_filter>6) return NSX_SENSOR_E_ARGUMENT;
    return NSX_SENSOR_OK;
}
nsx_sensor_status_t icm45605_bind(icm45605_context_t *c, const nsx_sensor_bus_t *bus,
                                 const nsx_sensor_runtime_t *runtime)
{
    if (!c) return NSX_SENSOR_E_ARGUMENT;
    nsx_sensor_status_t s=nsx_sensor_bus_validate(bus); if (s!=NSX_SENSOR_OK) return s;
    if (bus->kind!=NSX_SENSOR_BUS_SPI || bus->max_tx_size<4 || bus->max_rx_size<14)
        return NSX_SENSOR_E_UNSUPPORTED;
    icm45605_context_t next={0}; next.bus=*bus; if (runtime) next.runtime=*runtime;
    next._device.transport.user=c;
    next._device.transport.read_reg=read_reg; next._device.transport.write_reg=write_reg;
    next._device.transport.sleep_us=delay; next._device.transport.serif_type=UI_SPI4;
    next._bound=true; *c=next; return NSX_SENSOR_OK;
}
nsx_sensor_status_t icm45605_probe(icm45605_context_t *c, uint8_t *identity)
{
    if (!c || !identity) return NSX_SENSOR_E_ARGUMENT;
    if (!c->_bound) return NSX_SENSOR_E_STATE;
    begin(c); uint8_t id=0;
    nsx_sensor_status_t s=result(c,nsx_icm45605_tdk_get_who_am_i(&c->_device,&id));
    if (s!=NSX_SENSOR_OK) return s;
    if (id!=INV_IMU_WHOAMI) return NSX_SENSOR_E_DEVICE;
    *identity=id; return NSX_SENSOR_OK;
}
static int interrupts(icm45605_context_t *c, bool enable)
{
    const nsx_icm45605_tdk_int_pin_config_t pin={INTX_CONFIG2_INTX_POLARITY_HIGH,
        INTX_CONFIG2_INTX_MODE_PULSE,INTX_CONFIG2_INTX_DRIVE_PP};
    int r=nsx_icm45605_tdk_set_pin_config_int(&c->_device,INV_IMU_INT2,&pin);
    if (r) return r;
    nsx_icm45605_tdk_int_state_t flags={0}; flags.INV_UI_DRDY=enable;
    return nsx_icm45605_tdk_set_config_int(&c->_device,INV_IMU_INT2,&flags);
}
static nsx_sensor_status_t configure(icm45605_context_t *c, const icm45605_config_t *v)
{
    begin(c); int r;
#define APPLY(call) do { r=(call); if (r) return mutation(c,r); } while (0)
    APPLY(nsx_icm45605_tdk_set_accel_fsr(&c->_device,(accel_config0_accel_ui_fs_sel_t)v->accel_range));
    APPLY(nsx_icm45605_tdk_set_gyro_fsr(&c->_device,(gyro_config0_gyro_ui_fs_sel_t)v->gyro_range));
    APPLY(nsx_icm45605_tdk_set_accel_frequency(&c->_device,(accel_config0_accel_odr_t)v->odr));
    APPLY(nsx_icm45605_tdk_set_gyro_frequency(&c->_device,(gyro_config0_gyro_odr_t)v->odr));
    APPLY(nsx_icm45605_tdk_set_accel_ln_bw(&c->_device,(ipreg_sys2_reg_131_accel_ui_lpfbw_t)v->accel_filter));
    APPLY(nsx_icm45605_tdk_set_gyro_ln_bw(&c->_device,(ipreg_sys1_reg_172_gyro_ui_lpfbw_sel_t)v->gyro_filter));
    APPLY(interrupts(c,v->drdy_interrupt));
#undef APPLY
    c->_config=*v; return NSX_SENSOR_OK;
}
nsx_sensor_status_t icm45605_init(icm45605_context_t *c, const icm45605_config_t *config)
{
    nsx_sensor_status_t s=icm45605_validate_config(config); if (s!=NSX_SENSOR_OK) return s;
    if (!c) return NSX_SENSOR_E_ARGUMENT;
    if (!c->_bound) return NSX_SENSOR_E_STATE;
    if (!c->runtime.delay_us) return NSX_SENSOR_E_UNSUPPORTED;
    icm45605_config_t copy=*config;
    c->_initialized=false; c->_running=false;
    c->runtime.delay_us(c->runtime.user,1000); /* Power-on register access. */
    uint8_t id; s=icm45605_probe(c,&id); if (s!=NSX_SENSOR_OK) return s;
    begin(c); s=mutation(c,nsx_icm45605_tdk_soft_reset(&c->_device)); if (s!=NSX_SENSOR_OK) return s;
    s=configure(c,&copy); if (s!=NSX_SENSOR_OK) return s;
    memset(&c->_bias,0,sizeof(c->_bias)); c->_initialized=true;
    return NSX_SENSOR_OK;
}
nsx_sensor_status_t icm45605_configure(icm45605_context_t *c, const icm45605_config_t *config)
{
    nsx_sensor_status_t s=icm45605_validate_config(config); if (s!=NSX_SENSOR_OK) return s;
    s=ready(c); if (s!=NSX_SENSOR_OK) return s;
    if (c->_running) return NSX_SENSOR_E_STATE;
    icm45605_config_t copy=*config; return configure(c,&copy);
}
#define SETTER(name,field,type,vendor_type,vendor_fn) \
nsx_sensor_status_t icm45605_set_##name(icm45605_context_t *c, type value) \
{ nsx_sensor_status_t s=ready(c); if (s!=NSX_SENSOR_OK) return s; \
  icm45605_config_t copy=c->_config; copy.field=value; \
  s=icm45605_validate_config(&copy); if (s!=NSX_SENSOR_OK) return s; \
  if (c->_running) return NSX_SENSOR_E_STATE; \
  begin(c); s=mutation(c,vendor_fn(&c->_device,(vendor_type)value)); \
  if (s==NSX_SENSOR_OK) c->_config=copy; \
  return s; }
SETTER(accel_range,accel_range,icm45605_accel_range_t,accel_config0_accel_ui_fs_sel_t,nsx_icm45605_tdk_set_accel_fsr)
SETTER(gyro_range,gyro_range,icm45605_gyro_range_t,gyro_config0_gyro_ui_fs_sel_t,nsx_icm45605_tdk_set_gyro_fsr)
SETTER(accel_filter,accel_filter,icm45605_filter_t,ipreg_sys2_reg_131_accel_ui_lpfbw_t,nsx_icm45605_tdk_set_accel_ln_bw)
SETTER(gyro_filter,gyro_filter,icm45605_filter_t,ipreg_sys1_reg_172_gyro_ui_lpfbw_sel_t,nsx_icm45605_tdk_set_gyro_ln_bw)
#undef SETTER
nsx_sensor_status_t icm45605_set_odr(icm45605_context_t *c, icm45605_odr_t value)
{
    nsx_sensor_status_t s=ready(c); if (s!=NSX_SENSOR_OK) return s;
    icm45605_config_t copy=c->_config; copy.odr=value;
    s=icm45605_validate_config(&copy); if (s!=NSX_SENSOR_OK) return s;
    if (c->_running) return NSX_SENSOR_E_STATE;
    begin(c); int r=nsx_icm45605_tdk_set_accel_frequency(&c->_device,(accel_config0_accel_odr_t)value);
    if (!r) r=nsx_icm45605_tdk_set_gyro_frequency(&c->_device,(gyro_config0_gyro_odr_t)value);
    s=mutation(c,r); if (s==NSX_SENSOR_OK) c->_config=copy; return s;
}
nsx_sensor_status_t icm45605_set_drdy_interrupt(icm45605_context_t *c, bool enable)
{
    nsx_sensor_status_t s=ready(c); if (s!=NSX_SENSOR_OK) return s;
    begin(c); s=mutation(c,interrupts(c,enable));
    if (s==NSX_SENSOR_OK) c->_config.drdy_interrupt=enable;
    return s;
}
static nsx_sensor_status_t power(icm45605_context_t *c, bool on)
{
    nsx_sensor_status_t s=ready(c); if (s!=NSX_SENSOR_OK) return s;
    if (c->_running==on) return NSX_SENSOR_OK;
    begin(c); int r=nsx_icm45605_tdk_set_accel_mode(&c->_device,on?PWR_MGMT0_ACCEL_MODE_LN:PWR_MGMT0_ACCEL_MODE_OFF);
    if (!r) r=nsx_icm45605_tdk_set_gyro_mode(&c->_device,on?PWR_MGMT0_GYRO_MODE_LN:PWR_MGMT0_GYRO_MODE_OFF);
    s=mutation(c,r); if (s!=NSX_SENSOR_OK) return s;
    if (on) {
        /* Vendor startup bound plus one slowest supported output period. */
        c->runtime.delay_us(c->runtime.user,GYR_STARTUP_TIME_US+80000);
    }
    c->_running=on; return NSX_SENSOR_OK;
}
nsx_sensor_status_t icm45605_start(icm45605_context_t *c) { return power(c,true); }
nsx_sensor_status_t icm45605_stop(icm45605_context_t *c) { return power(c,false); }
nsx_sensor_status_t icm45605_read_raw(icm45605_context_t *c, icm45605_raw_sample_t *out)
{
    if (!out) return NSX_SENSOR_E_ARGUMENT;
    nsx_sensor_status_t s=ready(c); if (s!=NSX_SENSOR_OK) return s;
    if (!c->_running) return NSX_SENSOR_E_STATE;
    begin(c); nsx_icm45605_tdk_sensor_data_t raw={0};
    s=result(c,nsx_icm45605_tdk_get_register_data(&c->_device,&raw)); if (s!=NSX_SENSOR_OK) return s;
    icm45605_raw_sample_t v={0};
    for (unsigned i=0;i<3;++i) { v.accel[i]=raw.accel_data[i]; v.gyro[i]=raw.gyro_data[i]; }
    v.temperature=raw.temp_data; *out=v; return NSX_SENSOR_OK;
}
static void scale(icm45605_context_t *c, const icm45605_raw_sample_t *raw, icm45605_sample_t *out)
{
    /* Validated native ranges: ICM-45605 has no +/-32g or +/-4000dps UI setting. */
    float accel=(float)(32u>>(unsigned)c->_config.accel_range)/32768.0f;
    float gyro=(4000.0f/(float)(1u<<(unsigned)c->_config.gyro_range))/32768.0f;
    for (unsigned i=0;i<3;++i) { out->accel_g[i]=raw->accel[i]*accel; out->gyro_dps[i]=raw->gyro[i]*gyro; }
    out->temperature_c=25.0f+raw->temperature/128.0f;
}
nsx_sensor_status_t icm45605_read(icm45605_context_t *c, icm45605_sample_t *out)
{
    if (!out) return NSX_SENSOR_E_ARGUMENT;
    icm45605_raw_sample_t raw; nsx_sensor_status_t s=icm45605_read_raw(c,&raw); if (s!=NSX_SENSOR_OK) return s;
    icm45605_sample_t v; scale(c,&raw,&v);
    for (unsigned i=0;i<3;++i) { v.accel_g[i]-=c->_bias.accel_g[i]; v.gyro_dps[i]-=c->_bias.gyro_dps[i]; }
    *out=v; return NSX_SENSOR_OK;
}
nsx_sensor_status_t icm45605_read_data_ready(icm45605_context_t *c, bool *out)
{
    if (!out) return NSX_SENSOR_E_ARGUMENT;
    nsx_sensor_status_t s=ready(c); if (s!=NSX_SENSOR_OK) return s;
    /* The hardware enable gates the status flag as well as the pin. */
    if (!c->_config.drdy_interrupt) return NSX_SENSOR_E_STATE;
    begin(c); nsx_icm45605_tdk_int_state_t flags={0};
    s=result(c,nsx_icm45605_tdk_get_int_status(&c->_device,INV_IMU_INT1,&flags));
    if (s==NSX_SENSOR_OK && flags.INV_RESET_DONE) s=NSX_SENSOR_E_DEVICE;
    if (s==NSX_SENSOR_OK)
        s=result(c,nsx_icm45605_tdk_get_int_status(&c->_device,INV_IMU_INT2,&flags));
    if (s!=NSX_SENSOR_OK) { c->_initialized=false; c->_running=false; return s; }
    *out=flags.INV_UI_DRDY!=0; return NSX_SENSOR_OK;
}
nsx_sensor_status_t icm45605_set_bias(icm45605_context_t *c, const icm45605_bias_t *bias)
{
    if (!bias) return NSX_SENSOR_E_ARGUMENT;
    for (unsigned i=0;i<3;++i)
        if (!isfinite(bias->accel_g[i]) || !isfinite(bias->gyro_dps[i])) return NSX_SENSOR_E_ARGUMENT;
    nsx_sensor_status_t s=ready(c); if (s!=NSX_SENSOR_OK) return s;
    c->_bias=*bias; return NSX_SENSOR_OK;
}
nsx_sensor_status_t icm45605_calibrate(icm45605_context_t *c, uint16_t samples, uint32_t timeout)
{
    if (!samples || samples>4096 || !timeout) return NSX_SENSOR_E_ARGUMENT;
    nsx_sensor_status_t s=ready(c); if (s!=NSX_SENSOR_OK) return s;
    if (!c->_running) return NSX_SENSOR_E_STATE;
    if (!c->_config.drdy_interrupt) return NSX_SENSOR_E_STATE;
    uint64_t start=c->runtime.now_us?c->runtime.now_us(c->runtime.user):0;
    uint32_t remaining=timeout; double accel[3]={0},gyro[3]={0};
    for (unsigned n=0;n<samples;++n) {
        bool fresh; s=icm45605_read_data_ready(c,&fresh); if (s!=NSX_SENSOR_OK) return s;
        /* Discard prior DRDY even after a sample read: that flag may belong to
         * the sample just consumed. Wait for a subsequent observation. */
        for (;;) {
            if (!remaining || (c->runtime.now_us && c->runtime.now_us(c->runtime.user)-start>=timeout))
                return NSX_SENSOR_E_TIMEOUT;
            uint32_t wait=remaining<1000?remaining:1000;
            c->runtime.delay_us(c->runtime.user,wait); remaining-=wait;
            s=icm45605_read_data_ready(c,&fresh); if (s!=NSX_SENSOR_OK) return s;
            if (fresh) break;
        }
        icm45605_raw_sample_t raw; s=icm45605_read_raw(c,&raw); if (s!=NSX_SENSOR_OK) return s;
        if (c->runtime.now_us && c->runtime.now_us(c->runtime.user)-start>=timeout) return NSX_SENSOR_E_TIMEOUT;
        icm45605_sample_t v; scale(c,&raw,&v);
        for (unsigned i=0;i<3;++i) { accel[i]+=v.accel_g[i]; gyro[i]+=v.gyro_dps[i]; }
    }
    icm45605_bias_t bias;
    for (unsigned i=0;i<3;++i) { bias.accel_g[i]=(float)(accel[i]/samples)-(i==2?1.0f:0.0f); bias.gyro_dps[i]=(float)(gyro[i]/samples); }
    c->_bias=bias; return NSX_SENSOR_OK;
}
