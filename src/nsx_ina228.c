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

#include "nsx_ina228.h"

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

static uint32_t
ina228_read_register(ina228_context_t *ctx, uint8_t reg, uint16_t *value, uint16_t mask)
{
    uint8_t i2cBuffer[2];
    uint32_t rst;
    i2cBuffer[0] = reg;
    rst = nsx_i2c_write_read(ctx->i2c_config, ctx->addr, i2cBuffer, 1, i2cBuffer, 2);
    if (rst != 0) {
        // The buffer holds no device data on a failed transfer.
        *value = 0;
        return rst;
    }
    *value = (i2cBuffer[0] << 8) | i2cBuffer[1];
    if (mask != 0xFFFF) {
        *value &= mask;
    }
    return rst;
}

static uint32_t
ina228_read_register_24(ina228_context_t *ctx, uint8_t reg, uint32_t *value)
{
    uint8_t i2cBuffer[3];
    uint32_t rst;
    i2cBuffer[0] = reg;
    rst = nsx_i2c_write_read(ctx->i2c_config, ctx->addr, i2cBuffer, 1, i2cBuffer, 3);
    if (rst != 0) {
        *value = 0;
        return rst;
    }
    *value = (i2cBuffer[0] << 16) | (i2cBuffer[1] << 8) | i2cBuffer[2];
    return rst;
}

static uint32_t
ina228_read_register_40(ina228_context_t *ctx, uint8_t reg, uint64_t *value)
{
    uint8_t i2cBuffer[5];
    uint32_t rst;
    i2cBuffer[0] = reg;
    rst = nsx_i2c_write_read(ctx->i2c_config, ctx->addr, i2cBuffer, 1, i2cBuffer, 5);
    if (rst != 0) {
        *value = 0;
        return rst;
    }
    *value = 0;
    for (int i = 0; i < 5; i++) {
        *value = (*value << 8) | i2cBuffer[i];
    }
    return rst;
}

static uint32_t
ina228_read_bits(ina228_context_t *ctx, uint8_t reg, uint16_t *value, uint16_t len, uint16_t offset)
{
    uint16_t mask = (1 << len) - 1;
    uint16_t regValue;
    uint32_t rst;
    rst = ina228_read_register(ctx, reg, &regValue, 0xFFFF);
    *value = (regValue >> offset) & mask;
    return rst;
}

static uint32_t
ina228_write_register(ina228_context_t *ctx, uint8_t reg, uint16_t value, uint16_t mask)
{
    uint16_t rdValue;
    uint8_t i2cBuffer[3];
    uint32_t rst;
    i2cBuffer[0] = reg;
    if (mask != 0xFFFF) {
        rst = ina228_read_register(ctx, reg, &rdValue, ~mask);
        if (rst != 0) {
            // Never read-modify-write on top of a failed read.
            return rst;
        }
        value = (rdValue & ~mask) | (value & mask);
    }
    i2cBuffer[1] = (value >> 8) & 0xFF;
    i2cBuffer[2] = value & 0xFF;
    return nsx_i2c_write(ctx->i2c_config, i2cBuffer, 3, ctx->addr);
}

static uint32_t
ina228_write_bits(ina228_context_t *ctx, uint8_t reg, uint16_t value, uint16_t len, uint16_t offset)
{
    uint16_t mask = (1 << len) - 1;
    uint16_t regValue;
    uint32_t rst;
    rst = ina228_read_register(ctx, reg, &regValue, 0xFFFF);
    if (rst != 0) {
        // Never read-modify-write on top of a failed read.
        return rst;
    }
    regValue &= ~(mask << offset);
    regValue |= (value & mask) << offset;
    rst = ina228_write_register(ctx, reg, regValue, 0xFFFF);
    return rst;
}


uint32_t
ina228_init(ina228_context_t *ctx, nsx_i2c_config_t *i2c_config, uint16_t addr)
{
    ctx->i2c_config = i2c_config;
    ctx->addr = addr;
    ctx->_shunt_res = 0.1;
    ctx->_current_lsb = 0.0001;
    // The cached scaling above matches the device only once ina228_set_shunt()
    // has programmed SHUNT_CAL; until then _calibrated stays 0 and current,
    // power, energy, and charge readings are not meaningful.
    ctx->_calibrated = 0;
    return 0;
}

uint32_t
ina228_get_manufacturer_id(ina228_context_t *ctx, uint16_t *value)
{
    return ina228_read_register(ctx, INA228_REG_MFG_UID, value, 0xFFFF);
}

