/**
 * @file ina228.c
 * @author Adam Page (adam.page@ambiq.com)
 * @brief INA228 API Implementation
 * @version 0.1
 * @date 2024-10-01
 *
 * @copyright Copyright (c) 2024
 *
 */

#include <nsx/sensors/ina228.h>
#include <math.h>
#include <float.h>

#define INA228_I2CADDR_DEFAULT 0x40 ///< INA228 default i2c address
#define INA228_REG_CONFIG 0x00      ///< Configuration register
#define INA228_REG_ADCCFG 0x01      ///< ADC configuration register
#define INA228_REG_SHUNTCAL 0x02    ///< Shunt calibration register
#define INA228_REG_SHUNTTEMPCO 0x03 ///< Shunt temperature coefficient register
#define INA228_REG_VSHUNT 0x04      ///< Shunt voltage measurement register
#define INA228_REG_VBUS 0x05        ///< Bus voltage measurement register
#define INA228_REG_DIETEMP 0x06     ///< Temperature measurement register
#define INA228_REG_CURRENT 0x07     ///< Current result register
#define INA228_REG_POWER 0x08       ///< Power result register
#define INA228_REG_ENERGY 0x09      ///< Energy result register
#define INA228_REG_CHARGE 0x0A      ///< Charge result register
#define INA228_REG_DIAGALRT 0x0B    ///< Diagnostic flags and alert register
#define INA228_REG_SOVL 0x0C        ///< Shunt overvoltage threshold register
#define INA228_REG_SUVL 0x0D        ///< Shunt undervoltage threshold register
#define INA228_REG_BOVL 0x0E        ///< Bus overvoltage threshold register
#define INA228_REG_BUVL 0x0F        ///< Bus undervoltage threshold register
#define INA228_REG_TEMPLIMIT 0x10 ///< Temperature over-limit threshold register
#define INA228_REG_PWRLIMIT 0x11  ///< Power over-limit threshold register
#define INA228_REG_MFG_UID 0x3E   ///< Manufacturer ID register
#define INA228_REG_DVC_UID 0x3F   ///< Device ID and revision register
#define INA228_REG_MFG_VAL 0x5449
#define INA228_REG_DVC_VAL 0x228  ///< DEVICE_ID.DIEID, the revision-independent part

static nsx_sensor_status_t
read_bytes(ina228_context_t *ctx, uint8_t reg, uint8_t *bytes, size_t size)
{
    if (!ctx || !ctx->_bound) return NSX_SENSOR_E_STATE;
    nsx_sensor_transfer_t transfer = { &reg, 1, bytes, size };
    return nsx_sensor_bus_transfer(&ctx->bus, &transfer);
}
static nsx_sensor_status_t
ina228_read_register(ina228_context_t *ctx, uint8_t reg, uint16_t *value, uint16_t mask)
{
    if (!value) return NSX_SENSOR_E_ARGUMENT;
    uint8_t bytes[2];
    nsx_sensor_status_t status = read_bytes(ctx, reg, bytes, sizeof(bytes));
    if (status == NSX_SENSOR_OK) *value = (uint16_t)((bytes[0] << 8) | bytes[1]) & mask;
    return status;
}
static nsx_sensor_status_t
ina228_read_register_24(ina228_context_t *ctx, uint8_t reg, uint32_t *value)
{
    if (!value) return NSX_SENSOR_E_ARGUMENT;
    uint8_t bytes[3];
    nsx_sensor_status_t status = read_bytes(ctx, reg, bytes, sizeof(bytes));
    if (status == NSX_SENSOR_OK)
        *value = ((uint32_t)bytes[0] << 16) | ((uint32_t)bytes[1] << 8) | bytes[2];
    return status;
}
static nsx_sensor_status_t
ina228_read_register_40(ina228_context_t *ctx, uint8_t reg, uint64_t *value)
{
    if (!value) return NSX_SENSOR_E_ARGUMENT;
    uint8_t bytes[5];
    nsx_sensor_status_t status = read_bytes(ctx, reg, bytes, sizeof(bytes));
    if (status != NSX_SENSOR_OK) return status;
    uint64_t result = 0;
    for (size_t i = 0; i < sizeof(bytes); ++i) result = (result << 8) | bytes[i];
    *value = result;
    return NSX_SENSOR_OK;
}

