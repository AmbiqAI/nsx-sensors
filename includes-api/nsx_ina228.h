/**
 * @file ina228.h
 * @author Adam Page (adam.page@ambiq.com)
 * @brief INA228 API Header file
 * @version 0.1
 * @date 2024-10-01
 *
 * @copyright Copyright (c) 2024
 *
 */

#ifndef NSX_INA228_H
#define NSX_INA228_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

#include "nsx_i2c.h"

typedef struct {
  nsx_i2c_config_t *i2c_config;
  uint16_t addr;
  float _shunt_res;
  float _current_lsb;
  uint8_t _calibrated; ///< Nonzero once ina228_set_shunt() has programmed SHUNT_CAL
} ina228_context_t;


/**
 * @brief Mode options.
 *
 * Allowed values for setMode.
 */
typedef enum {
  /**< SHUTDOWN: Minimize quiescient current and turn off current into the
  device inputs. Set another mode to exit shutown mode **/
  INA228_MODE_SHUTDOWN = 0x00,

  /**< Triggered bus voltage, single shot **/
  INA228_MODE_TRIG_BUS = 0x01,
  /**< Triggered shunt voltage, single shot **/
  INA228_MODE_TRIG_SHUNT = 0x02,
  /**< Triggered shunt voltage and bus voltage, single shot **/
  INA228_MODE_TRIG_BUS_SHUNT = 0x03,
  /**< Triggered temperature, single shot **/
  INA228_MODE_TRIG_TEMP = 0x04,
  /**< Triggered temperature and bus voltage, single shot **/
  INA228_MODE_TRIG_TEMP_BUS = 0x05,
  /**< Triggered temperature and shunt voltage, single shot **/
  INA228_MODE_TRIG_TEMP_SHUNT = 0x06,
  /**< Triggered bus voltage, shunt voltage and temperature, single shot **/
  INA228_MODE_TRIG_TEMP_BUS_SHUNT = 0x07,

  /**< Shutdown **/
  INA228_MODE_SHUTDOWN2 = 0x08,
  /**< Continuous bus voltage only **/
  INA228_MODE_CONT_BUS = 0x09,
  /**< Continuous shunt voltage only **/
  INA228_MODE_CONT_SHUNT = 0x0A,
  /**< Continuous shunt and bus voltage **/
  INA228_MODE_CONT_BUS_SHUNT = 0x0B,
  /**< Continuous temperature only **/
  INA228_MODE_CONT_TEMP = 0x0C,
  /**< Continuous bus voltage and temperature **/
  INA228_MODE_CONT_TEMP_BUS = 0x0D,
  /**< Continuous temperature and shunt voltage **/
  INA228_MODE_CONT_TEMP_SHUNT = 0x0E,
  /**< Continuous bus voltage, shunt voltage and temperature **/
  INA228_MODE_CONT_TEMP_BUS_SHUNT = 0x0F,

  /**< TRIGGERED: Trigger a one-shot measurement of temp, current and bus
  voltage. Set the TRIGGERED mode again to take a new measurement **/
  INA228_MODE_TRIGGERED = INA228_MODE_TRIG_TEMP_BUS_SHUNT,
  /**< CONTINUOUS: (Default) Continuously update the temp, current, bus
  voltage and power registers with new measurements **/
  INA228_MODE_CONTINUOUS = INA228_MODE_CONT_TEMP_BUS_SHUNT
} ina228_meas_mode_t;

/**
 * @brief Conversion Time options.
 *
 * Allowed values for setCurrentConversionTime and setVoltageConversionTime.
 */
typedef enum _conversion_time {
  INA228_TIME_50_us,   ///< Measurement time: 50us
  INA228_TIME_84_us,   ///< Measurement time: 84us
  INA228_TIME_150_us,  ///< Measurement time: 150us
  INA228_TIME_280_us,  ///< Measurement time: 280us
  INA228_TIME_540_us,  ///< Measurement time: 540us
  INA228_TIME_1052_us, ///< Measurement time: 1052us
  INA228_TIME_2074_us, ///< Measurement time: 2074us
  INA228_TIME_4120_us, ///< Measurement time: 4120us
} ina228_conversion_time_t;

/**
 * @brief Averaging Count options.
 *
 * Allowed values forsetAveragingCount.
 */
typedef enum {
  INA228_COUNT_1,    ///< Window size: 1 sample (Default)
  INA228_COUNT_4,    ///< Window size: 4 samples
  INA228_COUNT_16,   ///< Window size: 16 samples
  INA228_COUNT_64,   ///< Window size: 64 samples
  INA228_COUNT_128,  ///< Window size: 128 samples
  INA228_COUNT_256,  ///< Window size: 256 samples
  INA228_COUNT_512,  ///< Window size: 512 samples
  INA228_COUNT_1024, ///< Window size: 1024 samples
} ina228_avg_count_t;

