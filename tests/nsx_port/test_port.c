/* SPDX-License-Identifier: BSD-3-Clause */
#include <nsx/sensors/port_nsx.h>
#include <nsx/sensors/ledstick.h>
#include <nsx/sensors/bme688.h>
#include <nsx/sensors/ina228.h>
#include <nsx/sensors/mpu6050.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
static unsigned calls, acquisitions, releases;
static int held;
static nsx_sensor_status_t lock_status;
static uint32_t sdk_status, route, selector, prefix_len;
static uint64_t prefix;
static uint8_t sent[256];
static size_t sent_size;
static nsx_sensor_status_t acquire(void *user) {
    assert(user == &held); ++acquisitions;
    if (lock_status) return lock_status;
    assert(!held); held = 1; return NSX_SENSOR_OK;
}
static void release(void *user) { assert(user == &held && held); held = 0; ++releases; }
static uint32_t record(unsigned which, const void *tx, size_t tx_size, void *rx, size_t rx_size) {
    assert(held); ++calls; route = which; sent_size = tx_size;
    if (tx_size) memcpy(sent, tx, tx_size);
    if (which == 1 || which == 4) {
        assert(!((uintptr_t)tx % sizeof(uint32_t)));
        /* Model HAL's whole-word final TX fetch, including tail padding. */
        const uint8_t *bytes = tx;
        for (size_t i = tx_size; i < ((tx_size + 3) & ~(size_t)3); ++i) assert(bytes[i] == 0);
    }
    if (rx_size) { assert(!((uintptr_t)rx % sizeof(uint32_t))); memset(rx, 0x61, rx_size); }
    return sdk_status;
}
uint32_t nsx_i2c_write(nsx_i2c_config_t *c, const void *b, uint32_t n, uint16_t a) {
    assert(c->iomHandle); selector=a; return record(1,b,n,NULL,0);
}
uint32_t nsx_i2c_read(nsx_i2c_config_t *c, const void *b, uint32_t n, uint16_t a) {
    assert(c->iomHandle); selector=a; return record(2,NULL,0,(void *)b,n);
}
uint32_t nsx_i2c_write_read_combined(nsx_i2c_config_t *c, uint16_t a, const void *t, size_t nt, void *r, size_t nr) {
    assert(c->iomHandle); selector=a; uint32_t status=record(3,t,nt,r,nr);
    if(nt==1 && nr==1 && *(const uint8_t *)t==0x75) *(uint8_t *)r=0x68;
    if(nt==1 && *(const uint8_t *)t==0xf0) memset(r,1,nr);
    if(nt==1 && nr==2 && *(const uint8_t *)t==0x3e) { ((uint8_t *)r)[0]=0x54; ((uint8_t *)r)[1]=0x49; }
    if(nt==1 && nr==2 && *(const uint8_t *)t==0x3f) { ((uint8_t *)r)[0]=0x22; ((uint8_t *)r)[1]=0x81; }
    return status;
}
uint32_t nsx_spi_write(nsx_spi_config_t *c, const void *b, uint32_t n, uint64_t p, uint32_t np, uint32_t cs) {
    assert(c->iomHandle); selector=cs; prefix=p; prefix_len=np; return record(4,b,n,NULL,0);
}
uint32_t nsx_spi_read(nsx_spi_config_t *c, const void *b, uint32_t n, uint64_t p, uint32_t np, uint32_t cs) {
    assert(c->iomHandle); selector=cs; prefix=p; prefix_len=np; return record(5,NULL,0,(void *)b,n);
}
int main(void) {
    nsx_i2c_config_t i2c = { &i2c, { AM_HAL_IOM_I2C_MODE } };
    nsx_spi_config_t spi = { &spi, { AM_HAL_IOM_SPI_MODE } };
    nsx_sensor_nsx_lock_t lock = { &held, acquire, release };
    nsx_sensor_nsx_context_t a, b;
    assert(nsx_sensor_nsx_bind_i2c(&a,&i2c,0x76,&lock)==NSX_SENSOR_OK);
    assert(nsx_sensor_nsx_bind_spi(&b,&spi,2,&lock)==NSX_SENSOR_OK);
    assert(!calls && !acquisitions);
    nsx_sensor_nsx_context_t saved = a;
    assert(nsx_sensor_nsx_bind_i2c(&a,&i2c,0x80,&lock)==NSX_SENSOR_E_ARGUMENT);
    assert(!memcmp(&a,&saved,sizeof(a)));
    assert(nsx_sensor_nsx_bind_spi(&b,&spi,4,&lock)==NSX_SENSOR_E_ARGUMENT);
    nsx_sensor_nsx_lock_t bad = {0, acquire, NULL};
    assert(nsx_sensor_nsx_bind_i2c(&a,&i2c,0x76,&bad)==NSX_SENSOR_E_ARGUMENT);
    assert(nsx_sensor_nsx_bind_i2c(NULL,&i2c,0x76,NULL)==NSX_SENSOR_E_ARGUMENT);
    i2c.sIomCfg.eInterfaceMode = AM_HAL_IOM_SPI_MODE;
    assert(nsx_sensor_nsx_bind_i2c(&a,&i2c,0x76,NULL)==NSX_SENSOR_E_ARGUMENT);
    i2c.sIomCfg.eInterfaceMode = AM_HAL_IOM_I2C_MODE;
    uint8_t tx[257], rx[258]; memset(tx,0xab,sizeof(tx)); memset(rx,0xee,sizeof(rx));
    nsx_sensor_transfer_t x={tx+1,1,rx+1,17};
    assert(nsx_sensor_bus_transfer(&a.bus,&x)==NSX_SENSOR_OK);
    assert(route==3 && selector==0x76 && sent_size==1 && sent[0]==0xab);
    assert(rx[0]==0xee && rx[18]==0xee && rx[1]==0x61);
    x.tx_size=AM_HAL_IOM_MAX_OFFSETSIZE;
    assert(nsx_sensor_bus_transfer(&b.bus,&x)==NSX_SENSOR_OK);
    uint64_t expected=0; for(size_t i=0;i<x.tx_size;++i) expected=(expected<<8)|0xab;
    assert(route==5 && selector==2 && prefix==expected && prefix_len==x.tx_size);
    unsigned before=calls;
    ++x.tx_size;
    assert(nsx_sensor_bus_transfer(&a.bus,&x)==NSX_SENSOR_E_UNSUPPORTED);
    assert(nsx_sensor_bus_transfer(&b.bus,&x)==NSX_SENSOR_E_UNSUPPORTED);
    assert(calls==before);
    x=(nsx_sensor_transfer_t){tx+1,5,NULL,0};
    assert(nsx_sensor_bus_transfer(&a.bus,&x)==NSX_SENSOR_OK && route==1);
    assert(nsx_sensor_bus_transfer(&b.bus,&x)==NSX_SENSOR_OK && route==4 && !prefix_len && !prefix);
    x=(nsx_sensor_transfer_t){NULL,0,rx+1,256};
    assert(nsx_sensor_bus_transfer(&a.bus,&x)==NSX_SENSOR_OK && route==2);
    assert(nsx_sensor_bus_transfer(&b.bus,&x)==NSX_SENSOR_OK && route==5 && !prefix_len);
    x.rx_size=257; before=calls;
    assert(nsx_sensor_bus_transfer(&a.bus,&x)==NSX_SENSOR_E_UNSUPPORTED && calls==before);
    x=(nsx_sensor_transfer_t){tx+1,256,NULL,0};
    assert(nsx_sensor_bus_transfer(&a.bus,&x)==NSX_SENSOR_OK && sent_size==256);
    x=(nsx_sensor_transfer_t){NULL,0,rx+1,17}; memset(rx,0xee,sizeof(rx)); sdk_status=0x1234;
    assert(nsx_sensor_bus_transfer(&a.bus,&x)==NSX_SENSOR_E_IO);
    assert(a.last_sdk_status==sdk_status && b.last_sdk_status==0);
    for(size_t i=0;i<sizeof(rx);++i) assert(rx[i]==0xee);
    assert(!held && acquisitions==releases);
    assert(nsx_sensor_bus_transfer(&b.bus,&x)==NSX_SENSOR_E_IO);
    assert(b.last_sdk_status==sdk_status);
    for(size_t i=0;i<sizeof(rx);++i) assert(rx[i]==0xee);
    nsx_sensor_transfer_t empty={0}; before=calls;
    assert(a.bus.ops->transfer(a.bus.user,&empty)==NSX_SENSOR_E_ARGUMENT && calls==before);
    lock_status=NSX_SENSOR_E_BUSY; before=calls;
    assert(nsx_sensor_bus_transfer(&a.bus,&x)==NSX_SENSOR_E_BUSY && calls==before);
    assert(a.last_sdk_status==0x1234 && acquisitions==releases+1);
    lock_status=NSX_SENSOR_OK; sdk_status=0;
    /* Exercise actual portable drivers through this adapter. */
    ledstick_context_t led; ledstick_config_t cfg={8};
    assert(ledstick_bind(&led,&a.bus)==NSX_SENSOR_OK);
    assert(ledstick_init(&led,&cfg)==NSX_SENSOR_OK);
    assert(route==1 && selector==0x76 && sent[0]==0x78);
    bme688_context_t sensor;
    assert(bme688_bind(&sensor,&a.bus,NULL)==NSX_SENSOR_OK);
    bme688_identity_t identity;
    assert(bme688_probe(&sensor,&identity)==NSX_SENSOR_OK);
    assert(route==3 && sent[0]==0xf0);
    ina228_context_t monitor; ina228_identity_t monitor_id;
    assert(ina228_bind(&monitor,&a.bus)==NSX_SENSOR_OK);
    assert(ina228_probe(&monitor,&monitor_id)==NSX_SENSOR_OK);
    assert(monitor_id.manufacturer_id==0x5449 && monitor_id.device_id==0x2281);
    assert(nsx_sensor_nsx_bind_i2c(&a,&i2c,MPU_I2CADDRESS_AD0_HIGH,&lock)==NSX_SENSOR_OK);
    mpu6050_context_t motion; uint8_t motion_id;
    assert(mpu6050_bind(&motion,&a.bus,NULL)==NSX_SENSOR_OK);
    assert(mpu6050_probe(&motion,&motion_id)==NSX_SENSOR_OK && motion_id==0x68);
    assert(route==3 && selector==0x69 && sent_size==1 && sent[0]==0x75);
    /* Explicitly serialized application can omit hooks. Binding still has no I/O. */
    before=calls; assert(nsx_sensor_nsx_bind_i2c(&a,&i2c,0x20,NULL)==NSX_SENSOR_OK);
    assert(calls==before); held=1;
    assert(nsx_sensor_bus_transfer(&a.bus,&x)==NSX_SENSOR_OK && selector==0x20); held=0;
    puts("NSX port contracts passed"); return 0;
}