static nsx_sensor_status_t
ina228_read_bits(ina228_context_t *ctx, uint8_t reg, uint16_t *value, uint16_t len, uint16_t offset)
{
    if (!value) return NSX_SENSOR_E_ARGUMENT;
    uint16_t mask = (1 << len) - 1;
    uint16_t regValue;
    nsx_sensor_status_t rst;
    rst = ina228_read_register(ctx, reg, &regValue, 0xFFFF);
    if (rst != NSX_SENSOR_OK) return rst;
    *value = (regValue >> offset) & mask;
    return rst;
}

static nsx_sensor_status_t
ina228_write_register(ina228_context_t *ctx, uint8_t reg, uint16_t value, uint16_t mask)
{
    if (!ctx || !ctx->_bound) return NSX_SENSOR_E_STATE;
    uint16_t rdValue;
    uint8_t i2cBuffer[3];
    nsx_sensor_status_t rst;
    i2cBuffer[0] = reg;
    if (mask != 0xFFFF) {
        rst = ina228_read_register(ctx, reg, &rdValue, ~mask);
        if (rst != NSX_SENSOR_OK) return rst;
        value = (rdValue & ~mask) | (value & mask);
    }
    i2cBuffer[1] = (value >> 8) & 0xFF;
    i2cBuffer[2] = value & 0xFF;
    nsx_sensor_transfer_t transfer = { i2cBuffer, 3, NULL, 0 };
    return nsx_sensor_bus_transfer(&ctx->bus, &transfer);
}

static nsx_sensor_status_t
ina228_write_bits(ina228_context_t *ctx, uint8_t reg, uint16_t value, uint16_t len, uint16_t offset)
{
    uint16_t mask = (1 << len) - 1;
    uint16_t regValue;
    nsx_sensor_status_t rst;
    rst = ina228_read_register(ctx, reg, &regValue, 0xFFFF);
    if (rst != NSX_SENSOR_OK) return rst;
    if (reg == INA228_REG_CONFIG) regValue &= 0x3ff0;
    if (reg == INA228_REG_DIAGALRT) regValue &= 0xf000;
    regValue &= ~(mask << offset);
    regValue |= (value & mask) << offset;
    rst = ina228_write_register(ctx, reg, regValue, 0xFFFF);
    return rst;
}


nsx_sensor_status_t
ina228_bind(ina228_context_t *ctx, const nsx_sensor_bus_t *bus)
{
    if (!ctx || nsx_sensor_bus_validate(bus) != NSX_SENSOR_OK) return NSX_SENSOR_E_ARGUMENT;
    if (bus->kind != NSX_SENSOR_BUS_I2C || bus->max_tx_size < 3 || bus->max_rx_size < 5)
        return NSX_SENSOR_E_UNSUPPORTED;
    nsx_sensor_bus_t copy = *bus;
    *ctx = (ina228_context_t){ .bus = copy, ._bound = true };
    return NSX_SENSOR_OK;
}

nsx_sensor_status_t
ina228_get_manufacturer_id(ina228_context_t *ctx, uint16_t *value)
{
    return ina228_read_register(ctx, INA228_REG_MFG_UID, value, 0xFFFF);
}

nsx_sensor_status_t
ina228_get_device_id(ina228_context_t *ctx, uint16_t *value)
{
    return ina228_read_register(ctx, INA228_REG_DVC_UID, value, 0xFFFF);
}

nsx_sensor_status_t
ina228_probe(ina228_context_t *ctx, ina228_identity_t *identity)
{
    if (!identity) return NSX_SENSOR_E_ARGUMENT;
    ina228_identity_t result;
    uint16_t value;
    nsx_sensor_status_t rst;
    rst = ina228_get_manufacturer_id(ctx, &value);
    if (rst != 0) {
        return rst;
    }
    if (value != INA228_REG_MFG_VAL) {
        return NSX_SENSOR_E_DEVICE;
    }
    result.manufacturer_id = value;
    rst = ina228_get_device_id(ctx, &value);
    if (rst != 0) {
        return rst;
    }
    // DEVICE_ID is DIEID in bits 15:4 and silicon REV_ID in bits 3:0, so the
    // revision has to be masked off: a real part reads 0x2281, not 0x228.
    if ((value >> 4) != INA228_REG_DVC_VAL) {
        return NSX_SENSOR_E_DEVICE;
    }
    result.device_id = value;
    *identity = result;
    return NSX_SENSOR_OK;
}