uint32_t
ina228_get_device_id(ina228_context_t *ctx, uint16_t *value)
{
    return ina228_read_register(ctx, INA228_REG_DVC_UID, value, 0xFFFF);
}

uint32_t
ina228_validate(ina228_context_t *ctx)
{
    uint16_t value;
    uint32_t rst;
    rst = ina228_get_manufacturer_id(ctx, &value);
    if (rst != 0) {
        return rst;
    }
    if (value != INA228_REG_MFG_VAL) {
        return 1;
    }
    rst = ina228_get_device_id(ctx, &value);
    if (rst != 0) {
        return rst;
    }
    // DEVICE_ID is DIEID in bits 15:4 and silicon REV_ID in bits 3:0, so the
    // revision has to be masked off: a real part reads 0x2281, not 0x228.
    if ((value >> 4) != INA228_REG_DVC_VAL) {
        return 1;
    }
    return rst;
}


uint32_t
ina228_reset(ina228_context_t *ctx)
{
    uint32_t rst;
    rst = ina228_write_bits(ctx, INA228_REG_CONFIG, 1, 1, 15);
    if (rst != 0) {
        return rst;
    }
    // The device is back at power-on defaults (SHUNT_CAL = 0x1000), so the
    // cached calibration no longer matches it.
    ctx->_calibrated = 0;
    return 0;
}


uint32_t
ina228_reset_accumulators(ina228_context_t *ctx)
{
    return ina228_write_bits(ctx, INA228_REG_CONFIG, 1, 1, 14);
}

uint32_t
ina228_set_shunt(ina228_context_t *ctx,  float shunt_res, float max_current)
{
    uint16_t adc_range;
    uint32_t rst;

    if (!(shunt_res > 0.0f) || !(max_current > 0.0f)) {
        return 1;
    }

    rst = ina228_get_adc_range(ctx, &adc_range);
    if (rst != 0) {
        return rst;
    }

    float current_lsb = max_current / (float)(1UL << 19);

    // SHUNT_CAL = 13107.2e6 * CURRENT_LSB * R_shunt, x4 when ADCRANGE = 1
    float shunt_cal = 13107.2 * 1000000.0 * shunt_res * current_lsb * (adc_range ? 4.0f : 1.0f);
    // SHUNT_CAL is a 15-bit field. A value that does not fit must be an
    // error: masking it into the field would silently miscalibrate, and
    // converting it to uint16_t would be undefined behavior first.
    if (shunt_cal > 32767.0f) {
        return 1;
    }

    // Plain full-register write: SHUNT_CAL bit 15 is reserved and always
    // reads 0, and the range check above keeps the value out of it, so a
    // read-modify-write would preserve nothing and cost an extra transfer.
    uint16_t shunt_cal_reg = (uint16_t)(shunt_cal + 0.5f);
    rst = ina228_write_register(ctx, INA228_REG_SHUNTCAL, shunt_cal_reg, 0xFFFF);
    if (rst != 0) {
        return rst;
    }

    // SHUNT_CAL = 0 silently zeroes current, power, energy, and charge, so
    // verify the write actually landed before trusting the calibration.
    uint16_t readback;
    rst = ina228_read_register(ctx, INA228_REG_SHUNTCAL, &readback, 0xFFFF);
    if (rst != 0) {
        return rst;
    }
    if (readback != shunt_cal_reg) {
        return 1;
    }

    ctx->_shunt_res = shunt_res;
    ctx->_current_lsb = current_lsb;
    ctx->_calibrated = 1;
    return 0;
}

uint32_t
ina228_set_adc_range(ina228_context_t *ctx, uint16_t adc_range)
{
    uint32_t rst;
    // ADCRANGE is CONFIG bit 4. ADC_CONFIG bit 4 is the middle bit of VTCT.
    rst = ina228_write_bits(ctx, INA228_REG_CONFIG, adc_range, 1, 4);
    if (rst != 0) {
        return rst;
    }
    // SHUNT_CAL carries a x4 factor when ADCRANGE = 1, so an existing
    // calibration must be reprogrammed for the new range.
    if (ctx->_calibrated) {
        return ina228_set_shunt(ctx, ctx->_shunt_res, ctx->_current_lsb * (float)(1UL << 19));
    }
    return 0;
}

uint32_t
ina228_get_adc_range(ina228_context_t *ctx, uint16_t *adc_range)
{
    return ina228_read_bits(ctx, INA228_REG_CONFIG, adc_range, 1, 4);
}

