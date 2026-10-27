/* SPDX-License-Identifier: BSD-3-Clause */
#include <nsx/sensors/max86150.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef struct {
    uint8_t reg[256], fifo[32][12];
    unsigned count, calls, fail_at, fifo_reads, fail_fifo_read;
    uint8_t last_reg;
    bool stuck_reset, fail_after, frozen_clock, overflow_during_read;
    /* Inject independent production/reset at transfer boundaries. */
    unsigned push_after_call, pushes_after_call, power_before_call;
    uint64_t time, next_sample;
    unsigned period_us, generated;
    size_t max_read;
} fake_t;
static unsigned width(fake_t *f)
{
    unsigned n=0;
    for (unsigned i=0;i<4;++i) if ((f->reg[9+i/2]>>(4*(i%2)))&15) ++n;
    return n*3;
}
static void flush(fake_t *f)
{ f->count=0; f->reg[4]=f->reg[5]=f->reg[6]=0; f->reg[0]&=0x7f; }
static void push(fake_t *f, unsigned seed);
static nsx_sensor_status_t transfer(void *user, const nsx_sensor_transfer_t *t)
{
    fake_t *f=user; ++f->calls;
    assert(t->tx && t->tx_size>=1 && t->tx_size<=3);
    if (f->calls==f->power_before_call) {
        memset(f->reg,0,sizeof(f->reg)); f->reg[255]=0x1e; flush(f); f->reg[0]=1;
    }
    uint8_t reg=t->tx[0]; f->last_reg=reg; bool fail=f->calls==f->fail_at;
    if (fail && !f->fail_after) return NSX_SENSOR_E_IO;
    if (t->rx_size) {
        assert(t->tx_size==1 && t->rx_size<=f->max_read);
        if (reg==7) {
            ++f->fifo_reads;
            unsigned w=width(f); assert(w && t->rx_size%w==0 && t->rx_size/w<=f->count);
            /* Real hardware erases OVF on each complete pop. */
            if (f->overflow_during_read && (f->reg[13]&4)) f->reg[5]=1;
            for (size_t i=0;i<t->rx_size/w;++i) {
                memcpy(t->rx+i*w,f->fifo[f->reg[6]],w);
                f->reg[6]=(f->reg[6]+1)&31; --f->count; f->reg[5]=0;
            }
            if (f->reg[8]&0x40) f->reg[0]&=0x7f;
            if (f->fifo_reads==f->fail_fifo_read) fail=true;
        } else {
            assert((size_t)reg+t->rx_size<=256);
            memcpy(t->rx,f->reg+reg,t->rx_size);
            if (reg==0) { f->reg[0]=0; if (t->rx_size>=2) f->reg[1]=0; }
            if (reg==1) f->reg[1]=0;
        }
    } else {
        assert(t->tx_size==2 && reg!=5); /* OVF is read-only. */
        uint8_t old=f->reg[reg]; f->reg[reg]=t->tx[1];
        if (reg==13) {
            if ((t->tx[1]&1) && !f->stuck_reset) {
                uint8_t power=f->reg[0]&1; /* Soft reset does not generate/clear PWR_RDY. */
                memset(f->reg,0,sizeof(f->reg)); f->reg[255]=0x1e; flush(f); f->reg[0]=power;
            }
            if (!(old&4) && (t->tx[1]&4)) flush(f);
            if (t->tx[1]&2) f->reg[0]=f->reg[1]=0;
        }
        if ((f->reg[13]&4) && (reg==9 || reg==10 || reg==14 || reg==15 || reg==60 || reg==62)) flush(f);
    }
    if (f->calls==f->push_after_call)
        for (unsigned i=0;i<f->pushes_after_call;++i) push(f,100+i);
    return fail?NSX_SENSOR_E_IO:NSX_SENSOR_OK;
}
/* Optional scenario clock: 400 complete frames/s, no averaging. This is an
 * externally selected schedule, not an implementation of ADC rate adjustment.
 * Rev2 FIFO/System Control register descriptions: FIFO production requires FIFO_EN and no shutdown. */
