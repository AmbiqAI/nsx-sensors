/* SPDX-License-Identifier: BSD-3-Clause */
#include <nsx/sensors/icm45605.h>
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
typedef struct {
    uint8_t reg[128], indirect[65536];
    uint16_t address;
    unsigned calls, writes, delays, fail_at, samples, generation, last_generation;
    bool fail_after, stuck_reset, big_endian, produce, frozen_clock, race;
    uint64_t time, next_sample;
    int16_t values[7];
} fake_t;
static void encode(fake_t *f)
{
    for (unsigned i=0;i<7;++i) {
        uint16_t v=(uint16_t)f->values[i];
        f->reg[2*i]=(uint8_t)(f->big_endian?v>>8:v);
        f->reg[2*i+1]=(uint8_t)(f->big_endian?v:v>>8);
    }
}
static void reset(fake_t *f)
{
    memset(f->reg,0,sizeof(f->reg)); memset(f->indirect,0,sizeof(f->indirect));
    f->reg[0x72]=0xe5; f->reg[0x16]=0x80; f->reg[0x19]=f->stuck_reset?0:0x80;
    f->indirect[0xa267]=f->big_endian?2:0;
    f->address=0; encode(f);
}
static void generate(fake_t *f)
{
    ++f->generation;
    if (f->reg[0x16]&4) f->reg[0x19]|=4;
    if (f->reg[0x56]&4) f->reg[0x59]|=4;
    encode(f);
}
static nsx_sensor_status_t transfer(void *user, const nsx_sensor_transfer_t *t)
{
    fake_t *f=user; ++f->calls;
    bool fail=f->calls==f->fail_at;
    assert(t->tx && t->tx_size>=1 && t->tx_size<=4 && t->rx_size<=14);
    if (fail && !f->fail_after) return NSX_SENSOR_E_TIMEOUT;
    uint8_t reg=t->tx[0]&0x7f;
    if (t->rx_size) {
        assert(t->tx_size==1 && (t->tx[0]&0x80));
        if (reg==0) {
            if (f->race) { generate(f); f->next_sample=f->time+10000; }
            if (f->produce) { assert(f->generation>f->last_generation); f->last_generation=f->generation; }
            ++f->samples;
        }
        for (size_t i=0;i<t->rx_size;++i)
            t->rx[i]=reg==0x7e ? f->indirect[f->address++] : f->reg[reg+i];
        if (reg==0x19 || reg==0x59) { f->reg[reg]=0; if (t->rx_size>1) f->reg[reg+1]=0; }
    } else {
        assert(!(t->tx[0]&0x80)); ++f->writes;
        if (reg==0x7c) {
            assert(t->tx_size==3 || t->tx_size==4);
            f->address=(uint16_t)((uint16_t)t->tx[1]<<8|t->tx[2]);
            if (t->tx_size==4) f->indirect[f->address++]=t->tx[3];
        } else if (reg==0x7e) {
            for (size_t i=1;i<t->tx_size;++i) f->indirect[f->address++]=t->tx[i];
        } else if (reg==0x7f && (t->tx[1]&2)) reset(f);
        else memcpy(f->reg+reg,t->tx+1,t->tx_size-1);
    }
    return fail?NSX_SENSOR_E_TIMEOUT:NSX_SENSOR_OK;
}
static void delay(void *user,uint32_t us)
{
    fake_t *f=user; ++f->delays; f->time+=us;
    if (f->produce && (f->reg[0x10]&15)==15 && f->time>=f->next_sample) {
        generate(f); f->next_sample=f->time+10000;
    }
}
static uint64_t now(void *user) { fake_t *f=user; return f->frozen_clock?0:f->time; }
static const nsx_sensor_bus_ops_t ops={transfer};
static void bind_fake(icm45605_context_t *c, fake_t *f, bool big)
{
    memset(f,0,sizeof(*f)); f->big_endian=big; reset(f);
    const nsx_sensor_bus_t bus={&ops,f,NSX_SENSOR_BUS_SPI,4,14};
    const nsx_sensor_runtime_t runtime={f,now,delay};
    assert(icm45605_bind(c,&bus,&runtime)==NSX_SENSOR_OK && !f->calls && !f->delays);
}
static void init_fake(icm45605_context_t *c, fake_t *f, bool big)
{
    bind_fake(c,f,big); icm45605_config_t cfg=icm45605_default_config();
    assert(icm45605_init(c,&cfg)==NSX_SENSOR_OK);
    assert(f->reg[0x10]==0 && !f->reg[0x56] && !f->reg[0x57] && !c->_running);
}
static void test_validation_and_instances(void)
{
    fake_t a,b; icm45605_context_t x,y;
    init_fake(&x,&a,false); init_fake(&y,&b,true);
    unsigned bc=b.calls, bd=b.delays;
    a.indirect[0xa583]=0xf8;
    assert(icm45605_set_accel_filter(&x,ICM45605_FILTER_ODR_DIV128)==NSX_SENSOR_OK);
    assert(a.indirect[0xa583]==0xfe && b.indirect[0xa583]==1 && b.calls==bc && b.delays==bd);
    assert(x._device.transport.user==&x && y._device.transport.user==&y);
    uint8_t id=77; a.reg[0x72]=0;
    assert(icm45605_probe(&x,&id)==NSX_SENSOR_E_DEVICE && id==77);
    a.reg[0x72]=0xe5;
    unsigned ac=a.calls;
    icm45605_config_t cfg=icm45605_default_config(); cfg.accel_range=(icm45605_accel_range_t)0;
    assert(icm45605_configure(&x,&cfg)==NSX_SENSOR_E_ARGUMENT && a.calls==ac);
    cfg=icm45605_default_config(); cfg.odr=(icm45605_odr_t)13;
    assert(icm45605_configure(&x,&cfg)==NSX_SENSOR_E_ARGUMENT && a.calls==ac);
    assert(icm45605_set_gyro_range(&x,(icm45605_gyro_range_t)-1)==NSX_SENSOR_E_ARGUMENT);
    nsx_sensor_bus_t bad=x.bus; bad.max_tx_size=3;
    assert(icm45605_bind(&x,&bad,NULL)==NSX_SENSOR_E_UNSUPPORTED && a.calls==ac);
    bad=x.bus; bad.max_rx_size=13;
    assert(icm45605_bind(&x,&bad,NULL)==NSX_SENSOR_E_UNSUPPORTED);
    bad=x.bus; bad.kind=NSX_SENSOR_BUS_I2C;
    assert(icm45605_bind(&x,&bad,NULL)==NSX_SENSOR_E_UNSUPPORTED);
    assert(icm45605_set_drdy_interrupt(&x,true)==NSX_SENSOR_OK);
    assert(a.reg[0x56]==4 && a.reg[0x57]==0 && (a.reg[0x58]&7)==1 && b.reg[0x56]==0);
    assert(icm45605_start(&x)==NSX_SENSOR_OK && a.reg[0x10]==15 && b.reg[0x10]==0);
    assert(icm45605_set_odr(&x,ICM45605_ODR_200HZ)==NSX_SENSOR_E_STATE);
    assert(icm45605_stop(&x)==NSX_SENSOR_OK && a.reg[0x10]==0);
    assert(b.calls==bc && b.delays==bd);
}
static void test_samples(void)
{
    for (unsigned big=0;big<2;++big) {
        for (unsigned ar=1;ar<=4;++ar) for (unsigned gr=1;gr<=8;++gr) {
            fake_t f; icm45605_context_t c; init_fake(&c,&f,big!=0);
            assert(icm45605_set_accel_range(&c,(icm45605_accel_range_t)ar)==NSX_SENSOR_OK);
            assert(icm45605_set_gyro_range(&c,(icm45605_gyro_range_t)gr)==NSX_SENSOR_OK);
            const int16_t values[]={-32768,32767,-1,16384,-16384,0,1280};
            memcpy(f.values,values,sizeof(values)); encode(&f);
            assert(icm45605_start(&c)==NSX_SENSOR_OK);
            icm45605_raw_sample_t raw;
            assert(icm45605_read_raw(&c,&raw)==NSX_SENSOR_OK);
            assert(raw.accel[0]==-32768 && raw.accel[1]==32767 && raw.accel[2]==-1 && raw.gyro[1]==-16384 && raw.temperature==1280);
            icm45605_sample_t sample;
            assert(icm45605_read(&c,&sample)==NSX_SENSOR_OK);
            assert(sample.accel_g[0]==-(float)(32u>>ar));
            assert(sample.gyro_dps[0]==2000.0f/(float)(1u<<gr) && sample.temperature_c==35.0f);
            icm45605_bias_t bias={{1,2,3},{4,5,6}};
            assert(icm45605_set_bias(&c,&bias)==NSX_SENSOR_OK);
            assert(icm45605_read(&c,&sample)==NSX_SENSOR_OK && sample.accel_g[0]==-(float)(32u>>ar)-1);
            icm45605_sample_t saved=sample; f.fail_at=f.calls+1; f.fail_after=true;
            assert(icm45605_read(&c,&sample)==NSX_SENSOR_E_TIMEOUT && !memcmp(&saved,&sample,sizeof(saved)));
            assert(icm45605_stop(&c)==NSX_SENSOR_OK);
            assert(icm45605_read_raw(&c,&raw)==NSX_SENSOR_E_STATE);
        }
    }
}
static void test_failures(void)
{
    fake_t f; icm45605_context_t c; init_fake(&c,&f,false); unsigned total=f.calls;
    icm45605_config_t cfg=icm45605_default_config();
    for (unsigned after=0;after<2;++after) for (unsigned at=1;at<=total;++at) {
        bind_fake(&c,&f,false); f.fail_at=at; f.fail_after=after!=0;
        assert(icm45605_init(&c,&cfg)==NSX_SENSOR_E_TIMEOUT && !c._initialized);
        assert(f.calls==at); /* No further bus writes after even an RMW read error. */
        assert(icm45605_start(&c)==NSX_SENSOR_E_STATE);
    }
    init_fake(&c,&f,false); f.fail_at=f.calls+1;
    uint8_t indirect=0xa5;
    assert(inv_imu_read_reg(&c._device,0xa583,1,&indirect)!=0 && indirect==0);
    bind_fake(&c,&f,false); f.stuck_reset=true;
    assert(icm45605_init(&c,&cfg)==NSX_SENSOR_E_DEVICE && !c._initialized && f.time<10000);
    init_fake(&c,&f,false); unsigned before=f.calls;
    assert(icm45605_configure(&c,&cfg)==NSX_SENSOR_OK); total=f.calls-before;
    for (unsigned at=1;at<=total;++at) {
        init_fake(&c,&f,false); f.fail_at=f.calls+at;
        assert(icm45605_configure(&c,&cfg)==NSX_SENSOR_E_TIMEOUT && !c._initialized);
        assert(f.calls==f.fail_at);
    }
    init_fake(&c,&f,false); before=f.calls;
    assert(icm45605_start(&c)==NSX_SENSOR_OK); total=f.calls-before;
    for (unsigned at=1;at<=total;++at) {
        init_fake(&c,&f,false); f.fail_at=f.calls+at;
        assert(icm45605_start(&c)==NSX_SENSOR_E_TIMEOUT && !c._initialized);
    }
    init_fake(&c,&f,false); bool data_ready=false;
    assert(icm45605_read_data_ready(&c,&data_ready)==NSX_SENSOR_E_STATE && !data_ready);
    assert(icm45605_set_drdy_interrupt(&c,true)==NSX_SENSOR_OK);
    generate(&f);
    assert(icm45605_read_data_ready(&c,&data_ready)==NSX_SENSOR_OK && data_ready);
    assert(icm45605_read_data_ready(&c,&data_ready)==NSX_SENSOR_OK && !data_ready);
    f.reg[0x19]=0x80; data_ready=true;
    assert(icm45605_read_data_ready(&c,&data_ready)==NSX_SENSOR_E_DEVICE && data_ready && !c._initialized);
    init_fake(&c,&f,false);
    assert(icm45605_set_drdy_interrupt(&c,true)==NSX_SENSOR_OK);
    f.fail_at=f.calls+1; f.fail_after=true;
    assert(icm45605_read_data_ready(&c,&data_ready)==NSX_SENSOR_E_TIMEOUT && !c._initialized);
}
static void test_calibration(void)
{
    fake_t f; icm45605_context_t c; init_fake(&c,&f,false);
    assert(icm45605_start(&c)==NSX_SENSOR_OK);
    assert(icm45605_calibrate(&c,1,1000)==NSX_SENSOR_E_STATE);
    assert(icm45605_set_drdy_interrupt(&c,true)==NSX_SENSOR_OK);
    f.values[0]=512; f.values[2]=8192; f.values[3]=128;
    assert(icm45605_start(&c)==NSX_SENSOR_OK);
    f.produce=true; f.next_sample=f.time+10000; f.race=true;
    uint64_t calibration_start=f.time;
    assert(icm45605_calibrate(&c,12,300000)==NSX_SENSOR_OK && f.samples==12);
    assert(f.time-calibration_start>=120000);
    assert(c._bias.accel_g[0]==0.0625f && c._bias.accel_g[2]==0 && c._bias.gyro_dps[0]==1.953125f);
    icm45605_bias_t saved=c._bias;
    assert(icm45605_calibrate(&c,12,300000)==NSX_SENSOR_OK && f.samples==24);
    assert(!memcmp(&saved,&c._bias,sizeof(saved)));
    f.produce=false; f.race=false; f.frozen_clock=true; uint64_t before=f.time;
    assert(icm45605_calibrate(&c,2,2500)==NSX_SENSOR_E_TIMEOUT && f.time-before==2500);
    assert(!memcmp(&saved,&c._bias,sizeof(saved)));
    c.runtime.now_us=NULL;
    assert(icm45605_calibrate(&c,2,2500)==NSX_SENSOR_E_TIMEOUT);
    assert(icm45605_calibrate(&c,0,10)==NSX_SENSOR_E_ARGUMENT);
    icm45605_bias_t invalid=saved; invalid.accel_g[0]=NAN;
    assert(icm45605_set_bias(&c,&invalid)==NSX_SENSOR_E_ARGUMENT);
}
int main(void)
{
    test_validation_and_instances(); test_samples(); test_failures(); test_calibration();
    puts("ICM45605: instances, indirect SPI, ranges, endian samples, failures and fresh calibration passed");
    return 0;
}