/**
 * @brief DIAG_ALRT flag masks, as returned by ina228_alert_functions().
 *
 * Each value is the flag's real bit position in the DIAG_ALRT register.
 * MEMSTAT (bit 0) is deliberately not listed: it reads 1 on a healthy part,
 * so it is not a fault flag.
 */
typedef enum  {
  INA228_ALERT_NONE = 0x0,              ///< No flag set (Default)
  INA228_ALERT_CONVERSION_READY = 0x2,  ///< CNVRF: conversion ready
  INA228_ALERT_OVERPOWER = 0x4,         ///< POL: power over limit
  INA228_ALERT_UNDERVOLTAGE = 0x8,      ///< BUSUL: bus voltage under limit
  INA228_ALERT_OVERVOLTAGE = 0x10,      ///< BUSOL: bus voltage over limit
  INA228_ALERT_UNDERCURRENT = 0x20,     ///< SHNTUL: shunt voltage under limit
  INA228_ALERT_OVERCURRENT = 0x40,      ///< SHNTOL: shunt voltage over limit
  INA228_ALERT_OVERTEMP = 0x80,         ///< TMPOL: temperature over limit
  INA228_ALERT_MATH_OVERFLOW = 0x200,   ///< MATHOF: current/power arithmetic overflowed
  INA228_ALERT_CHARGE_OVERFLOW = 0x400, ///< CHARGEOF: CHARGE register overflowed
  INA228_ALERT_ENERGY_OVERFLOW = 0x800, ///< ENERGYOF: ENERGY register overflowed
} ina228_alert_type_t;

/**
 * @brief Alert pin polarity options.
 *
 * Allowed values for setAlertPolarity.
 */
typedef enum {
  INA228_ALERT_POLARITY_NORMAL = 0x0, ///< Active high open-collector (Default)
  INA228_ALERT_POLARITY_INVERTED = 0x1, ///< Active low open-collector
} ina228_alert_polarity_t;

/**
 * @brief Alert pin latch options.
 *
 * Allowed values for setAlertLatch.
 */
typedef enum {
  INA228_ALERT_LATCH_ENABLED = 0x1,     /**< Alert will latch until Mask/Enable
                                           register is read **/
  INA228_ALERT_LATCH_TRANSPARENT = 0x0, /**< Alert will reset when fault is
                                           cleared **/
} ina228_alert_latch_t;


/**
 * @brief Bind a context to an I2C bus and address. Performs no I2C traffic.
 *
 * The device powers up with SHUNT_CAL = 0x1000, which does not correspond to
 * any calibration this driver knows about, so current, power, energy, and
 * charge readings are not meaningful until ina228_set_shunt() has been
 * called. The same applies again after ina228_reset().
 */
uint32_t
ina228_init(ina228_context_t *ctx, nsx_i2c_config_t *i2c_config, uint16_t addr);

uint32_t
ina228_get_manufacturer_id(ina228_context_t *ctx, uint16_t *value);

uint32_t
ina228_get_device_id(ina228_context_t *ctx, uint16_t *value);

uint32_t
ina228_validate(ina228_context_t *ctx);

/**
 * @brief Reset the device to power-on defaults (CONFIG.RST).
 *
 * This reverts SHUNT_CAL to its reset value, so ina228_set_shunt() must be
 * called again before current, power, energy, or charge readings are
 * meaningful.
 */
uint32_t
ina228_reset(ina228_context_t *ctx);

/**
 * @brief Reset the ENERGY and CHARGE accumulators (CONFIG.RSTACC).
 *
 * This also clears DIAG_ALRT.MATHOF. It does not clear DIAG_ALRT.ENERGYOF or
 * DIAG_ALRT.CHARGEOF: those clear only when the ENERGY or CHARGE register is
 * read, so a windowed measurement that resets the accumulators and then polls
 * the overflow flags can observe a flag raised before the reset. Read the
 * accumulator itself to clear it. Note that the ADC free-runs in continuous
 * mode, so MATHOF can be raised again immediately while SHUNT_CAL is still
 * zero, before ina228_set_shunt() has run.
 */
uint32_t
ina228_reset_accumulators(ina228_context_t *ctx);

/**
 * @brief Program SHUNT_CAL for a shunt resistance (ohms) and the maximum
 * expected current (amperes).
 *
 * Fails without touching the device or the cached calibration if either
 * input is not positive, or if the computed SHUNT_CAL exceeds the register's
 * 15-bit field (13107.2e6 * (max_current / 2^19) * shunt_res, x4 when
 * ADCRANGE = 1, must be <= 32767) — in that case pick a smaller shunt or a
 * larger range.
 */