static void advance(fake_t *f, uint32_t us)
{
    uint64_t end = f->time + us;
    if (f->period_us) while (f->next_sample <= end) {
        push(f, f->generated++);
        f->next_sample += f->period_us;
    }
    f->time = end;
}
static void delay(void *user,uint32_t us) { advance(user, us); }
static uint64_t now(void *user) { fake_t *f=user; return f->frozen_clock?0:f->time; }
static const nsx_sensor_bus_ops_t ops={transfer};
static void bind_fake(max86150_context_t *c,fake_t *f,size_t max_read)
{
    memset(f,0,sizeof(*f)); f->reg[255]=0x1e; f->max_read=max_read;
    nsx_sensor_bus_t bus={&ops,f,NSX_SENSOR_BUS_I2C,3,max_read};
    nsx_sensor_runtime_t runtime={f,now,delay};
    assert(max86150_bind(c,&bus,&runtime)==NSX_SENSOR_OK && f->calls==0);
}
static void init_fake(max86150_context_t *c,fake_t *f,size_t max_read)
{
    bind_fake(c,f,max_read); max86150_config_t config=max86150_default_config();
    assert(max86150_init(c,&config)==NSX_SENSOR_OK);
    assert(f->reg[13]==2 && f->reg[2]==0 && f->reg[3]==0);
}
static void put24(uint8_t *b,uint32_t v) { b[0]=(uint8_t)(v>>16); b[1]=(uint8_t)(v>>8); b[2]=(uint8_t)v; }
static void push(fake_t *f, unsigned seed)
{
    if (!(f->reg[13]&4) || (f->reg[13]&2)) return;
    if (f->count==32) {
        if (f->reg[5]<31) ++f->reg[5];
        if (!(f->reg[8]&16)) return;
        f->reg[6]=(f->reg[6]+1)&31; --f->count;
    }
    unsigned n=width(f)/3;
    for (unsigned j=0;j<n;++j) {
        unsigned slot=(f->reg[9+j/2]>>(4*(j%2)))&15;
        uint32_t v=slot==9 ? (seed&1 ? 0x20000 : 0x3ffff) : 0xf80000u+seed*10+j;
        put24(f->fifo[f->reg[4]]+j*3,v);
    }
    f->reg[4]=(f->reg[4]+1)&31; ++f->count;
    if (f->count>=32u-(f->reg[8]&15)) f->reg[0]|=0x80;
}
static void test_lifecycle_and_errors(void)
{
    fake_t f; max86150_context_t c; bind_fake(&c,&f,12);
    uint8_t id=77; assert(max86150_probe(&c,&id)==NSX_SENSOR_OK && id==0x1e);
    f.reg[255]=0; assert(max86150_probe(&c,&id)==NSX_SENSOR_E_DEVICE && id==0x1e);
    f.reg[255]=0x1e; f.fail_at=f.calls+1; f.fail_after=true;
    assert(max86150_probe(&c,&id)==NSX_SENSOR_E_IO && id==0x1e);
    init_fake(&c,&f,12); unsigned total=f.calls;
    for (unsigned after=0;after<2;++after) for (unsigned i=1;i<=total;++i) {
        bind_fake(&c,&f,12); f.fail_at=i; f.fail_after=after!=0; max86150_config_t cfg=max86150_default_config();
        assert(max86150_init(&c,&cfg)==NSX_SENSOR_E_IO && !c._configured);
        f.fail_at=0;
        assert(max86150_init(&c,&cfg)==NSX_SENSOR_OK);
    }
    init_fake(&c,&f,12); f.stuck_reset=true; f.frozen_clock=true;
    assert(max86150_reset(&c,2500)==NSX_SENSOR_E_TIMEOUT && f.time==2500);
    assert(max86150_start(&c)==NSX_SENSOR_E_STATE);
    init_fake(&c,&f,12); assert(max86150_start(&c)==NSX_SENSOR_OK && f.reg[13]==4);
    assert(max86150_set_ppg_adc_range(&c,MAX86150_PPG_ADC_RANGE_8192_NA)==NSX_SENSOR_E_STATE);
    assert(max86150_shutdown(&c)==NSX_SENSOR_OK && f.reg[13]==2 && f.time==100000);
    assert(max86150_disable_slots(&c)==NSX_SENSOR_OK);
    assert(max86150_start(&c)==NSX_SENSOR_E_STATE);
    max86150_config_t cfg=max86150_default_config(); cfg.slots[0]=MAX86150_SLOT_ECG;
    assert(max86150_validate_config(&cfg)==NSX_SENSOR_E_ARGUMENT);
    cfg=max86150_default_config(); cfg.led_range[0]=(max86150_led_range_t)2;
    assert(max86150_validate_config(&cfg)==NSX_SENSOR_E_ARGUMENT);
    cfg=max86150_default_config(); cfg.slots[0]=MAX86150_SLOT_OFF;
    assert(max86150_validate_config(&cfg)==NSX_SENSOR_E_ARGUMENT);
    init_fake(&c,&f,12); unsigned calls=f.calls;
    assert(max86150_set_led_current_range(&c,2,0)==NSX_SENSOR_E_ARGUMENT);
    assert(max86150_set_led_pulse_amplitude(&c,3,0)==NSX_SENSOR_E_ARGUMENT && f.calls==calls);
    f.reg[20]=0xf0;
    assert(max86150_set_led_current_range(&c,1,1)==NSX_SENSOR_OK && f.reg[20]==0xf4);
    assert(max86150_set_led_pulse_amplitude(&c,0,255)==NSX_SENSOR_OK && f.reg[17]==255);
    cfg=c._config; cfg.ecg_pga_gain=MAX86150_ECG_PGA_GAIN_8;
    f.fail_at=f.calls+3; /* After one applied config write, a later RMW read fails. */
    assert(max86150_configure(&c,&cfg)==NSX_SENSOR_E_IO && !c._configured);
}
static void test_fifo(void)
{
    fake_t f; max86150_context_t c; init_fake(&c,&f,96);
    max86150_frame_t frames[32]; size_t done=99;
    assert(max86150_read_fifo_snapshot(&c,frames,32,&done)==NSX_SENSOR_AGAIN && done==0);
    assert(max86150_start(&c)==NSX_SENSOR_OK);
    for (unsigned i=0;i<32;++i) push(&f,i);
    assert(f.reg[4]==f.reg[6] && f.count==32 && !f.reg[5]);
    /* A public consuming interrupt read must not erase full/empty evidence. */
    uint8_t status;
    assert(max86150_get_int1(&c,&status)==NSX_SENSOR_OK && (status&0x80));
    assert(max86150_read_fifo_snapshot(&c,frames,2,&done)==NSX_SENSOR_OK && done==2 && f.count==30);
    assert(!(f.reg[13]&4));
    assert(frames[0].value[0]==0 && frames[0].value[1]==1 && frames[0].value[2]==-1);
    assert(frames[1].value[2]==-131072 && frames[1].count==3 && frames[1].slots[2]==MAX86150_SLOT_ECG);
    unsigned count=f.count; push(&f,99); assert(f.count==count);
    assert(max86150_read_fifo_snapshot(&c,frames,32,&done)==NSX_SENSOR_OK && done==30);
    assert(max86150_read_fifo_snapshot(&c,frames,32,&done)==NSX_SENSOR_AGAIN && done==0);
    /* Equal nonzero pointers are empty as well. */
    assert(max86150_start(&c)==NSX_SENSOR_OK);
    for (unsigned i=0;i<5;++i) push(&f,i);
    assert(max86150_read_fifo(&c,frames,32,&done)==NSX_SENSOR_OK && done==4);
    assert(f.reg[4]==5 && f.reg[6]==4 && c._fifo_enabled);
    assert(max86150_read_fifo(&c,frames,32,&done)==NSX_SENSOR_AGAIN);
    for (unsigned i=0;i<31;++i) push(&f,i);
    assert(max86150_read_fifo_snapshot(&c,frames,32,&done)==NSX_SENSOR_OK && done==32);
    assert(max86150_start(&c)==NSX_SENSOR_OK);
    for (unsigned i=0;i<33;++i) push(&f,i);
    unsigned reads=f.fifo_reads;
    assert(max86150_read_fifo_snapshot(&c,frames,32,&done)==NSX_SENSOR_E_OVERFLOW && done==0 && f.fifo_reads==reads);
    assert(max86150_read_fifo_snapshot(&c,frames,32,&done)==NSX_SENSOR_E_STATE);
    assert(max86150_clear_fifo(&c)==NSX_SENSOR_OK && !f.count && f.reg[13]==2);
    assert(max86150_start(&c)==NSX_SENSOR_OK);
    for (unsigned i=0;i<22;++i) push(&f,i);
    memset(frames,0x5a,sizeof(frames)); max86150_frame_t saved=frames[10];
    f.fail_fifo_read=f.fifo_reads+2;
    assert(max86150_read_fifo_snapshot(&c,frames,32,&done)==NSX_SENSOR_E_IO && done==10);
    assert(!memcmp(&saved,&frames[10],sizeof(saved)) && !c._fifo_valid);
}
static void test_slots_and_limits(void)
{
    /* All contiguous permitted source permutations, 1..4 slots. */
    const max86150_slot_t choices[]={MAX86150_SLOT_OFF,MAX86150_SLOT_PPG_LED1,MAX86150_SLOT_PPG_LED2,
        MAX86150_SLOT_PILOT_LED1,MAX86150_SLOT_PILOT_LED2,MAX86150_SLOT_ECG};
    unsigned tested=0;
    for (unsigned combo=0;combo<1296;++combo) {
        max86150_config_t cfg=max86150_default_config(); unsigned v=combo;
        for (unsigned i=0;i<4;++i) { cfg.slots[i]=choices[v%6]; v/=6; }
        if (max86150_validate_config(&cfg)!=NSX_SENSOR_OK || cfg.slots[0]==MAX86150_SLOT_OFF) continue;
        fake_t f; max86150_context_t c; bind_fake(&c,&f,12);
        assert(max86150_init(&c,&cfg)==NSX_SENSOR_OK && max86150_start(&c)==NSX_SENSOR_OK);
        for (unsigned i=0;i<3;++i) push(&f,i);
        max86150_frame_t frames[4]; size_t done;
        assert(max86150_read_fifo_snapshot(&c,frames,2,&done)==NSX_SENSOR_OK && done==2 && f.count==1);
        for (unsigned j=0;j<frames[0].count;++j) {
            assert(frames[0].slots[j]==cfg.slots[j]);
            assert(frames[0].value[j]==(cfg.slots[j]==MAX86150_SLOT_ECG?-1:(int)j));
        }
        assert(max86150_read_fifo_snapshot(&c,frames,4,&done)==NSX_SENSOR_OK && done==1);
        ++tested;
    }
    assert(tested>40);
}
static void test_status_and_instances(void)
{
    fake_t a,b; max86150_context_t x,y; init_fake(&x,&a,12); init_fake(&y,&b,96);
    assert(max86150_set_ecg_pga_gain(&x,MAX86150_ECG_PGA_GAIN_8)==NSX_SENSOR_OK && a.reg[62]==14 && b.reg[62]==2);
    assert(max86150_start(&x)==NSX_SENSOR_OK && max86150_start(&y)==NSX_SENSOR_OK);
    push(&a,1); push(&b,2); max86150_frame_t frame; size_t done;
    assert(max86150_read_fifo_snapshot(&x,&frame,1,&done)==NSX_SENSOR_OK && frame.value[2]==-131072);
    assert(max86150_read_fifo_snapshot(&y,&frame,1,&done)==NSX_SENSOR_OK && frame.value[2]==-1);
    a.reg[0]=0x60; a.reg[1]=0x84;
    max86150_interrupts_t status;
    assert(max86150_get_interrupts(&x,&status)==NSX_SENSOR_OK && status.status1==0x60 && status.status2==0x84);
    assert(max86150_get_interrupts(&x,&status)==NSX_SENSOR_OK && !status.status1 && !status.status2);
    a.reg[0]=0x80; a.fail_at=a.calls+1; a.fail_after=true;
    status.status1=99;
    assert(max86150_get_interrupts(&x,&status)==NSX_SENSOR_E_IO && status.status1==99 && !x._fifo_valid);
    assert(max86150_start(&x)==NSX_SENSOR_E_STATE);
    max86150_config_t config=max86150_default_config();
    assert(max86150_init(&x,&config)==NSX_SENSOR_OK);
    assert(max86150_start(&x)==NSX_SENSOR_OK);
    a.reg[0]=1;
    assert(max86150_get_interrupts(&x,&status)==NSX_SENSOR_E_DEVICE && !x._configured);
    assert(max86150_set_interrupts(&y,0x10,0)==NSX_SENSOR_E_ARGUMENT);
    assert(max86150_shutdown(&y)==NSX_SENSOR_OK);
    assert(max86150_set_almost_full_rollover(&y,1)==NSX_SENSOR_OK);
    assert(max86150_start(&y)==NSX_SENSOR_OK); push(&b,1);
    assert(max86150_read_fifo(&y,&frame,1,&done)==NSX_SENSOR_E_UNSUPPORTED);
    assert(max86150_read_fifo_snapshot(&y,&frame,1,&done)==NSX_SENSOR_OK);
}
static void test_continuous_guards_and_failures(void)
{
    fake_t f; max86150_context_t c; init_fake(&c,&f,96);
    max86150_frame_t frames[32]; size_t done;
    assert(max86150_read_fifo(&c,frames,32,&done)==NSX_SENSOR_E_STATE);
    assert(max86150_set_almost_full_flag_options(&c,1)==NSX_SENSOR_OK);
    assert(max86150_start(&c)==NSX_SENSOR_OK); push(&f,0);
    assert(max86150_read_fifo(&c,frames,32,&done)==NSX_SENSOR_E_UNSUPPORTED);
    assert(max86150_read_fifo_snapshot(&c,frames,32,&done)==NSX_SENSOR_OK && done==1);
    assert(max86150_shutdown(&c)==NSX_SENSOR_OK);
    assert(max86150_set_almost_full_flag_options(&c,0)==NSX_SENSOR_OK);
    assert(max86150_set_almost_full_int_options(&c,1)==NSX_SENSOR_OK);
    assert(max86150_start(&c)==NSX_SENSOR_OK); push(&f,0);
    assert(max86150_read_fifo(&c,frames,32,&done)==NSX_SENSOR_E_UNSUPPORTED);
    assert(max86150_read_fifo_snapshot(&c,frames,32,&done)==NSX_SENSOR_OK && done==1);
    init_fake(&c,&f,96);
    assert(max86150_start(&c)==NSX_SENSOR_OK);
    for (unsigned i=0;i<33;++i) push(&f,i);
    assert(max86150_read_fifo(&c,frames,32,&done)==NSX_SENSOR_E_OVERFLOW && !done);
    assert(max86150_start(&c)==NSX_SENSOR_OK);
    for (unsigned i=0;i<32;++i) push(&f,i);
    /* Documented silicon limitation: a late lost sample can be erased from
     * OVF by the following pop. Continuous mode does not claim gaplessness. */
    f.overflow_during_read=true;
    assert(max86150_read_fifo(&c,frames,1,&done)==NSX_SENSOR_OK && done==1 && !f.reg[5]);
    assert(max86150_start(&c)==NSX_SENSOR_OK);
    for (unsigned i=0;i<22;++i) push(&f,i);
    f.fail_fifo_read=f.fifo_reads+2;
    memset(frames,0x5a,sizeof(frames)); max86150_frame_t saved=frames[10];
    assert(max86150_read_fifo(&c,frames,32,&done)==NSX_SENSOR_E_IO && done==10);
    assert(!memcmp(&frames[10],&saved,sizeof(saved)) && !c._fifo_valid);
    init_fake(&c,&f,12); unsigned calls=f.calls;
    nsx_sensor_bus_t bad=c.bus; bad.max_rx_size=11;
    assert(max86150_bind(&c,&bad,NULL)==NSX_SENSOR_E_UNSUPPORTED && f.calls==calls);
    bad=c.bus; bad.kind=NSX_SENSOR_BUS_SPI;
    assert(max86150_bind(&c,&bad,NULL)==NSX_SENSOR_E_UNSUPPORTED && f.calls==calls);
    assert(max86150_set_ppg_sample_rate(&c,(max86150_ppg_sample_rate_t)16)==NSX_SENSOR_E_ARGUMENT && f.calls==calls);
    uint8_t a=0xa5,b=0x5a; f.fail_at=f.calls+1; f.fail_after=true;
    assert(max86150_get_ppg_config(&c,&a,&b)==NSX_SENSOR_E_IO && a==0xa5 && b==0x5a);
}
/* Status/pointer reads and bursts are distinct bus transactions. Production
 * runs independently, including after a status read has consumed A_FULL. */
