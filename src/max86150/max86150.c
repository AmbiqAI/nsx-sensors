/* SPDX-License-Identifier: BSD-3-Clause
 * Original MAX86150 driver: Adam Page, Copyright (c) 2022. */
#include <nsx/sensors/max86150.h>
#include <string.h>
enum { INT1=0, INT2=1, EN1=2, EN2=3, WR=4, OVF=5, RD=6, DATA=7,
       FIFO_CFG=8, SLOT1=9, SLOT2=10, SYS=13, PPG1=14, PPG2=15,
       PROX=16, LED1=17, LED2=18, RANGE=20, PILOT=21, ECG1=60, ECG3=62, ID=255 };
static nsx_sensor_status_t ready(const max86150_context_t *c)
{
    if (!c) return NSX_SENSOR_E_ARGUMENT;
    return c->_bound && c->_configured ? NSX_SENSOR_OK : NSX_SENSOR_E_STATE;
}
static void invalidate_fifo(max86150_context_t *c)
{ c->_fifo_valid=false; c->_snapshot_ready=false; c->_afull_seen=false; c->_remaining=0; }
static nsx_sensor_status_t read_bytes(max86150_context_t *c, uint8_t reg, uint8_t *out, size_t n)
{
    const nsx_sensor_transfer_t t={&reg,1,out,n};
    return nsx_sensor_bus_transfer(&c->bus,&t);
}
static nsx_sensor_status_t write_reg(max86150_context_t *c, uint8_t reg, uint8_t v)
{
    const uint8_t bytes[]={reg,v};
    const nsx_sensor_transfer_t t={bytes,2,NULL,0};
    nsx_sensor_status_t s=nsx_sensor_bus_transfer(&c->bus,&t);
    if (s!=NSX_SENSOR_OK) { c->_configured=false; invalidate_fifo(c); }
    return s;
}
static nsx_sensor_status_t update(max86150_context_t *c, uint8_t reg, uint8_t mask, uint8_t v)
{
    uint8_t old;
    nsx_sensor_status_t s=read_bytes(c,reg,&old,1);
    if (s!=NSX_SENSOR_OK) return s;
    return write_reg(c,reg,(uint8_t)((old & (uint8_t)~mask) | v));
}
static unsigned slot_count(const max86150_config_t *c)
{
    unsigned n=0;
    while (n<4 && c->slots[n]!=Max86150SlotOff) ++n;
    return n;
}
max86150_config_t max86150_default_config(void)
{
    max86150_config_t c={0};
    c.slots[0]=Max86150SlotPpgLed1; c.slots[1]=Max86150SlotPpgLed2; c.slots[2]=Max86150SlotEcg;
    c.ppg_sample_rate=6; c.ppg_pulse_width=1; c.ecg_sample_rate=2; c.ecg_ia_gain=2;
    c.almost_full_threshold=15;
    return c;
}
nsx_sensor_status_t max86150_validate_config(const max86150_config_t *c)
{
    if (!c || c->ppg_adc_range>3 || c->ppg_sample_rate>15 || c->ppg_pulse_width>3 ||
        c->ppg_sample_average>7 || c->led_range[0]>1 || c->led_range[1]>1 ||
        c->ecg_sample_rate>7 || c->ecg_pga_gain>3 || c->ecg_ia_gain>3 || c->almost_full_threshold>15)
        return NSX_SENSOR_E_ARGUMENT;
    bool off=false, ecg=false; unsigned seen=0;
    for (unsigned i=0;i<4;++i) {
        unsigned v=(unsigned)c->slots[i];
        if (!v) { off=true; continue; }
        if (off || ecg || (v!=1 && v!=2 && v!=5 && v!=6 && v!=9)) return NSX_SENSOR_E_ARGUMENT;
        /* One occurrence per data source, PPG before the single ECG element. */
        if (seen & (1u<<v)) return NSX_SENSOR_E_ARGUMENT;
        seen |= 1u<<v; ecg=v==9;
    }
    return NSX_SENSOR_OK;
}
nsx_sensor_status_t max86150_bind(max86150_context_t *c, const nsx_sensor_bus_t *bus,
                                 const nsx_sensor_runtime_t *runtime)
{
    if (!c) return NSX_SENSOR_E_ARGUMENT;
    nsx_sensor_status_t s=nsx_sensor_bus_validate(bus);
    if (s!=NSX_SENSOR_OK) return s;
    if (bus->kind!=NSX_SENSOR_BUS_I2C || bus->max_tx_size<3 || bus->max_rx_size<12) return NSX_SENSOR_E_UNSUPPORTED;
    max86150_context_t next={0}; next.bus=*bus; if (runtime) next.runtime=*runtime;
    next._bound=true; *c=next; return NSX_SENSOR_OK;
}
nsx_sensor_status_t max86150_probe(max86150_context_t *c, uint8_t *id)
{
    if (!c || !id) return NSX_SENSOR_E_ARGUMENT;
    if (!c->_bound) return NSX_SENSOR_E_STATE;
    uint8_t v; nsx_sensor_status_t s=read_bytes(c,ID,&v,1);
    if (s!=NSX_SENSOR_OK) return s;
    if (v!=0x1e) return NSX_SENSOR_E_DEVICE;
    *id=v; return NSX_SENSOR_OK;
}
nsx_sensor_status_t max86150_reset(max86150_context_t *c, uint32_t timeout)
{
    if (!c || !timeout) return NSX_SENSOR_E_ARGUMENT;
    if (!c->_bound) return NSX_SENSOR_E_STATE;
    if (!c->runtime.delay_us) return NSX_SENSOR_E_UNSUPPORTED;
    uint64_t start=c->runtime.now_us ? c->runtime.now_us(c->runtime.user) : 0;
    c->_configured=false; invalidate_fifo(c);
    nsx_sensor_status_t s=write_reg(c,SYS,1);
    if (s!=NSX_SENSOR_OK) return s;
    uint32_t remaining=timeout;
    for (;;) {
        uint8_t v; s=read_bytes(c,SYS,&v,1);
        if (s!=NSX_SENSOR_OK) return s;
        if (c->runtime.now_us && c->runtime.now_us(c->runtime.user)-start>=timeout) return NSX_SENSOR_E_TIMEOUT;
        if (!(v&1)) return NSX_SENSOR_OK;
        if (!remaining) return NSX_SENSOR_E_TIMEOUT;
        uint32_t wait=remaining<1000 ? remaining : 1000;
        c->runtime.delay_us(c->runtime.user,wait); remaining-=wait;
    }
}
static nsx_sensor_status_t apply_config(max86150_context_t *c, const max86150_config_t *v)
{
    const uint8_t regs[][3]={
        {FIFO_CFG,0x7f,(uint8_t)((v->almost_full_clear_on_data<<6)|(v->almost_full_once<<5)|(v->fifo_rollover<<4)|v->almost_full_threshold)},
        {SLOT1,0xff,(uint8_t)(v->slots[0]|(v->slots[1]<<4))},
        {SLOT2,0xff,(uint8_t)(v->slots[2]|(v->slots[3]<<4))},
        {PPG1,0xff,(uint8_t)((v->ppg_adc_range<<6)|(v->ppg_sample_rate<<2)|v->ppg_pulse_width)},
        {PPG2,7,v->ppg_sample_average}, {PROX,0xff,v->proximity_threshold},
        {LED1,0xff,v->led_amplitude[0]}, {LED2,0xff,v->led_amplitude[1]},
        {RANGE,15,(uint8_t)(v->led_range[0]|(v->led_range[1]<<2))}, {PILOT,0xff,v->pilot_amplitude},
        {ECG1,7,v->ecg_sample_rate}, {ECG3,15,(uint8_t)((v->ecg_pga_gain<<2)|v->ecg_ia_gain)}
    };
    /* Multiple writes can partially apply, including if a later RMW read fails. */
    c->_configured=false; invalidate_fifo(c);
    for (size_t i=0;i<sizeof(regs)/sizeof(regs[0]);++i) {
        nsx_sensor_status_t s=update(c,regs[i][0],regs[i][1],regs[i][2]);
        if (s!=NSX_SENSOR_OK) return s;
    }
    c->_config=*v; c->_configured=true;
    return NSX_SENSOR_OK;
}
nsx_sensor_status_t max86150_configure(max86150_context_t *c, const max86150_config_t *config)
{
    nsx_sensor_status_t s=max86150_validate_config(config);
    if (s!=NSX_SENSOR_OK) return s;
    s=ready(c); if (s!=NSX_SENSOR_OK) return s;
    if (!c->_shutdown || c->_fifo_enabled) return NSX_SENSOR_E_STATE;
    max86150_config_t copy=*config;
    return apply_config(c,&copy);
}
nsx_sensor_status_t max86150_init(max86150_context_t *c, const max86150_config_t *config)
{
    if (!c) return NSX_SENSOR_E_ARGUMENT;
    nsx_sensor_status_t s=max86150_validate_config(config);
    if (s!=NSX_SENSOR_OK) return s;
    max86150_config_t copy=*config;
    if (!c->_bound) return NSX_SENSOR_E_STATE;
    if (!c->runtime.delay_us) return NSX_SENSOR_E_UNSUPPORTED;
    uint8_t id; s=max86150_probe(c,&id); if (s!=NSX_SENSOR_OK) return s;
    /* A power-ready flag already present at entry is expected on first boot.
     * Consume it before reset/configuration, then reject any later power loss. */
    c->_configured=false; invalidate_fifo(c);
    uint8_t boot_status[2]; s=read_bytes(c,INT1,boot_status,2);
    if (s!=NSX_SENSOR_OK) return s;
    s=max86150_reset(c,100000); if (s!=NSX_SENSOR_OK) return s;
    s=write_reg(c,SYS,2); if (s!=NSX_SENSOR_OK) return s;
    c->_shutdown=true; c->_fifo_enabled=false;
    s=write_reg(c,EN1,0); if (s!=NSX_SENSOR_OK) return s;
    s=write_reg(c,EN2,0); if (s!=NSX_SENSOR_OK) return s;
    s=apply_config(c,&copy); if (s!=NSX_SENSOR_OK) return s;
    max86150_interrupts_t status; s=max86150_get_interrupts(c,&status);
    if (s!=NSX_SENSOR_OK) { c->_configured=false; return s; }
    c->_fifo_valid=true; c->_snapshot_ready=true; c->_remaining=0;
    return NSX_SENSOR_OK;
}
nsx_sensor_status_t max86150_set_fifo_enable(max86150_context_t *c, uint8_t enable)
{
    if (enable>1) return NSX_SENSOR_E_ARGUMENT;
    nsx_sensor_status_t s=ready(c); if (s!=NSX_SENSOR_OK) return s;
    if (enable && !slot_count(&c->_config)) return NSX_SENSOR_E_STATE;
    if ((bool)enable==c->_fifo_enabled) return NSX_SENSOR_OK;
    s=write_reg(c,SYS,(uint8_t)((c->_shutdown?2:0)|(enable?4:0)));
    if (s!=NSX_SENSOR_OK) return s;
    c->_fifo_enabled=enable!=0;
    if (enable) {
        invalidate_fifo(c); c->_fifo_valid=true;
        /* New FIFO epoch: discard status from the old epoch before waking. */
        max86150_interrupts_t status; s=max86150_get_interrupts(c,&status);
        if (s!=NSX_SENSOR_OK) invalidate_fifo(c);
    }
    return s;
}
nsx_sensor_status_t max86150_get_interrupts(max86150_context_t *c, max86150_interrupts_t *out)
{
    if (!out) return NSX_SENSOR_E_ARGUMENT;
    nsx_sensor_status_t s=ready(c); if (s!=NSX_SENSOR_OK) return s;
    uint8_t bytes[2]; s=read_bytes(c,INT1,bytes,2);
    if (s!=NSX_SENSOR_OK) { invalidate_fifo(c); return s; }
    if (bytes[0]&1) { c->_configured=false; invalidate_fifo(c); return NSX_SENSOR_E_DEVICE; }
    if (bytes[0]&0x80) c->_afull_seen=true;
    const max86150_interrupts_t v={bytes[0],bytes[1]}; *out=v; return NSX_SENSOR_OK;
}
nsx_sensor_status_t max86150_get_int1(max86150_context_t *c, uint8_t *out)
{
    if (!out) return NSX_SENSOR_E_ARGUMENT;
    max86150_interrupts_t s; nsx_sensor_status_t r=max86150_get_interrupts(c,&s);
    if (r==NSX_SENSOR_OK) *out=s.status1;
    return r;
}
nsx_sensor_status_t max86150_get_int2(max86150_context_t *c, uint8_t *out)
{
    if (!out) return NSX_SENSOR_E_ARGUMENT;
    max86150_interrupts_t s; nsx_sensor_status_t r=max86150_get_interrupts(c,&s);
    if (r==NSX_SENSOR_OK) *out=s.status2;
    return r;
}
nsx_sensor_status_t max86150_set_interrupts(max86150_context_t *c, uint8_t a, uint8_t b)
{
    if ((a&~0xe0)||(b&~0x84)) return NSX_SENSOR_E_ARGUMENT;
    nsx_sensor_status_t s=ready(c); if (s!=NSX_SENSOR_OK) return s;
    s=write_reg(c,EN1,a); if (s!=NSX_SENSOR_OK) return s;
    return write_reg(c,EN2,b);
}
nsx_sensor_status_t max86150_powerup(max86150_context_t *c)
{
    nsx_sensor_status_t s=ready(c); if (s!=NSX_SENSOR_OK) return s;
    s=write_reg(c,SYS,c->_fifo_enabled?4:0);
    if (s==NSX_SENSOR_OK) c->_shutdown=false;
    return s;
}
nsx_sensor_status_t max86150_shutdown(max86150_context_t *c)
{
    nsx_sensor_status_t s=max86150_set_fifo_enable(c,0); if (s!=NSX_SENSOR_OK) return s;
    /* SHDN clears interrupts. Preserve full evidence with pushes frozen. */
    max86150_interrupts_t status;
    s=max86150_get_interrupts(c,&status); if (s!=NSX_SENSOR_OK) return s;
    bool was_awake=!c->_shutdown;
    s=write_reg(c,SYS,2);
    if (s==NSX_SENSOR_OK) {
        c->_shutdown=true;
        /* SHDN lets the active conversion finish. Allow a full slowest PPG
         * period before a subsequent FIFO-enable edge could admit old data. */
        if (was_awake) c->runtime.delay_us(c->runtime.user,100000);
    }
    return s;
}
nsx_sensor_status_t max86150_clear_fifo(max86150_context_t *c)
{
    nsx_sensor_status_t s=ready(c); if (s!=NSX_SENSOR_OK) return s;
    s=max86150_shutdown(c); if (s!=NSX_SENSOR_OK) return s;
    invalidate_fifo(c);
    s=write_reg(c,SYS,6); if (s!=NSX_SENSOR_OK) return s;
    s=write_reg(c,SYS,2); if (s!=NSX_SENSOR_OK) return s;
    max86150_interrupts_t status; s=max86150_get_interrupts(c,&status);
    if (s!=NSX_SENSOR_OK) return s;
    c->_afull_seen=false;
    c->_fifo_valid=true; c->_snapshot_ready=true; c->_remaining=0;
    return NSX_SENSOR_OK;
}
nsx_sensor_status_t max86150_start(max86150_context_t *c)
{
    nsx_sensor_status_t s=ready(c); if (s!=NSX_SENSOR_OK) return s;
    if (!slot_count(&c->_config)) return NSX_SENSOR_E_STATE;
    s=max86150_clear_fifo(c); if (s!=NSX_SENSOR_OK) return s;
    s=max86150_set_fifo_enable(c,1); if (s!=NSX_SENSOR_OK) return s;
    return max86150_powerup(c);
}
nsx_sensor_status_t max86150_get_fifo_state(max86150_context_t *c, max86150_fifo_state_t *out)
{
    if (!out) return NSX_SENSOR_E_ARGUMENT;
    nsx_sensor_status_t s=ready(c); if (s!=NSX_SENSOR_OK) return s;
    uint8_t b[3]; s=read_bytes(c,WR,b,3); if (s!=NSX_SENSOR_OK) return s;
    const max86150_fifo_state_t v={b[0]&31,b[1]&31,b[2]&31};
    if (v.overflow_count) invalidate_fifo(c);
    *out=v; return NSX_SENSOR_OK;
}
static nsx_sensor_status_t read_fifo(max86150_context_t *c,
    max86150_frame_t *frames, size_t capacity, size_t *completed, bool freeze)
{
    if (!completed) return NSX_SENSOR_E_ARGUMENT;
    *completed=0;
    if (!frames || !capacity) return NSX_SENSOR_E_ARGUMENT;
    nsx_sensor_status_t s=ready(c); if (s!=NSX_SENSOR_OK) return s;
    unsigned n=slot_count(&c->_config);
    if (!c->_fifo_valid || !n) return NSX_SENSOR_E_STATE;
    if (freeze) {
        s=max86150_set_fifo_enable(c,0); if (s!=NSX_SENSOR_OK) return s;
    } else {
        if (!c->_fifo_enabled || c->_shutdown) return NSX_SENSOR_E_STATE;
        if (c->_config.fifo_rollover || c->_config.almost_full_once ||
            c->_config.almost_full_clear_on_data) return NSX_SENSOR_E_UNSUPPORTED;
        c->_snapshot_ready=false;
    }
    if (!c->_snapshot_ready) {
        max86150_interrupts_t status; s=max86150_get_interrupts(c,&status);
        if (s!=NSX_SENSOR_OK) return s;
        max86150_fifo_state_t state; s=max86150_get_fifo_state(c,&state);
        if (s!=NSX_SENSOR_OK) return s;
        if (state.overflow_count) return NSX_SENSOR_E_OVERFLOW;
        /* Continuous consumption always leaves a queued frame. With no
         * other consumer and no rollover, a known nonempty FIFO cannot become
         * empty. Thus equal pointers mean full even if A_FULL was consumed.
         * Before the first pop, A_FULL cannot be stale from an earlier drain. */
        bool nonempty=c->_remaining!=0;
        c->_remaining=(uint8_t)((state.write_pointer-state.read_pointer)&31);
        if (!c->_remaining && (nonempty || c->_afull_seen)) c->_remaining=32;
        c->_afull_seen=false; c->_snapshot_ready=freeze;
    }
    size_t available=c->_remaining;
    if (!freeze && available) --available; /* Retain one frame until snapshot. */
    if (!available) return NSX_SENSOR_AGAIN;
    size_t count=available<capacity?available:capacity;
    size_t frame_size=3*n;
    while (*completed<count) {
        uint8_t bytes[96];
        size_t limit=c->bus.max_rx_size<sizeof(bytes)?c->bus.max_rx_size:sizeof(bytes);
        size_t batch=limit/frame_size;
        if (batch>count-*completed) batch=count-*completed;
        if (!freeze) {
            uint8_t overflow; s=read_bytes(c,OVF,&overflow,1);
            if (s!=NSX_SENSOR_OK) { invalidate_fifo(c); return s; }
            if (overflow&31) { invalidate_fifo(c); return NSX_SENSOR_E_OVERFLOW; }
        }
        s=read_bytes(c,DATA,bytes,batch*frame_size);
        if (s!=NSX_SENSOR_OK) { invalidate_fifo(c); return s; }
        if (!freeze) {
            max86150_fifo_state_t state; s=max86150_get_fifo_state(c,&state);
            if (s!=NSX_SENSOR_OK) { invalidate_fifo(c); return s; }
            if (state.overflow_count) return NSX_SENSOR_E_OVERFLOW;
        }
        for (size_t i=0;i<batch;++i) {
            max86150_frame_t f={0}; f.count=(uint8_t)n;
            for (size_t j=0;j<n;++j) {
                const uint8_t *b=bytes+i*frame_size+j*3;
                uint32_t v=(uint32_t)b[0]<<16|(uint32_t)b[1]<<8|b[2];
                f.slots[j]=c->_config.slots[j];
                if (f.slots[j]==Max86150SlotEcg) {
                    v &= 0x3ffff; f.value[j]=(int32_t)(v&0x1ffff)-(int32_t)(v&0x20000);
                } else f.value[j]=(int32_t)(v&0x7ffff);
            }
            frames[*completed+i]=f;
        }
        *completed+=batch; c->_remaining=(uint8_t)(c->_remaining-batch);
    }
    if (!freeze) { c->_snapshot_ready=false; c->_afull_seen=false; }
    return NSX_SENSOR_OK;
}
nsx_sensor_status_t max86150_read_fifo_snapshot(max86150_context_t *c,
    max86150_frame_t *frames, size_t capacity, size_t *completed)
{ return read_fifo(c,frames,capacity,completed,true); }
nsx_sensor_status_t max86150_read_fifo(max86150_context_t *c,
    max86150_frame_t *frames, size_t capacity, size_t *completed)
{ return read_fifo(c,frames,capacity,completed,false); }
nsx_sensor_status_t max86150_get_ppg_config(max86150_context_t *c, uint8_t *a, uint8_t *b)
{
    if (!a||!b) return NSX_SENSOR_E_ARGUMENT;
    nsx_sensor_status_t s=ready(c); if (s!=NSX_SENSOR_OK) return s;
    uint8_t v[2]; s=read_bytes(c,PPG1,v,2);
    if (s==NSX_SENSOR_OK) { *a=v[0]; *b=v[1]; }
    return s;
}
static nsx_sensor_status_t set_field(max86150_context_t *c, const max86150_config_t *v,
                                     uint8_t reg, uint8_t mask, uint8_t bits)
{
    nsx_sensor_status_t s=max86150_validate_config(v);
    if (s!=NSX_SENSOR_OK) return s;
    if (!c->_shutdown || c->_fifo_enabled) return NSX_SENSOR_E_STATE;
    s=update(c,reg,mask,bits);
    if (s==NSX_SENSOR_OK) { c->_config=*v; invalidate_fifo(c); }
    return s;
}
nsx_sensor_status_t max86150_set_fifo_slots(max86150_context_t *c, const max86150_slot_type slots[4])
{
    if (!slots) return NSX_SENSOR_E_ARGUMENT;
    nsx_sensor_status_t s=ready(c); if (s!=NSX_SENSOR_OK) return s;
    max86150_config_t v=c->_config; memcpy(v.slots,slots,sizeof(v.slots));
    s=max86150_validate_config(&v); if (s!=NSX_SENSOR_OK) return s;
    if (!c->_shutdown || c->_fifo_enabled) return NSX_SENSOR_E_STATE;
    invalidate_fifo(c);
    s=write_reg(c,SLOT1,(uint8_t)(v.slots[0]|(v.slots[1]<<4)));
    if (s!=NSX_SENSOR_OK) return s;
    s=write_reg(c,SLOT2,(uint8_t)(v.slots[2]|(v.slots[3]<<4)));
    if (s==NSX_SENSOR_OK) c->_config=v;
    return s;
}
nsx_sensor_status_t max86150_set_fifo_slot(max86150_context_t *c, uint8_t slot, max86150_slot_type type)
{
    if (slot>=4) return NSX_SENSOR_E_ARGUMENT;
    nsx_sensor_status_t s=ready(c); if (s!=NSX_SENSOR_OK) return s;
    max86150_config_t v=c->_config; v.slots[slot]=type;
    return max86150_set_fifo_slots(c,v.slots);
}
nsx_sensor_status_t max86150_disable_slots(max86150_context_t *c)
{
    const max86150_slot_type slots[4]={0}; return max86150_set_fifo_slots(c,slots);
}
#define SETTER(name,field,reg,mask,shift) \
nsx_sensor_status_t max86150_set_##name(max86150_context_t *c, uint8_t value) \
{ nsx_sensor_status_t s=ready(c); if (s!=NSX_SENSOR_OK) return s; \
  max86150_config_t v=c->_config; v.field=value; \
  return set_field(c,&v,reg,mask,(uint8_t)(value<<shift)); }