nsx_sensor_status_t
ina228_reset(ina228_context_t *ctx)
{
    if (!ctx || !ctx->_bound) return NSX_SENSOR_E_STATE;
    ctx->_calibrated = false;
    return ina228_write_register(ctx, INA228_REG_CONFIG, 0x8000, 0xffff);
}

nsx_sensor_status_t
ina228_reset_accumulators(ina228_context_t *ctx)
{
    return ina228_write_bits(ctx, INA228_REG_CONFIG, 1, 1, 14);
}

/* Validate before integer conversion, including float underflow and NaN/Inf.
 * Preserve the existing rounded SHUNT_CAL and CURRENT_LSB scaling convention. */
static nsx_sensor_status_t calibration(float resistance, float maximum, uint16_t range,
                                        float *lsb, uint16_t *reg)
{
    if (!isfinite(resistance) || !isfinite(maximum) || resistance <= 0 || maximum <= 0 || range > 1)
        return NSX_SENSOR_E_ARGUMENT;
    float current_lsb = maximum / 524288.0f;
    double cal = 13107200000.0 * (double)resistance * current_lsb * (range ? 4.0 : 1.0);
    if (!(current_lsb > 0) || !isfinite(cal) || cal < 0.5 || cal > 32767.0)
        return NSX_SENSOR_E_CALIBRATION;
    *lsb = current_lsb;
    *reg = (uint16_t)(cal + 0.5);
    return NSX_SENSOR_OK;
}
static nsx_sensor_status_t program_calibration(ina228_context_t *ctx, float resistance,
                                               float maximum, float lsb, uint16_t reg)
{
    ctx->_calibrated = false;
    nsx_sensor_status_t status = ina228_write_register(ctx, INA228_REG_SHUNTCAL, reg, 0xffff);
    if (status != NSX_SENSOR_OK) return status;
    uint16_t readback;
    status = ina228_read_register(ctx, INA228_REG_SHUNTCAL, &readback, 0xffff);
    if (status != NSX_SENSOR_OK) return status;
    if (readback != reg) return NSX_SENSOR_E_CALIBRATION;
    ctx->_shunt_res = resistance;
    ctx->_max_current = maximum;
    ctx->_current_lsb = lsb;
    ctx->_calibrated = true;
    return NSX_SENSOR_OK;
}
nsx_sensor_status_t
ina228_set_shunt(ina228_context_t *ctx, float shunt_res, float max_current)
{
    if (!isfinite(shunt_res) || !isfinite(max_current) || shunt_res <= 0 || max_current <= 0)
        return NSX_SENSOR_E_ARGUMENT;
    uint16_t range, reg;
    float lsb;
    nsx_sensor_status_t status = ina228_get_adc_range(ctx, &range);
    if (status != NSX_SENSOR_OK) return status;
    status = calibration(shunt_res, max_current, range, &lsb, &reg);
    if (status != NSX_SENSOR_OK) return status;
    return program_calibration(ctx, shunt_res, max_current, lsb, reg);
}
nsx_sensor_status_t
ina228_set_adc_range(ina228_context_t *ctx, uint16_t adc_range)
{
    if (adc_range > 1) return NSX_SENSOR_E_ARGUMENT;
    if (!ctx || !ctx->_bound) return NSX_SENSOR_E_STATE;
    bool calibrated = ctx->_calibrated;
    float lsb = 0;
    uint16_t reg = 0, config;
    nsx_sensor_status_t status;
    if (calibrated) {
        status = calibration(ctx->_shunt_res, ctx->_max_current, adc_range, &lsb, &reg);
        if (status != NSX_SENSOR_OK) return status;
    }
    status = ina228_read_register(ctx, INA228_REG_CONFIG, &config, 0xffff);
    if (status != NSX_SENSOR_OK) return status;
    ctx->_calibrated = false;
    /* Never replay self-clearing reset commands read from CONFIG. */
    config = (uint16_t)((config & ~0xc010u) | (adc_range << 4));
    status = ina228_write_register(ctx, INA228_REG_CONFIG, config, 0xffff);
    if (status != NSX_SENSOR_OK || !calibrated) return status;
    return program_calibration(ctx, ctx->_shunt_res, ctx->_max_current, lsb, reg);
}