static void test_continuous_interleavings(void)
{
    for (unsigned offset=1;offset<=8;++offset) {
        for (unsigned initial=0;initial<=32;++initial) {
            for (unsigned arrivals=1;arrivals<=32;arrivals+=31) {
                fake_t f; max86150_context_t c; init_fake(&c,&f,96);
                assert(max86150_start(&c)==NSX_SENSOR_OK);
                for (unsigned i=0;i<initial;++i) push(&f,i);
                f.push_after_call=f.calls+offset; f.pushes_after_call=arrivals;
                max86150_frame_t frames[32]; size_t done;
                nsx_sensor_status_t s=max86150_read_fifo(&c,frames,32,&done);
                assert(s==NSX_SENSOR_OK || s==NSX_SENSOR_AGAIN || s==NSX_SENSOR_E_OVERFLOW);
                if (s==NSX_SENSOR_E_OVERFLOW) continue;
                /* Stop injecting; capture exactly what remains, including the
                 * retained frame. The fake rejects every out-of-bounds pop. */
                f.push_after_call=0;
                unsigned queued=f.count;
                s=max86150_read_fifo_snapshot(&c,frames,32,&done);
                if (s==NSX_SENSOR_E_OVERFLOW) continue;
                assert(done==queued && f.count==0);
                assert(s==(queued?NSX_SENSOR_OK:NSX_SENSOR_AGAIN));
                assert(max86150_read_fifo_snapshot(&c,frames,32,&done)==NSX_SENSOR_AGAIN && !done);
            }
        }
    }
    /* Exactly the reported 31->32 race, then continuous retries and a refill. */
    fake_t f; max86150_context_t c; init_fake(&c,&f,96);
    assert(max86150_start(&c)==NSX_SENSOR_OK);
    for (unsigned i=0;i<31;++i) push(&f,i);
    f.push_after_call=f.calls+1; f.pushes_after_call=1;
    max86150_frame_t frames[32]; size_t done;
    assert(max86150_read_fifo(&c,frames,32,&done)==NSX_SENSOR_OK && done==31 && f.count==1);
    assert(max86150_read_fifo(&c,frames,32,&done)==NSX_SENSOR_AGAIN && !done && f.count==1);
    for (unsigned i=0;i<31;++i) push(&f,i);
    assert(max86150_read_fifo(&c,frames,32,&done)==NSX_SENSOR_OK && done==31 && f.count==1);
    assert(max86150_read_fifo_snapshot(&c,frames,32,&done)==NSX_SENSOR_OK && done==1 && !f.count);
}
static void test_full_fifo_read_failure(void)
{
    for (unsigned offset=3;offset<=6;offset+=3) {
        fake_t f; max86150_context_t c; init_fake(&c,&f,96);
        assert(max86150_start(&c)==NSX_SENSOR_OK);
        for (unsigned i=0;i<32;++i) push(&f,i);
        f.fail_at=f.calls+offset; /* OVF read before first or second burst. */
        max86150_frame_t frames[32]; size_t done;
        assert(max86150_read_fifo(&c,frames,32,&done)==NSX_SENSOR_E_IO);
        assert(done==(offset==3?0u:10u) && !c._fifo_valid);
        unsigned queued=f.count;
        assert(max86150_read_fifo_snapshot(&c,frames,32,&done)==NSX_SENSOR_E_STATE && !done && f.count==queued);
        assert(max86150_start(&c)==NSX_SENSOR_OK && !f.count);
    }
}
/* A failed consuming status read cannot prove whether PWR_RDY was lost.
 * Cover direct/convenience reads and both status reads inside clear_fifo. */