SETTER(ppg_adc_range,ppg_adc_range,PPG1,0xc0,6)
SETTER(ppg_sample_rate,ppg_sample_rate,PPG1,0x3c,2)
SETTER(ppg_pulse_width,ppg_pulse_width,PPG1,3,0)
SETTER(ppg_sample_average,ppg_sample_average,PPG2,7,0)
SETTER(proximity_threshold,proximity_threshold,PROX,0xff,0)
SETTER(ecg_sample_rate,ecg_sample_rate,ECG1,7,0)
SETTER(ecg_pga_gain,ecg_pga_gain,ECG3,12,2)
SETTER(ecg_ia_gain,ecg_ia_gain,ECG3,3,0)
SETTER(almost_full_threshold,almost_full_threshold,FIFO_CFG,15,0)
#undef SETTER
#define BOOL_SETTER(name,field,mask,shift) \
nsx_sensor_status_t max86150_set_##name(max86150_context_t *c, uint8_t value) \
{ if (value>1) return NSX_SENSOR_E_ARGUMENT; nsx_sensor_status_t s=ready(c); if (s!=NSX_SENSOR_OK) return s; \
  max86150_config_t v=c->_config; v.field=value!=0; \
  return set_field(c,&v,FIFO_CFG,mask,(uint8_t)(value<<shift)); }
