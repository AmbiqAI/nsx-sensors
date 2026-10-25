/* SPDX-License-Identifier: BSD-3-Clause */
#include <nsx/sensors/t5838.h>
#define REG_ENABLE 0x29u
#define REG_FLOOR_MSB 0x2au
#define REG_FLOOR_LSB 0x2bu
#define REG_DIGITAL_RESERVED 0x2cu
#define REG_ALGORITHM 0x2du
#define REG_REL_PULSE 0x2eu
#define REG_PULSE_MSB 0x2fu
#define REG_ABS_PULSE 0x30u
#define REG_ABS_LSB 0x31u
#define REG_ABS_MSB 0x32u
#define REG_REL_THRESHOLD 0x33u
#define REG_ANALOG_LPF 0x35u
#define REG_ANALOG_THRESHOLD 0x36u
nsx_sensor_status_t t5838_encode_write(uint8_t reg, uint8_t value, t5838_write_frame_t *frame)
{
    if (!frame) return NSX_SENSOR_E_ARGUMENT;
    const uint8_t payload[] = {0xa6, reg, value};
    t5838_write_frame_t result = {.space_cycles = 10, .pre_cycles = 5, .post_cycles = 5};
    result.high_cycles[0] = 10;
    for (size_t byte = 0; byte < 3; ++byte)
        for (unsigned bit = 0; bit < 8; ++bit)
            result.high_cycles[1 + byte * 8 + bit] =
                (payload[byte] & (0x80u >> bit)) ? 30 : 10;
    result.high_cycles[25] = 130; /* Strictly greater than 128 CLK periods. */
    *frame = result;
    return NSX_SENSOR_OK;
}
void t5838_invalidate(t5838_context_t *ctx)
{
    if (ctx) { ctx->_configured = false; ctx->_enabled = false; }
}
static nsx_sensor_status_t write_reg(t5838_context_t *ctx, uint8_t reg, uint8_t value)
{
    t5838_write_frame_t frame;
    (void)t5838_encode_write(reg, value, &frame);
    nsx_sensor_status_t s = ctx->control.ops->write_frame(ctx->control.user, &frame);
    if (s != NSX_SENSOR_OK) t5838_invalidate(ctx);
    return s;
}
static nsx_sensor_status_t ready(const t5838_context_t *ctx)
{
    if (!ctx) return NSX_SENSOR_E_ARGUMENT;
    return ctx->_bound && ctx->_configured ? NSX_SENSOR_OK : NSX_SENSOR_E_STATE;
}
nsx_sensor_status_t t5838_bind(t5838_context_t *ctx, const t5838_control_t *control)
{
    if (!ctx || !control || !control->ops || !control->ops->write_frame)
        return NSX_SENSOR_E_ARGUMENT;
    *ctx = (t5838_context_t){.control = *control, ._bound = true};
    return NSX_SENSOR_OK;
}
t5838_config_t t5838_default_config(void)
{
    return (t5838_config_t){.mode = T5838_AAD_ANALOG, .analog_lpf = T5838_LPF_4400_HZ,
        .analog_threshold = 4, .algorithm = T5838_THRESHOLD_BOTH,
        .absolute_threshold = 0x113, .floor = 0x16, .relative_threshold = 0x24};
}
nsx_sensor_status_t t5838_validate_config(const t5838_config_t *c)
{
    if (!c || (unsigned)c->mode > T5838_AAD_DIGITAL2) return NSX_SENSOR_E_ARGUMENT;
    if (c->mode == T5838_AAD_ANALOG &&
        ((unsigned)c->analog_lpf < 1 || (unsigned)c->analog_lpf > 7 || c->analog_threshold > 15))
        return NSX_SENSOR_E_ARGUMENT;
    if ((c->mode == T5838_AAD_DIGITAL1 || c->mode == T5838_AAD_DIGITAL2) &&
        ((unsigned)c->algorithm < 1 || (unsigned)c->algorithm > 3 ||
         c->absolute_threshold < 0x0f || c->absolute_threshold > 0x7bc ||
         c->floor < 0x0f || c->floor > 0x7bc || c->relative_threshold < 0x24 ||
         c->relative_pulse_min > 300 || c->absolute_pulse_min > 3500))
        return NSX_SENSOR_E_ARGUMENT;
    return NSX_SENSOR_OK;
}
nsx_sensor_status_t t5838_init(t5838_context_t *ctx, const t5838_config_t *config)
{
    static const uint8_t unlock[] = {0x5c, 0x3e, 0x6f, 0x3b, 0x4c};
    if (!ctx) return NSX_SENSOR_E_ARGUMENT;
    nsx_sensor_status_t s = t5838_validate_config(config);
    if (s != NSX_SENSOR_OK) return s;
    if (!ctx->_bound) return NSX_SENSOR_E_STATE;
    const t5838_config_t c = *config;
    t5838_invalidate(ctx);
#define TRY(expr) do { s = (expr); if (s != NSX_SENSOR_OK) return s; } while (0)
    for (size_t i = 0; i < sizeof(unlock); ++i) TRY(write_reg(ctx, unlock[i], 0));
    TRY(write_reg(ctx, REG_ENABLE, 0));
    if (c.mode == T5838_AAD_ANALOG) {
        TRY(write_reg(ctx, REG_ANALOG_LPF, (uint8_t)c.analog_lpf));
        TRY(write_reg(ctx, REG_ANALOG_THRESHOLD, c.analog_threshold));
    } else if (c.mode != T5838_AAD_OFF) {
        TRY(write_reg(ctx, REG_FLOOR_MSB, (uint8_t)(c.floor >> 8)));
        TRY(write_reg(ctx, REG_FLOOR_LSB, (uint8_t)c.floor));
        /* DS-000383 rev1.2 sections 5.6.11/12 require these reserved values. */
        TRY(write_reg(ctx, REG_DIGITAL_RESERVED, 0x32));
        TRY(write_reg(ctx, REG_ALGORITHM, (uint8_t)((unsigned)c.algorithm << 6)));
        TRY(write_reg(ctx, REG_REL_PULSE, (uint8_t)c.relative_pulse_min));
        TRY(write_reg(ctx, REG_PULSE_MSB, (uint8_t)(((c.absolute_pulse_min >> 8) << 4) |
                                                  (c.relative_pulse_min >> 8))));
        TRY(write_reg(ctx, REG_ABS_PULSE, (uint8_t)c.absolute_pulse_min));
        TRY(write_reg(ctx, REG_ABS_LSB, (uint8_t)c.absolute_threshold));
        TRY(write_reg(ctx, REG_ABS_MSB, (uint8_t)(0x40u | (c.absolute_threshold >> 8))));
        TRY(write_reg(ctx, REG_REL_THRESHOLD, c.relative_threshold));
    }
#undef TRY
    ctx->_config = c; ctx->_configured = true;
    return NSX_SENSOR_OK;
}
nsx_sensor_status_t t5838_enable(t5838_context_t *ctx)
{
    static const uint8_t enables[] = {0, 8, 1, 2};
    nsx_sensor_status_t s = ready(ctx);
    if (s != NSX_SENSOR_OK) return s;
    s = write_reg(ctx, REG_ENABLE, enables[ctx->_config.mode]);
    if (s == NSX_SENSOR_OK) ctx->_enabled = ctx->_config.mode != T5838_AAD_OFF;
    return s;
}
nsx_sensor_status_t t5838_disable(t5838_context_t *ctx)
{
    nsx_sensor_status_t s = ready(ctx);
    if (s != NSX_SENSOR_OK) return s;
    s = write_reg(ctx, REG_ENABLE, 0);
    t5838_invalidate(ctx);
    return s;
}
nsx_sensor_status_t t5838_read_wake(t5838_context_t *ctx, bool *active)
{
    bool value;
    if (!active) return NSX_SENSOR_E_ARGUMENT;
    nsx_sensor_status_t s = ready(ctx);
    if (s != NSX_SENSOR_OK) return s;
    if (!ctx->_enabled) return NSX_SENSOR_E_STATE;
    if (!ctx->control.ops->read_wake) return NSX_SENSOR_E_UNSUPPORTED;
    s = ctx->control.ops->read_wake(ctx->control.user, &value);
    if (s == NSX_SENSOR_OK) *active = value;
    return s;
}
nsx_sensor_status_t t5838_validate_operating_clock(t5838_aad_mode_t aad,
                                                 t5838_audio_mode_t audio, uint32_t hz)
{
    bool valid;
    if ((unsigned)aad > T5838_AAD_DIGITAL2) return NSX_SENSOR_E_ARGUMENT;
    switch (audio) {
    case T5838_AUDIO_SLEEP: valid = hz == 0 || (hz > 50000 && hz < 200000); break;
    case T5838_AUDIO_LOW_POWER: valid = hz >= 400000 && hz <= 800000; break;
    case T5838_AUDIO_HIGH_QUALITY: valid = hz >= 2000000 && hz <= 3700000; break;
    case T5838_AUDIO_ULTRASONIC: valid = hz >= 4200000 && hz <= 4800000; break;
    default: return NSX_SENSOR_E_ARGUMENT;
    }
    if (!valid) return NSX_SENSOR_E_ARGUMENT;
    if ((aad == T5838_AAD_ANALOG || aad == T5838_AAD_DIGITAL2) && hz != 0)
        return NSX_SENSOR_E_ARGUMENT;
    if (aad == T5838_AAD_DIGITAL1 && audio != T5838_AUDIO_LOW_POWER)
        return NSX_SENSOR_E_ARGUMENT;
    return NSX_SENSOR_OK;
}