uint32_t
ina228_read_die_temp(ina228_context_t *ctx, float *temp)
{
    uint16_t uvalue;
    int16_t value;
    uint32_t rst;
    rst = ina228_read_register(ctx, INA228_REG_DIETEMP, &uvalue, 0xFFFF);
    // Coerce to signed, 7.8125 m°C/LSB, convert to °C
    value = (int16_t)uvalue;
    *temp = (float)value * 7.8125 / 1000.0;
    return rst;
}

uint32_t
ina228_read_current(ina228_context_t *ctx, float *current)
{

    uint32_t rst;
    uint32_t uvalue;
    rst = ina228_read_register_24(ctx, INA228_REG_CURRENT, &uvalue);
    int32_t value = (int32_t)(uvalue & 0x7FFFFFU);
    if ((uvalue & 0x800000U) != 0U) {
        value -= INT32_C(0x800000);
    }
    // /16 is because last 4 bits are dont care, convert to mA
    *current = (float)value / 16.0 * ctx->_current_lsb * 1000.0;
    return rst;
}


uint32_t
ina228_read_bus_voltage(ina228_context_t *ctx, float *bus_voltage)
{
    uint32_t rst;
    uint32_t value;
    rst = ina228_read_register_24(ctx, INA228_REG_VBUS, &value);
    // Never negative, 195.3125 µV/LSB, convert to mV
    *bus_voltage = (float)((uint32_t)value >> 4) * 195.3125 / 1000.0;
    return rst;
}

uint32_t
ina228_read_shunt_voltage(ina228_context_t *ctx, float *shunt_voltage)
{
    uint32_t rst;
    uint32_t raw_value;
    uint16_t value16;

    rst = ina228_get_adc_range(ctx, &value16);
    if (rst != 0) {
        return rst;
    }
    float scale = value16 ? 78.125 : 312.5;

    rst = ina228_read_register_24(ctx, INA228_REG_VSHUNT, &raw_value);
    if (rst != 0) {
        return rst;
    }
    int32_t value = (int32_t)(raw_value & 0x7FFFFFU);
    if ((raw_value & 0x800000U) != 0U) {
        value -= INT32_C(0x800000);
    }

    // 78.125 nV/LSB if adc is 1 else 312.5 nV/LSB
    // /16 is because last 4 bits are dont care
    // Convert to mV
    *shunt_voltage = (float)value / 16.0 * scale / 1000000.0;
    return rst;
}

uint32_t
ina228_read_power(ina228_context_t *ctx, float *power)
{
    uint32_t rst;
    uint32_t value;
    rst = ina228_read_register_24(ctx, INA228_REG_POWER, &value);
    // Never negative, 3.2*current_lsb convert to mW
    *power = (float)value * 3.2 * ctx->_current_lsb * 1000;
    return rst;
}

uint32_t
ina228_read_energy_raw(ina228_context_t *ctx, uint64_t *energy)
{
    return ina228_read_register_40(ctx, INA228_REG_ENERGY, energy);
}

uint32_t
ina228_read_energy(ina228_context_t *ctx, float *energy) {
    uint64_t value;
    uint32_t rst;
    rst = ina228_read_energy_raw(ctx, &value);
    // Scale in double: a float mantissa holds 24 bits and the register 40,
    // so scaling in float would quantize large accumulations.
    *energy = (float)((double)value * 16.0 * 3.2 * (double)ctx->_current_lsb);
    return rst;
}

uint32_t
ina228_read_charge_raw(ina228_context_t *ctx, int64_t *charge)
{
    uint64_t uvalue;
    uint32_t rst;
    rst = ina228_read_register_40(ctx, INA228_REG_CHARGE, &uvalue);
    // CHARGE is 40-bit two's complement; sign-extend before scaling
    int64_t value = (int64_t)uvalue;
    if (value & (INT64_C(1) << 39)) {
        value -= (INT64_C(1) << 40);
    }
    *charge = value;
    return rst;
}

uint32_t
ina228_read_charge(ina228_context_t *ctx, float *charge)
{
    int64_t value;
    uint32_t rst;
    rst = ina228_read_charge_raw(ctx, &value);
    *charge = (float)((double)value * (double)ctx->_current_lsb);
    return rst;
}

uint32_t
ina228_set_mode(ina228_context_t *ctx, ina228_meas_mode_t mode)
{
    return ina228_write_bits(ctx, INA228_REG_ADCCFG, mode, 4, 12);
}

uint32_t
ina228_get_mode(ina228_context_t *ctx, ina228_meas_mode_t *mode)
{
    uint16_t value;
    uint32_t rst;
    rst = ina228_read_bits(ctx, INA228_REG_ADCCFG, &value, 4, 12);
    *mode = (ina228_meas_mode_t)value;
    return rst;
}