BOOL_SETTER(almost_full_rollover,fifo_rollover,16,4)
BOOL_SETTER(almost_full_int_options,almost_full_clear_on_data,64,6)
BOOL_SETTER(almost_full_flag_options,almost_full_once,32,5)
#undef BOOL_SETTER
nsx_sensor_status_t max86150_set_led_pulse_amplitude(max86150_context_t *c, uint8_t led, uint8_t value)
{
    if (led>2) return NSX_SENSOR_E_ARGUMENT;
    nsx_sensor_status_t s=ready(c); if (s!=NSX_SENSOR_OK) return s;
    max86150_config_t v=c->_config;
    if (led==2) v.pilot_amplitude=value; else v.led_amplitude[led]=value;
    return set_field(c,&v,led==2?PILOT:(uint8_t)(LED1+led),0xff,value);
}
nsx_sensor_status_t max86150_set_led_current_range(max86150_context_t *c, uint8_t led, uint8_t value)
{
    if (led>1) return NSX_SENSOR_E_ARGUMENT;
    nsx_sensor_status_t s=ready(c); if (s!=NSX_SENSOR_OK) return s;
    max86150_config_t v=c->_config; v.led_range[led]=value;
    return set_field(c,&v,RANGE,(uint8_t)(3<<(2*led)),(uint8_t)(value<<(2*led)));
}