nsx_sensor_status_t
ina228_get_adc_range(ina228_context_t *ctx, uint16_t *adc_range)
{
    return ina228_read_bits(ctx, INA228_REG_CONFIG, adc_range, 1, 4);
}

nsx_sensor_status_t
ina228_read_die_temp(ina228_context_t *ctx, float *temp)
{
    if (!temp) return NSX_SENSOR_E_ARGUMENT;
    uint16_t uvalue;
    int32_t value;
    nsx_sensor_status_t rst;
    rst = ina228_read_register(ctx, INA228_REG_DIETEMP, &uvalue, 0xFFFF);
    if (rst != NSX_SENSOR_OK) return rst;
    // Coerce to signed, 7.8125 m°C/LSB, convert to °C
    value = (int32_t)(uvalue & 0x7fff) - (int32_t)(uvalue & 0x8000);
    *temp = (float)value * 7.8125 / 1000.0;
    return rst;
}

static nsx_sensor_status_t scaled_result(double value, float *out)
{
    if (!isfinite(value) || value > FLT_MAX || value < -FLT_MAX) return NSX_SENSOR_E_OVERFLOW;
    *out = (float)value;
    return NSX_SENSOR_OK;
}

nsx_sensor_status_t ina228_get_current_lsb(const ina228_context_t *ctx,
                                         float *amperes_per_lsb)
{
    if (!ctx || !amperes_per_lsb) return NSX_SENSOR_E_ARGUMENT;
    if (!ctx->_bound || !ctx->_calibrated) return NSX_SENSOR_E_STATE;
    *amperes_per_lsb = ctx->_current_lsb;
    return NSX_SENSOR_OK;
}

nsx_sensor_status_t
ina228_read_current(ina228_context_t *ctx, float *current)
{
    if (!ctx || !ctx->_bound || !ctx->_calibrated) return NSX_SENSOR_E_STATE;
    if (!current) return NSX_SENSOR_E_ARGUMENT;

    nsx_sensor_status_t rst;
    uint32_t uvalue;
    rst = ina228_read_register_24(ctx, INA228_REG_CURRENT, &uvalue);
    if (rst != NSX_SENSOR_OK) return rst;
    int32_t value = (int32_t)((uvalue >> 4) & 0x7ffffu);
    if ((uvalue & 0x800000U) != 0U) {
        value -= INT32_C(0x80000);
    }
    // Undefined low nibble was discarded above; convert to mA.
    return scaled_result((double)value * ctx->_current_lsb * 1000.0, current);
}


nsx_sensor_status_t
ina228_read_bus_voltage(ina228_context_t *ctx, float *bus_voltage)
{
    if (!bus_voltage) return NSX_SENSOR_E_ARGUMENT;
    nsx_sensor_status_t rst;
    uint32_t value;
    rst = ina228_read_register_24(ctx, INA228_REG_VBUS, &value);
    if (rst != NSX_SENSOR_OK) return rst;
    // Never negative, 195.3125 µV/LSB, convert to mV
    *bus_voltage = (float)((uint32_t)value >> 4) * 195.3125 / 1000.0;
    return rst;
}

nsx_sensor_status_t
ina228_read_shunt_voltage(ina228_context_t *ctx, float *shunt_voltage)
{
    if (!shunt_voltage) return NSX_SENSOR_E_ARGUMENT;
    nsx_sensor_status_t rst;
    uint32_t raw_value;
    uint16_t value16;

    rst = ina228_get_adc_range(ctx, &value16);
    if (rst != 0) {
        return rst;
    }
    float scale = value16 ? 78.125 : 312.5;

    rst = ina228_read_register_24(ctx, INA228_REG_VSHUNT, &raw_value);
    if (rst != NSX_SENSOR_OK) return rst;
    int32_t value = (int32_t)((raw_value >> 4) & 0x7ffffu);
    if ((raw_value & 0x800000U) != 0U) {
        value -= INT32_C(0x80000);
    }

    // 78.125 nV/LSB if adc is 1 else 312.5 nV/LSB
    // Convert to mV
    *shunt_voltage = (float)value * scale / 1000000.0;
    return rst;
}