uint32_t
ina228_conversion_ready(ina228_context_t *ctx, uint8_t *ready)
{
    uint16_t value;
    uint32_t rst;
    // CNVRF is DIAG_ALRT bit 1. Bit 0 is MEMSTAT, which reads 1 on a healthy
    // part, so polling it reports ready immediately.
    rst = ina228_read_bits(ctx, INA228_REG_DIAGALRT, &value, 1, 1);
    *ready = value;
    return rst;
}

uint32_t
ina228_alert_functions(ina228_context_t *ctx, uint16_t *functions)
{
    uint16_t value;
    uint32_t rst;
    rst = ina228_read_bits(ctx, INA228_REG_DIAGALRT, &value, 12, 0);
    *functions = value;
    return rst;
}

uint32_t
ina228_get_alert_latch(ina228_context_t *ctx, ina228_alert_latch_t *latch)
{
    uint16_t value;
    uint32_t rst;
    rst = ina228_read_bits(ctx, INA228_REG_DIAGALRT, &value, 1, 15);
    *latch = value ? INA228_ALERT_LATCH_ENABLED : INA228_ALERT_LATCH_TRANSPARENT;
    return rst;
}

uint32_t
ina228_set_alert_latch(ina228_context_t *ctx, ina228_alert_latch_t latch)
{
    return ina228_write_bits(ctx, INA228_REG_DIAGALRT, latch, 1, 15);
}

uint32_t
ina228_get_alert_polarity(ina228_context_t *ctx, ina228_alert_polarity_t *polarity)
{
    uint16_t value;
    uint32_t rst;
    rst = ina228_read_bits(ctx, INA228_REG_DIAGALRT, &value, 1, 12);
    *polarity = value ? INA228_ALERT_POLARITY_INVERTED : INA228_ALERT_POLARITY_NORMAL;
    return rst;
}

uint32_t
ina228_set_alert_polarity(ina228_context_t *ctx, ina228_alert_polarity_t polarity)
{
    return ina228_write_bits(ctx, INA228_REG_DIAGALRT, polarity, 1, 12);
}

uint32_t
ina228_get_current_conversion_time(ina228_context_t *ctx, ina228_conversion_time_t *time)
{
    uint16_t value;
    uint32_t rst;
    rst = ina228_read_bits(ctx, INA228_REG_ADCCFG, &value, 3, 6);
    *time = (ina228_conversion_time_t)value;
    return rst;

}

uint32_t
ina228_set_current_conversion_time(ina228_context_t *ctx, ina228_conversion_time_t time)
{
    return ina228_write_bits(ctx, INA228_REG_ADCCFG, time, 3, 6);
}

uint32_t
ina228_get_voltage_conversion_time(ina228_context_t *ctx, ina228_conversion_time_t *time)
{
    uint16_t value;
    uint32_t rst;
    rst = ina228_read_bits(ctx, INA228_REG_ADCCFG, &value, 3, 9);
    *time = (ina228_conversion_time_t)value;
    return rst;
}

uint32_t
ina228_set_voltage_conversion_time(ina228_context_t *ctx, ina228_conversion_time_t time)
{
    return ina228_write_bits(ctx, INA228_REG_ADCCFG, time, 3, 9);
}

uint32_t
ina228_get_temperature_conversion_time(ina228_context_t *ctx, ina228_conversion_time_t *time)
{
    uint16_t value;
    uint32_t rst;
    rst = ina228_read_bits(ctx, INA228_REG_ADCCFG, &value, 3, 3);
    *time = (ina228_conversion_time_t)value;
    return rst;
}

uint32_t
ina228_set_temperature_conversion_time(ina228_context_t *ctx, ina228_conversion_time_t time)
{
    // Bits 3-5
    return ina228_write_bits(ctx, INA228_REG_ADCCFG, time, 3, 3);
}

uint32_t
ina228_get_averaging_count(ina228_context_t *ctx, ina228_avg_count_t *count)
{
    uint16_t value;
    uint32_t rst;
    rst = ina228_read_bits(ctx, INA228_REG_ADCCFG, &value, 3, 0);
    *count = (ina228_avg_count_t)(value);
    return rst;

}

uint32_t
ina228_set_averaging_count(ina228_context_t *ctx, ina228_avg_count_t count)
{
    return ina228_write_bits(ctx, INA228_REG_ADCCFG, count, 3, 0);
}