static void test_ambiguous_status_recovery(void)
{
    fake_t f; max86150_context_t c;
    init_fake(&c,&f,96);
    assert(max86150_start(&c)==NSX_SENSOR_OK);
    unsigned before=f.calls;
    assert(max86150_clear_fifo(&c)==NSX_SENSOR_OK);
    unsigned clear_calls=f.calls-before;
    for (unsigned path=0;path<5;++path) {
        for (unsigned reset=0;reset<2;++reset) {
            for (unsigned after=0;after<2;++after) {
                init_fake(&c,&f,96);
                assert(max86150_start(&c)==NSX_SENSOR_OK);
                unsigned offset=path==4?clear_calls:(path==3?2:1);
                f.fail_at=f.calls+offset; f.fail_after=after!=0;
                if (reset) f.power_before_call=f.fail_at;
                max86150_interrupts_t irq={0xa5,0x5a};
                uint8_t status=0xa5;
                nsx_sensor_status_t result;
                if (path==0) result=max86150_get_interrupts(&c,&irq);
                else if (path==1) result=max86150_get_int1(&c,&status);
                else if (path==2) result=max86150_get_int2(&c,&status);
                else result=max86150_clear_fifo(&c);
                assert(result==NSX_SENSOR_E_IO && f.last_reg==0);
                assert(irq.status1==0xa5 && irq.status2==0x5a && status==0xa5);
                assert(!c._configured && !c._fifo_valid);
                if (reset && after) assert(f.reg[0]==0 && f.reg[9]==0);
                unsigned calls=f.calls;
                assert(max86150_start(&c)==NSX_SENSOR_E_STATE);
                assert(max86150_clear_fifo(&c)==NSX_SENSOR_E_STATE);
                assert(f.calls==calls); /* Never reuse the stale requested settings. */
                max86150_config_t config=max86150_default_config();
                assert(max86150_init(&c,&config)==NSX_SENSOR_OK);
                assert(f.reg[9]!=0 && max86150_start(&c)==NSX_SENSOR_OK);
                push(&f,2);
                max86150_frame_t frame; size_t done=99;
                assert(max86150_read_fifo_snapshot(&c,&frame,1,&done)==NSX_SENSOR_OK);
                assert(done==1 && frame.count==3 && frame.slots[2]==MAX86150_SLOT_ECG);
            }
        }
    }
}
static void test_power_reset_detection(void)
{
    fake_t f; max86150_context_t c;
    bind_fake(&c,&f,96); f.reg[0]=1;
    max86150_config_t config=max86150_default_config();
    assert(max86150_init(&c,&config)==NSX_SENSOR_OK); /* Expected power-on flag. */
    unsigned init_calls=f.calls;
    bind_fake(&c,&f,96); f.power_before_call=init_calls;
    assert(max86150_init(&c,&config)==NSX_SENSOR_E_DEVICE && !c._configured);
    init_fake(&c,&f,96);
    unsigned before=f.calls;
    assert(max86150_clear_fifo(&c)==NSX_SENSOR_OK);
    unsigned clear_calls=f.calls-before;
    f.power_before_call=f.calls+clear_calls;
    assert(max86150_clear_fifo(&c)==NSX_SENSOR_E_DEVICE && !c._configured && !c._fifo_valid);
    assert(max86150_start(&c)==NSX_SENSOR_E_STATE);
    assert(max86150_init(&c,&config)==NSX_SENSOR_OK && c._configured);
}
/* Public selections must preserve device encodings and reject invalid enum
 * casts before I/O or bit shifts. Expected register bytes come from Rev 2. */