nsx_sensor_status_t
ina228_read_power(ina228_context_t *ctx, float *power)
{
    if (!ctx || !ctx->_bound || !ctx->_calibrated) return NSX_SENSOR_E_STATE;
    if (!power) return NSX_SENSOR_E_ARGUMENT;
    nsx_sensor_status_t rst;
    uint32_t value;
    rst = ina228_read_register_24(ctx, INA228_REG_POWER, &value);
    if (rst != NSX_SENSOR_OK) return rst;
    // Never negative, 3.2*current_lsb convert to mW
    return scaled_result((double)value * 3.2 * ctx->_current_lsb * 1000, power);
}

nsx_sensor_status_t
ina228_read_energy_raw(ina228_context_t *ctx, uint64_t *energy)
{
    return ina228_read_register_40(ctx, INA228_REG_ENERGY, energy);
}

nsx_sensor_status_t
ina228_read_energy(ina228_context_t *ctx, float *energy) {
    if (!ctx || !ctx->_bound || !ctx->_calibrated) return NSX_SENSOR_E_STATE;
    if (!energy) return NSX_SENSOR_E_ARGUMENT;
    uint64_t value;
    nsx_sensor_status_t rst;
    rst = ina228_read_energy_raw(ctx, &value);
    if (rst != NSX_SENSOR_OK) return rst;
    // Scale in double: a float mantissa holds 24 bits and the register 40,
    // so scaling in float would quantize large accumulations.
    return scaled_result((double)value * 16.0 * 3.2 * ctx->_current_lsb, energy);
}

nsx_sensor_status_t
ina228_read_charge_raw(ina228_context_t *ctx, int64_t *charge)
{
    if (!charge) return NSX_SENSOR_E_ARGUMENT;
    uint64_t uvalue;
    nsx_sensor_status_t rst;
    rst = ina228_read_register_40(ctx, INA228_REG_CHARGE, &uvalue);
    if (rst != NSX_SENSOR_OK) return rst;
    // CHARGE is 40-bit two's complement; sign-extend before scaling
    int64_t value = (int64_t)uvalue;
    if (value & (INT64_C(1) << 39)) {
        value -= (INT64_C(1) << 40);
    }
    *charge = value;
    return rst;
}

nsx_sensor_status_t
ina228_read_charge(ina228_context_t *ctx, float *charge)
{
    if (!ctx || !ctx->_bound || !ctx->_calibrated) return NSX_SENSOR_E_STATE;
    if (!charge) return NSX_SENSOR_E_ARGUMENT;
    int64_t value;
    nsx_sensor_status_t rst;
    rst = ina228_read_charge_raw(ctx, &value);
    if (rst != NSX_SENSOR_OK) return rst;
    return scaled_result((double)value * ctx->_current_lsb, charge);
}

nsx_sensor_status_t
ina228_set_mode(ina228_context_t *ctx, ina228_meas_mode_t mode)
{
    if ((unsigned)mode > 15) return NSX_SENSOR_E_ARGUMENT;
    return ina228_write_bits(ctx, INA228_REG_ADCCFG, mode, 4, 12);
}

nsx_sensor_status_t
ina228_get_mode(ina228_context_t *ctx, ina228_meas_mode_t *mode)
{
    if (!mode) return NSX_SENSOR_E_ARGUMENT;
    uint16_t value;
    nsx_sensor_status_t rst;
    rst = ina228_read_bits(ctx, INA228_REG_ADCCFG, &value, 4, 12);
    if (rst != NSX_SENSOR_OK) return rst;
    *mode = (ina228_meas_mode_t)value;
    return rst;
}

nsx_sensor_status_t
ina228_conversion_ready(ina228_context_t *ctx, uint8_t *ready)
{
    if (!ready) return NSX_SENSOR_E_ARGUMENT;
    uint16_t value;
    nsx_sensor_status_t rst;
    // CNVRF is DIAG_ALRT bit 1. Bit 0 is MEMSTAT, which reads 1 on a healthy
    // part, so polling it reports ready immediately.
    rst = ina228_read_bits(ctx, INA228_REG_DIAGALRT, &value, 1, 1);
    if (rst != NSX_SENSOR_OK) return rst;
    *ready = value;
    return rst;
}