uint32_t
ina228_set_shunt(ina228_context_t *ctx,  float shunt_res, float max_current);

/**
 * @brief Select the shunt full-scale range (CONFIG.ADCRANGE): 0 = +/-163.84
 * mV, 1 = +/-40.96 mV.
 *
 * SHUNT_CAL depends on ADCRANGE, so if ina228_set_shunt() has already run,
 * this reprograms SHUNT_CAL for the new range. That reprogramming can fail
 * (see ina228_set_shunt()); on failure the range has changed but SHUNT_CAL
 * still holds the old range's value, and ina228_set_shunt() must be called
 * before measurements are trusted.
 */
uint32_t
ina228_set_adc_range(ina228_context_t *ctx, uint16_t adc_range);

uint32_t
ina228_get_adc_range(ina228_context_t *ctx, uint16_t *adc_range);

uint32_t
ina228_read_die_temp(ina228_context_t *ctx, float *temp);

uint32_t
ina228_read_current(ina228_context_t *ctx, float *current);

uint32_t
ina228_read_bus_voltage(ina228_context_t *ctx, float *bus_voltage);

uint32_t
ina228_read_shunt_voltage(ina228_context_t *ctx, float *shunt_voltage);

uint32_t
ina228_read_power(ina228_context_t *ctx, float *power);

/**
 * @brief Energy in joules. Scaled from the raw register through double, but
 * the float result still carries only 24 mantissa bits — for windowed
 * (delta) measurements subtract two ina228_read_energy_raw() readings
 * instead, which stay exact over the full 40-bit range.
 */
uint32_t
ina228_read_energy(ina228_context_t *ctx, float *energy);

/**
 * @brief Raw unsigned 40-bit ENERGY register, exact. Multiply by
 * 16 * 3.2 * CURRENT_LSB for joules. Reading clears DIAG_ALRT.ENERGYOF.
 */
uint32_t
ina228_read_energy_raw(ina228_context_t *ctx, uint64_t *energy);

/**
 * @brief Charge in coulombs. See ina228_read_energy() for float precision;
 * windowed measurements should subtract ina228_read_charge_raw() readings.
 */
uint32_t
ina228_read_charge(ina228_context_t *ctx, float *charge);

/**
 * @brief Raw sign-extended 40-bit CHARGE register, exact. Multiply by
 * CURRENT_LSB for coulombs. Reading clears DIAG_ALRT.CHARGEOF.
 */
uint32_t
ina228_read_charge_raw(ina228_context_t *ctx, int64_t *charge);

uint32_t
ina228_set_mode(ina228_context_t *ctx, ina228_meas_mode_t mode);

uint32_t
ina228_get_mode(ina228_context_t *ctx, ina228_meas_mode_t *mode);

/**
 * @brief Poll DIAG_ALRT.CNVRF. When ALATCH = 1 (latched mode), the read
 * itself clears CNVRF and the latched alert flags — inherent to the device.
 */
uint32_t
ina228_conversion_ready(ina228_context_t *ctx, uint8_t *ready);

/**
 * @brief DIAG_ALRT bits 11:0. Mask with ina228_alert_type_t values. When
 * ALATCH = 1 the read clears the latched flags.
 */
uint32_t
ina228_alert_functions(ina228_context_t *ctx, uint16_t *functions);

uint32_t
ina228_get_alert_latch(ina228_context_t *ctx, ina228_alert_latch_t *latch);

uint32_t
ina228_set_alert_latch(ina228_context_t *ctx, ina228_alert_latch_t latch);

uint32_t
ina228_get_alert_polarity(ina228_context_t *ctx, ina228_alert_polarity_t *polarity);

uint32_t
ina228_set_alert_polarity(ina228_context_t *ctx, ina228_alert_polarity_t polarity);

uint32_t
ina228_get_current_conversion_time(ina228_context_t *ctx, ina228_conversion_time_t *time);

uint32_t
ina228_set_current_conversion_time(ina228_context_t *ctx, ina228_conversion_time_t time);

uint32_t
ina228_get_voltage_conversion_time(ina228_context_t *ctx, ina228_conversion_time_t *time);

uint32_t
ina228_set_voltage_conversion_time(ina228_context_t *ctx, ina228_conversion_time_t time);

uint32_t
ina228_get_temperature_conversion_time(ina228_context_t *ctx, ina228_conversion_time_t *time);

uint32_t
ina228_set_temperature_conversion_time(ina228_context_t *ctx, ina228_conversion_time_t time);

uint32_t
ina228_get_averaging_count(ina228_context_t *ctx, ina228_avg_count_t *count);

uint32_t
ina228_set_averaging_count(ina228_context_t *ctx, ina228_avg_count_t count);

#ifdef __cplusplus
}
#endif

#endif // NSX_INA228_H