static void test_typed_settings(void)
{
    fake_t f; max86150_context_t c; init_fake(&c,&f,96);
    assert(f.reg[14]==0x19 && f.reg[15]==0 && f.reg[60]==2 && f.reg[62]==2);
    max86150_config_t config=max86150_default_config();
    config.ppg_adc_range=MAX86150_PPG_ADC_RANGE_32768_NA;
    config.ppg_sample_rate=MAX86150_PPG_SAMPLE_RATE_84_HZ_2_PULSES;
    config.ppg_pulse_width=MAX86150_PPG_PULSE_WIDTH_400_US;
    config.ppg_sample_average=MAX86150_PPG_SAMPLE_AVERAGE_32;
    config.led_range[1]=MAX86150_LED_RANGE_100_MA;
    config.ecg_sample_rate=MAX86150_ECG_SAMPLE_RATE_800_HZ_CLK1;
    config.ecg_pga_gain=MAX86150_ECG_PGA_GAIN_4;
    config.ecg_ia_gain=MAX86150_ECG_IA_GAIN_9_5;
    assert(max86150_configure(&c,&config)==NSX_SENSOR_OK);
    assert(f.reg[14]==0xfb && f.reg[15]==5 && f.reg[20]==4);
    assert(f.reg[60]==6 && f.reg[62]==9);
    /* Enum conversion can narrow 256 to valid zero under -fshort-enums.
     * Only values still outside the field range reach validation as invalid. */
#define CHECK_SELECTION(field,maximum) \
    { const int invalid_values[]={-1,(maximum)+1,256}; \
    for (unsigned i=0;i<3;++i) { \
        int invalid=invalid_values[i]; \
        max86150_config_t bad=config; \
        bad.field=(max86150_##field##_t)invalid; \
        if ((unsigned)bad.field<=(maximum)) continue; \
        unsigned calls=f.calls; \
        assert(max86150_configure(&c,&bad)==NSX_SENSOR_E_ARGUMENT); \
        assert(max86150_set_##field(&c,(max86150_##field##_t)invalid)==NSX_SENSOR_E_ARGUMENT); \
        assert(f.calls==calls); \
    } }
    CHECK_SELECTION(ppg_adc_range,3)
    CHECK_SELECTION(ppg_sample_rate,15)
    CHECK_SELECTION(ppg_pulse_width,3)
    CHECK_SELECTION(ppg_sample_average,7)
    CHECK_SELECTION(ecg_sample_rate,7)
    CHECK_SELECTION(ecg_pga_gain,3)
    CHECK_SELECTION(ecg_ia_gain,3)
#undef CHECK_SELECTION
    unsigned calls=f.calls;
    assert(max86150_set_led_current_range(&c,0,(max86150_led_range_t)-1)==NSX_SENSOR_E_ARGUMENT);
    max86150_led_range_t large_range=(max86150_led_range_t)256;
    if ((unsigned)large_range>1)
        assert(max86150_set_led_current_range(&c,0,large_range)==NSX_SENSOR_E_ARGUMENT);
    assert(f.calls==calls);
    assert(max86150_set_ppg_sample_rate(&c,MAX86150_PPG_SAMPLE_RATE_100_HZ)==NSX_SENSOR_OK);
    assert(f.reg[14]==0xd3); /* Preserve ADC range and pulse width. */
    assert(max86150_set_ecg_ia_gain(&c,MAX86150_ECG_IA_GAIN_50)==NSX_SENSOR_OK && f.reg[62]==11);
}
/* Device-only controls prevent the test model silently following driver
 * behavior. Rev2 FIFO pointers/overflow sections, pp22-25,44. */
static void test_clocked_fifo(void)
{
    fake_t f = {0};
    f.reg[9] = 1; f.reg[13] = 4; /* One PPG slot, FIFO enabled. */
    f.period_us = 2500; f.next_sample = 2500;
    advance(&f, 2499); assert(f.count == 0);
    advance(&f, 1); assert(f.count == 1 && f.reg[4] == 1);
    advance(&f, 31 * 2500); assert(f.count == 32 && f.reg[4] == f.reg[6]);
    advance(&f, 2500); assert(f.count == 32 && f.reg[5] == 1 && f.reg[6] == 0);
    f.reg[8] |= 16; /* Rollover replaces oldest frame. */
    advance(&f, 2500); assert(f.count == 32 && f.reg[6] == 1 && f.reg[4] == 1);
    f.reg[13] |= 2; advance(&f, 2500); assert(f.reg[5] == 2);

    max86150_context_t c; init_fake(&c, &f, 96);
    assert(max86150_start(&c) == NSX_SENSOR_OK);
    f.period_us = 2500; f.next_sample = f.time + 2500;
    max86150_frame_t frames[32]; size_t done;
    advance(&f, 2499);
    assert(max86150_read_fifo(&c, frames, 32, &done) == NSX_SENSOR_AGAIN && done == 0);
    advance(&f, 7501); /* Four frames exist, regardless of polling frequency. */
    assert(max86150_read_fifo(&c, frames, 32, &done) == NSX_SENSOR_OK && done == 3);
    assert(frames[0].value[0] == 0 && frames[2].value[0] == 20);
    advance(&f, 33 * 2500); /* Task stops servicing the FIFO. */
    assert(max86150_read_fifo(&c, frames, 32, &done) == NSX_SENSOR_E_OVERFLOW && done == 0);
    f.period_us = 0;
    assert(max86150_clear_fifo(&c) == NSX_SENSOR_OK);
    assert(max86150_start(&c) == NSX_SENSOR_OK);
    f.period_us = 2500; f.next_sample = f.time + 2500;
    advance(&f, 5000);
    assert(max86150_read_fifo(&c, frames, 32, &done) == NSX_SENSOR_OK && done == 1);
}
int main(void)
{
    test_clocked_fifo();
    test_typed_settings();
    test_lifecycle_and_errors(); test_fifo(); test_slots_and_limits(); test_status_and_instances();
    test_continuous_guards_and_failures(); test_continuous_interleavings();
    test_full_fifo_read_failure(); test_power_reset_detection(); test_ambiguous_status_recovery();
    puts("MAX86150: lifecycle, signed data, FIFO fullness/capacity/recovery, slot layouts and failures passed");
    return 0;
}