nsx_sensor_status_t
ina228_alert_functions(ina228_context_t *ctx, uint16_t *functions)
{
    if (!functions) return NSX_SENSOR_E_ARGUMENT;
    uint16_t value;
    nsx_sensor_status_t rst;
    rst = ina228_read_bits(ctx, INA228_REG_DIAGALRT, &value, 12, 0);
    if (rst != NSX_SENSOR_OK) return rst;
    *functions = value;
    return rst;
}

nsx_sensor_status_t
ina228_get_alert_latch(ina228_context_t *ctx, ina228_alert_latch_t *latch)
{
    if (!latch) return NSX_SENSOR_E_ARGUMENT;
    uint16_t value;
    nsx_sensor_status_t rst;
    rst = ina228_read_bits(ctx, INA228_REG_DIAGALRT, &value, 1, 15);
    if (rst != NSX_SENSOR_OK) return rst;
    *latch = value ? INA228_ALERT_LATCH_ENABLED : INA228_ALERT_LATCH_TRANSPARENT;
    return rst;
}

nsx_sensor_status_t
ina228_set_alert_latch(ina228_context_t *ctx, ina228_alert_latch_t latch)
{
    if ((unsigned)latch > 1) return NSX_SENSOR_E_ARGUMENT;
    return ina228_write_bits(ctx, INA228_REG_DIAGALRT, latch, 1, 15);
}

nsx_sensor_status_t
ina228_get_alert_polarity(ina228_context_t *ctx, ina228_alert_polarity_t *polarity)
{
    if (!polarity) return NSX_SENSOR_E_ARGUMENT;
    uint16_t value;
    nsx_sensor_status_t rst;
    rst = ina228_read_bits(ctx, INA228_REG_DIAGALRT, &value, 1, 12);
    if (rst != NSX_SENSOR_OK) return rst;
    *polarity = value ? INA228_ALERT_POLARITY_INVERTED : INA228_ALERT_POLARITY_NORMAL;
    return rst;
}

nsx_sensor_status_t
ina228_set_alert_polarity(ina228_context_t *ctx, ina228_alert_polarity_t polarity)
{
    if ((unsigned)polarity > 1) return NSX_SENSOR_E_ARGUMENT;
    return ina228_write_bits(ctx, INA228_REG_DIAGALRT, polarity, 1, 12);
}

nsx_sensor_status_t
ina228_get_current_conversion_time(ina228_context_t *ctx, ina228_conversion_time_t *time)
{
    if (!time) return NSX_SENSOR_E_ARGUMENT;
    uint16_t value;
    nsx_sensor_status_t rst;
    rst = ina228_read_bits(ctx, INA228_REG_ADCCFG, &value, 3, 6);
    if (rst != NSX_SENSOR_OK) return rst;
    *time = (ina228_conversion_time_t)value;
    return rst;

}

nsx_sensor_status_t
ina228_set_current_conversion_time(ina228_context_t *ctx, ina228_conversion_time_t time)
{
    if ((unsigned)time > 7) return NSX_SENSOR_E_ARGUMENT;
    return ina228_write_bits(ctx, INA228_REG_ADCCFG, time, 3, 6);
}

nsx_sensor_status_t
ina228_get_voltage_conversion_time(ina228_context_t *ctx, ina228_conversion_time_t *time)
{
    if (!time) return NSX_SENSOR_E_ARGUMENT;
    uint16_t value;
    nsx_sensor_status_t rst;
    rst = ina228_read_bits(ctx, INA228_REG_ADCCFG, &value, 3, 9);
    if (rst != NSX_SENSOR_OK) return rst;
    *time = (ina228_conversion_time_t)value;
    return rst;
}

nsx_sensor_status_t
ina228_set_voltage_conversion_time(ina228_context_t *ctx, ina228_conversion_time_t time)
{
    if ((unsigned)time > 7) return NSX_SENSOR_E_ARGUMENT;
    return ina228_write_bits(ctx, INA228_REG_ADCCFG, time, 3, 9);
}

