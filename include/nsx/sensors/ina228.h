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

#ifndef NSX_SENSORS_INA228_H
#define NSX_SENSORS_INA228_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

#include <stdbool.h>
#include <nsx/sensors/bus.h>

/* Caller-owned context. Serialize whole driver operations per physical device.
 * The bus descriptor is copied; its callback/user storage must outlive ctx.
 * Fields prefixed with _ are private. No controller or device I/O during bind. */
typedef struct {
  nsx_sensor_bus_t bus;
  float _shunt_res;
  float _max_current;
  float _current_lsb;
  bool _bound;
  bool _calibrated;
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
  INA228_ALERT_POLARITY_NORMAL = 0x0, ///< Active low open-drain (Default)
  INA228_ALERT_POLARITY_INVERTED = 0x1, ///< Active high open-drain
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


typedef struct {
  float shunt_ohms;        /* Required: finite and positive. */
  float max_current_amps; /* Required: finite and positive. */
  uint16_t adc_range;     /* 0 = +/-163.84 mV; 1 = +/-40.96 mV. */
  ina228_conversion_time_t current_conversion_time;
  ina228_conversion_time_t voltage_conversion_time;
  ina228_conversion_time_t temperature_conversion_time;
  ina228_avg_count_t averaging_count;
  ina228_alert_latch_t alert_latch;
  ina228_alert_polarity_t alert_polarity;
} ina228_config_t;
typedef struct {
  uint16_t manufacturer_id;
  uint16_t device_id; /* DIEID in bits 15:4, silicon revision in bits 3:0. */
} ina228_identity_t;

/* Defaults match ADC power-on timing/averaging; shunt/current are zero and MUST be set
 * by the application. Validation is pure and checks calibration representability. */
ina228_config_t ina228_default_config(void);
nsx_sensor_status_t ina228_validate_config(const ina228_config_t *config);
/* I2C only; requires TX >= 3, RX >= 5. Failure leaves ctx unchanged. */
nsx_sensor_status_t ina228_bind(ina228_context_t *ctx, const nsx_sensor_bus_t *bus);
/* Reads identity only; no reset or configuration writes. Failure preserves id. */
nsx_sensor_status_t ina228_probe(ina228_context_t *ctx, ina228_identity_t *identity);
/* Validate, probe, stop conversions, apply explicit config/calibration, clear
 * accumulators and configure alerts. Success leaves the ADC in shutdown.
 * Start conversions explicitly with set_mode. No conversion wait. Disables shunt temperature compensation and conversion delay; leaves
 * alert thresholds unchanged. Does not issue a device reset. Failed I/O may
 * partially configure hardware and invalidates cached calibration; retry init.
 * Direct register APIs below are also usable after bind. Scaled current/power/
 * energy/charge require verified calibration (init or set_shunt). Raw counters,
 * voltage, temperature and diagnostics do not. All failed reads preserve output.
 * Units: current mA, bus/shunt voltage mV, power mW, temperature degC,
 * energy J, charge C. No read promises a fresh conversion or atomic snapshot. */
nsx_sensor_status_t ina228_init(ina228_context_t *ctx, const ina228_config_t *config);

nsx_sensor_status_t
ina228_get_manufacturer_id(ina228_context_t *ctx, uint16_t *value);

nsx_sensor_status_t
ina228_get_device_id(ina228_context_t *ctx, uint16_t *value);

/**
 * @brief Reset the device to power-on defaults (CONFIG.RST).
 *
 * This reverts SHUNT_CAL to its reset value, so ina228_set_shunt() must be
 * called again before scaled current, power, energy, or charge reads.
 * Calibration is invalidated before attempting the reset, even on bus failure.
 * This issues the reset command only; it does not wait for a new conversion.
 */
nsx_sensor_status_t
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
nsx_sensor_status_t
ina228_reset_accumulators(ina228_context_t *ctx);

/**
 * @brief Program SHUNT_CAL for a shunt resistance (ohms) and the maximum
 * expected current (amperes).
 *
 * Fails without writing the device or changing cached calibration if either
 * input is not finite and positive, or if the computed SHUNT_CAL exceeds the register's
 * 15-bit field (13107.2e6 * (max_current / 2^19) * shunt_res, x4 when
 * ADCRANGE = 1, must produce a nonzero value <= 32767) — in that case pick a smaller shunt or a
 * larger range. A write/readback failure invalidates cached calibration.
 * Stop conversions and reset accumulators when changing scaling for a new
 * accumulation window; set_shunt does not clear old accumulated history.
 */
nsx_sensor_status_t
ina228_set_shunt(ina228_context_t *ctx,  float shunt_res, float max_current);

/* Cached CURRENT_LSB in amperes per code, from the last verified calibration;
 * no I/O or hardware readback. E_STATE after bind/reset or failed calibration
 * programming/readback. Preflight rejection preserves prior valid calibration.
 * Output is unchanged on error. For raw accumulator deltas, use double
 * arithmetic: charge C = delta * lsb; energy J = delta * 16 * 3.2 * lsb.
 * Keep calibration/range fixed across the window and handle counter wrap.
 * This uses the same requested scale as the scaled reads, not a correction
 * for SHUNT_CAL integer quantization. */
nsx_sensor_status_t ina228_get_current_lsb(const ina228_context_t *ctx,
                                         float *amperes_per_lsb);

/**
 * @brief Select the shunt full-scale range (CONFIG.ADCRANGE): 0 = +/-163.84
 * mV, 1 = +/-40.96 mV.
 *
 * SHUNT_CAL depends on ADCRANGE, so if ina228_set_shunt() has already run,
 * this reprograms SHUNT_CAL for the new range. An unrepresentable calibration
 * is rejected before I/O. Any attempted range/calibration write failure
 * invalidates cached scaling; reinitialize or call set_shunt before scaled reads.
 */
nsx_sensor_status_t
ina228_set_adc_range(ina228_context_t *ctx, uint16_t adc_range);

nsx_sensor_status_t
ina228_get_adc_range(ina228_context_t *ctx, uint16_t *adc_range);

nsx_sensor_status_t
ina228_read_die_temp(ina228_context_t *ctx, float *temp);

nsx_sensor_status_t
ina228_read_current(ina228_context_t *ctx, float *current);

nsx_sensor_status_t
ina228_read_bus_voltage(ina228_context_t *ctx, float *bus_voltage);

nsx_sensor_status_t
ina228_read_shunt_voltage(ina228_context_t *ctx, float *shunt_voltage);

nsx_sensor_status_t
ina228_read_power(ina228_context_t *ctx, float *power);

/**
 * @brief Energy in joules. Scaled from the raw register through double, but
 * the float result still carries only 24 mantissa bits — for windowed
 * (delta) measurements subtract two ina228_read_energy_raw() readings
 * instead, which stay exact over the full 40-bit range.
 */
nsx_sensor_status_t
ina228_read_energy(ina228_context_t *ctx, float *energy);

/**
 * @brief Raw unsigned 40-bit ENERGY register, exact. Multiply by
 * 16 * 3.2 * CURRENT_LSB for joules. Reading clears DIAG_ALRT.ENERGYOF.
 */
nsx_sensor_status_t
ina228_read_energy_raw(ina228_context_t *ctx, uint64_t *energy);

/**
 * @brief Charge in coulombs. See ina228_read_energy() for float precision;
 * windowed measurements should subtract ina228_read_charge_raw() readings.
 */
nsx_sensor_status_t
ina228_read_charge(ina228_context_t *ctx, float *charge);

/**
 * @brief Raw sign-extended 40-bit CHARGE register, exact. Multiply by
 * CURRENT_LSB for coulombs. Reading clears DIAG_ALRT.CHARGEOF.
 */
nsx_sensor_status_t
ina228_read_charge_raw(ina228_context_t *ctx, int64_t *charge);

nsx_sensor_status_t
ina228_set_mode(ina228_context_t *ctx, ina228_meas_mode_t mode);

nsx_sensor_status_t
ina228_get_mode(ina228_context_t *ctx, ina228_meas_mode_t *mode);

/**
 * @brief Poll DIAG_ALRT.CNVRF. When ALATCH = 1 (latched mode), the read
 * itself clears CNVRF and the latched alert flags — inherent to the device.
 */
nsx_sensor_status_t
ina228_conversion_ready(ina228_context_t *ctx, uint8_t *ready);

/**
 * @brief DIAG_ALRT bits 11:0. Mask with ina228_alert_type_t values. When
 * ALATCH = 1 the read clears the latched flags.
 */
nsx_sensor_status_t
ina228_alert_functions(ina228_context_t *ctx, uint16_t *functions);

nsx_sensor_status_t
ina228_get_alert_latch(ina228_context_t *ctx, ina228_alert_latch_t *latch);

/* These setters read DIAG_ALRT to preserve other control bits; that read can
 * clear latched flags. Getters have the same read-to-clear side effects. */
nsx_sensor_status_t
ina228_set_alert_latch(ina228_context_t *ctx, ina228_alert_latch_t latch);

nsx_sensor_status_t
ina228_get_alert_polarity(ina228_context_t *ctx, ina228_alert_polarity_t *polarity);

nsx_sensor_status_t
ina228_set_alert_polarity(ina228_context_t *ctx, ina228_alert_polarity_t polarity);

nsx_sensor_status_t
ina228_get_current_conversion_time(ina228_context_t *ctx, ina228_conversion_time_t *time);

nsx_sensor_status_t
ina228_set_current_conversion_time(ina228_context_t *ctx, ina228_conversion_time_t time);

nsx_sensor_status_t
ina228_get_voltage_conversion_time(ina228_context_t *ctx, ina228_conversion_time_t *time);

nsx_sensor_status_t
ina228_set_voltage_conversion_time(ina228_context_t *ctx, ina228_conversion_time_t time);

nsx_sensor_status_t
ina228_get_temperature_conversion_time(ina228_context_t *ctx, ina228_conversion_time_t *time);

nsx_sensor_status_t
ina228_set_temperature_conversion_time(ina228_context_t *ctx, ina228_conversion_time_t time);

nsx_sensor_status_t
ina228_get_averaging_count(ina228_context_t *ctx, ina228_avg_count_t *count);

nsx_sensor_status_t
ina228_set_averaging_count(ina228_context_t *ctx, ina228_avg_count_t count);

#ifdef __cplusplus
}
#endif

#endif // NSX_SENSORS_INA228_H