nsx_sensor_status_t
ina228_get_temperature_conversion_time(ina228_context_t *ctx, ina228_conversion_time_t *time)
{
    if (!time) return NSX_SENSOR_E_ARGUMENT;
    uint16_t value;
    nsx_sensor_status_t rst;
    rst = ina228_read_bits(ctx, INA228_REG_ADCCFG, &value, 3, 3);
    if (rst != NSX_SENSOR_OK) return rst;
    *time = (ina228_conversion_time_t)value;
    return rst;
}

nsx_sensor_status_t
ina228_set_temperature_conversion_time(ina228_context_t *ctx, ina228_conversion_time_t time)
{
    if ((unsigned)time > 7) return NSX_SENSOR_E_ARGUMENT;
    // Bits 3-5
    return ina228_write_bits(ctx, INA228_REG_ADCCFG, time, 3, 3);
}

nsx_sensor_status_t
ina228_get_averaging_count(ina228_context_t *ctx, ina228_avg_count_t *count)
{
    if (!count) return NSX_SENSOR_E_ARGUMENT;
    uint16_t value;
    nsx_sensor_status_t rst;
    rst = ina228_read_bits(ctx, INA228_REG_ADCCFG, &value, 3, 0);
    if (rst != NSX_SENSOR_OK) return rst;
    *count = (ina228_avg_count_t)(value);
    return rst;

}

nsx_sensor_status_t
ina228_set_averaging_count(ina228_context_t *ctx, ina228_avg_count_t count)
{
    if ((unsigned)count > 7) return NSX_SENSOR_E_ARGUMENT;
    return ina228_write_bits(ctx, INA228_REG_ADCCFG, count, 3, 0);
}

ina228_config_t ina228_default_config(void)
{
    return (ina228_config_t){
        .current_conversion_time = INA228_TIME_1052_us,
        .voltage_conversion_time = INA228_TIME_1052_us,
        .temperature_conversion_time = INA228_TIME_1052_us };
}
nsx_sensor_status_t ina228_validate_config(const ina228_config_t *config)
{
    if (!config || config->adc_range > 1 ||
        (unsigned)config->current_conversion_time > 7 ||
        (unsigned)config->voltage_conversion_time > 7 ||
        (unsigned)config->temperature_conversion_time > 7 ||
        (unsigned)config->averaging_count > 7 || (unsigned)config->alert_latch > 1 ||
        (unsigned)config->alert_polarity > 1) return NSX_SENSOR_E_ARGUMENT;
    float lsb;
    uint16_t reg;
    return calibration(config->shunt_ohms, config->max_current_amps, config->adc_range, &lsb, &reg);
}
nsx_sensor_status_t ina228_init(ina228_context_t *ctx, const ina228_config_t *config)
{
    nsx_sensor_status_t status = ina228_validate_config(config);
    if (status != NSX_SENSOR_OK) return status;
    if (!ctx || !ctx->_bound) return NSX_SENSOR_E_STATE;
    ina228_config_t copy = *config;
    ina228_identity_t identity;
    ctx->_calibrated = false;
    status = ina228_probe(ctx, &identity);
    if (status != NSX_SENSOR_OK) return status;
    uint16_t adc = (uint16_t)((copy.voltage_conversion_time << 9) |
        (copy.current_conversion_time << 6) | (copy.temperature_conversion_time << 3) |
        copy.averaging_count);
    status = ina228_write_register(ctx, INA228_REG_ADCCFG, adc, 0xffff);
    if (status == NSX_SENSOR_OK)
        status = ina228_write_register(ctx, INA228_REG_CONFIG, (uint16_t)(copy.adc_range << 4), 0xffff);
    if (status == NSX_SENSOR_OK) status = ina228_set_shunt(ctx, copy.shunt_ohms, copy.max_current_amps);
    if (status == NSX_SENSOR_OK) status = ina228_reset_accumulators(ctx);
    if (status == NSX_SENSOR_OK)
        status = ina228_write_register(ctx, INA228_REG_DIAGALRT,
            (uint16_t)((copy.alert_latch << 15) | (copy.alert_polarity << 12)), 0xffff);
    if (status != NSX_SENSOR_OK) ctx->_calibrated = false;
    return status;
}
