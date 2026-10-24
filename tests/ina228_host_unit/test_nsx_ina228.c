/**
 * Host unit tests for the INA228 driver register math.
 *
 * The driver uses the portable transfer callback; the
 * fake below emulates the INA228 register map as big-endian byte arrays, the
 * same wire format the device uses. These tests pin down:
 *
 *   - SHUNT_CAL programming per the TI INA228 datasheet (SLYS021):
 *     SHUNT_CAL = 13107.2e6 * CURRENT_LSB * R_shunt, multiplied by 4 when
 *     ADCRANGE = 1. A regression here previously multiplied by the raw
 *     ADCRANGE bit, writing SHUNT_CAL = 0 for ADCRANGE = 0.
 *   - CHARGE register decoding: 40-bit two's complement, so negative
 *     accumulated charge must sign-extend.
 *   - Which register each field actually lives in: ADCRANGE is CONFIG bit 4,
 *     and ADC_CONFIG bit 4 is the middle bit of VTCT.
 *   - Conversion-ready polling reads DIAG_ALRT.CNVRF (bit 1), not MEMSTAT
 *     (bit 0), which reads 1 on every healthy part.
 *   - ina228_probe() masks DEVICE_ID.REV_ID off before comparing the die
 *     id, so a real part reading 0x2281 is accepted.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include <nsx/sensors/ina228.h>

#define REG_CONFIG 0x00
#define REG_ADCCFG 0x01
#define REG_SHUNTCAL 0x02
#define REG_ENERGY 0x09
#define REG_CHARGE 0x0A
#define REG_DIAGALRT 0x0B
#define REG_MFG_UID 0x3E
#define REG_DVC_UID 0x3F

// ADC_CONFIG power-on value: MODE=Fh, VBUSCT=VSHCT=VTCT=5h, AVG=0h.
#define ADCCFG_RESET 0xFB68

#define NUM_REGS 0x40
#define MAX_REG_BYTES 5

static uint8_t fake_regs[NUM_REGS][MAX_REG_BYTES];

// Failure injection. fail_reads makes the next N write_read transfers return
// an error WITHOUT touching the read buffer (a failed transfer delivers no
// data). drop_writes makes the next N writes report success without storing
// anything, modeling a write that never lands on the device.
static int fail_reads = 0;
static int drop_writes = 0;
static int fail_write_nth, fail_read_nth;
static bool apply_failed_write;
static uint8_t (*active_regs)[MAX_REG_BYTES] = fake_regs;

// Transfer counters, for pinning a function's transaction shape.
static unsigned read_xfers = 0;
static unsigned write_xfers = 0;

static nsx_sensor_status_t fake_transfer(void *user, const nsx_sensor_transfer_t *t)
{
    uint8_t (*regs)[MAX_REG_BYTES] = user;
    if (!t->tx || t->tx_size < 1 || t->tx[0] >= NUM_REGS) return NSX_SENSOR_E_ARGUMENT;
    uint8_t reg = t->tx[0];
    if (t->rx_size) {
        if (t->tx_size != 1 || t->rx_size > MAX_REG_BYTES) return NSX_SENSOR_E_ARGUMENT;
        ++read_xfers;
        if ((fail_reads > 0 && --fail_reads >= 0) || (fail_read_nth > 0 && --fail_read_nth == 0)) {
            memset(t->rx, 0xa5, t->rx_size); /* Failed transport may partially fill scratch. */
            return NSX_SENSOR_E_IO;
        }
        memcpy(t->rx, regs[reg], t->rx_size);
        if (reg == REG_DIAGALRT && (regs[reg][0] & 0x80)) regs[reg][1] &= 1;
        if (reg == REG_ENERGY) regs[REG_DIAGALRT][0] &= ~8u;
        if (reg == REG_CHARGE) regs[REG_DIAGALRT][0] &= ~4u;
        return NSX_SENSOR_OK;
    }
    if (t->tx_size != 3) return NSX_SENSOR_E_ARGUMENT;
    ++write_xfers;
    bool failed = fail_write_nth > 0 && --fail_write_nth == 0;
    if (failed && !apply_failed_write) return NSX_SENSOR_E_IO;
    if (drop_writes > 0) { --drop_writes; return NSX_SENSOR_OK; }
    uint16_t value = (uint16_t)((t->tx[1] << 8) | t->tx[2]);
    if (reg == REG_CONFIG && (value & 0x8000)) {
        memset(regs, 0, REG_MFG_UID * MAX_REG_BYTES);
        regs[REG_SHUNTCAL][0] = 0x10;
        regs[REG_ADCCFG][0] = 0xfb; regs[REG_ADCCFG][1] = 0x68;
        regs[REG_DIAGALRT][1] = 1;
    } else if (reg == REG_DIAGALRT) {
        regs[reg][0] = (uint8_t)((regs[reg][0] & 0x0f) | (value >> 8 & 0xf0));
    } else {
        if (reg == REG_CONFIG && (value & 0x4000)) {
            memset(regs[REG_ENERGY], 0, 5); memset(regs[REG_CHARGE], 0, 5);
            regs[REG_DIAGALRT][0] &= ~2u;
            value &= ~0x4000u;
        }
        regs[reg][0] = (uint8_t)(value >> 8); regs[reg][1] = (uint8_t)value;
    }
    return failed ? NSX_SENSOR_E_IO : NSX_SENSOR_OK;
}
static const nsx_sensor_bus_ops_t fake_ops = { fake_transfer };

static void
set_reg16(uint8_t reg, uint16_t value)
{
    fake_regs[reg][0] = (uint8_t)(value >> 8);
    fake_regs[reg][1] = (uint8_t)(value & 0xFF);
}

static uint16_t
get_reg16(uint8_t reg)
{
    return (uint16_t)((fake_regs[reg][0] << 8) | fake_regs[reg][1]);
}

static void
set_reg40(uint8_t reg, uint64_t value)
{
    for (int i = 0; i < 5; i++) {
        fake_regs[reg][i] = (uint8_t)(value >> (8 * (4 - i)));
    }
}

static int failures = 0;

#define CHECK(cond)                                                                              \
    do {                                                                                         \
        if (!(cond)) {                                                                           \
            failures++;                                                                          \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                      \
        }                                                                                        \
    } while (0)

#define CHECK_NEAR(actual, expected, tol)                                                        \
    do {                                                                                         \
        float _a = (actual);                                                                     \
        float _e = (expected);                                                                   \
        if (!isfinite(_a) || fabsf(_a - _e) > (tol)) {                                                            \
            failures++;                                                                          \
            fprintf(stderr, "FAIL %s:%d: %s = %g, expected %g\n", __FILE__, __LINE__, #actual,   \
                    _a, _e);                                                                     \
        }                                                                                        \
    } while (0)



static ina228_context_t
fresh_device(void)
{
    ina228_context_t ctx;
    memset(fake_regs, 0, sizeof(fake_regs));
    fail_reads = 0;
    drop_writes = 0;
    fail_write_nth = fail_read_nth = 0; apply_failed_write = false;
    active_regs = fake_regs;
    // Power-on defaults for the registers whose reset value is not zero.
    set_reg16(REG_ADCCFG, ADCCFG_RESET);
    set_reg16(REG_SHUNTCAL, 0x1000);
    set_reg16(REG_DIAGALRT, 0x0001); // MEMSTAT = 1 (trim memory healthy)
    set_reg16(REG_MFG_UID, 0x5449); set_reg16(REG_DVC_UID, 0x2281);
    nsx_sensor_bus_t bus = { &fake_ops, active_regs, NSX_SENSOR_BUS_I2C, 3, 5 };
    CHECK(ina228_bind(&ctx, &bus) == 0);
    return ctx;
}

// SHUNT_CAL = 13107.2e6 * CURRENT_LSB * R_shunt with ADCRANGE = 0.
// R_shunt = 0.1 ohm, max current 0.5 A -> CURRENT_LSB = 0.5 / 2^19,
// so SHUNT_CAL = 13107.2e6 * 0.1 * 0.5 / 2^19 = 1250 exactly.
static void
test_shunt_cal_adcrange_0(void)
{
    ina228_context_t ctx = fresh_device();
    CHECK(ina228_set_shunt(&ctx, 0.1f, 0.5f) == 0);
    CHECK(get_reg16(REG_SHUNTCAL) == 1250);
}

// Same operating point with ADCRANGE = 1 must program 4x the value.
// ADCRANGE lives in CONFIG bit 4, not ADC_CONFIG bit 4.
static void
test_shunt_cal_adcrange_1(void)
{
    ina228_context_t ctx = fresh_device();
    set_reg16(REG_CONFIG, 1 << 4);
    CHECK(ina228_set_shunt(&ctx, 0.1f, 0.5f) == 0);
    CHECK(get_reg16(REG_SHUNTCAL) == 5000);
}

// A stray bit 4 in ADC_CONFIG is the middle bit of VTCT, not the shunt range,
// and must not change the calibration.
static void
test_shunt_cal_ignores_adc_config_bit_4(void)
{
    ina228_context_t ctx = fresh_device();
    set_reg16(REG_ADCCFG, ADCCFG_RESET | (1 << 4));
    CHECK(ina228_set_shunt(&ctx, 0.1f, 0.5f) == 0);
    CHECK(get_reg16(REG_SHUNTCAL) == 1250);
}

// set_shunt writes the full register in one plain transfer (the reserved top
// bit always reads 0, so a read-modify-write would preserve nothing), fully
// replacing the 0x1000 power-on value.
static void
test_shunt_cal_plain_full_write(void)
{
    ina228_context_t ctx = fresh_device();
    CHECK(get_reg16(REG_SHUNTCAL) == 0x1000);
    CHECK(ina228_set_shunt(&ctx, 0.1f, 0.5f) == 0);
    CHECK(get_reg16(REG_SHUNTCAL) == 1250);
}

// Non-positive inputs must be rejected without touching the device.
static void
test_shunt_cal_rejects_bad_inputs(void)
{
    ina228_context_t ctx = fresh_device();
    CHECK(ina228_set_shunt(&ctx, 0.0f, 0.5f) != 0);
    CHECK(ina228_set_shunt(&ctx, 0.1f, 0.0f) != 0);
    CHECK(ina228_set_shunt(&ctx, -0.1f, 0.5f) != 0);
    CHECK(get_reg16(REG_SHUNTCAL) == 0x1000);
}

// SHUNT_CAL is a 15-bit field. R = 1 ohm, Imax = 2 A computes 50000, which
// previously masked down to 17232 -- a silent 2.9x miscalibration. It must be
// an error that leaves the register untouched.
static void
test_shunt_cal_rejects_out_of_range(void)
{
    ina228_context_t ctx = fresh_device();
    CHECK(ina228_set_shunt(&ctx, 1.0f, 2.0f) != 0);
    CHECK(get_reg16(REG_SHUNTCAL) == 0x1000);
    // And the driver must not consider itself calibrated afterwards.
    CHECK(ctx._calibrated == 0);
}

// A write that the bus accepts but the device never applies must surface as
// an error, not as silently-zero current/power/energy readings: SHUNT_CAL is
// the one register where a lost write zeroes every derived measurement.
static void
test_shunt_cal_readback_verify(void)
{
    ina228_context_t ctx = fresh_device();
    drop_writes = 1;
    CHECK(ina228_set_shunt(&ctx, 0.1f, 0.5f) != 0);
    CHECK(ctx._calibrated == 0);
    CHECK(get_reg16(REG_SHUNTCAL) == 0x1000);
}

// set_shunt must be exactly: one ADCRANGE read, one plain SHUNT_CAL write,
// one readback. Pins the design: no read-modify-write of SHUNT_CAL (there is
// nothing to preserve) and no unverified write.
static void
test_set_shunt_transaction_shape(void)
{
    ina228_context_t ctx = fresh_device();
    read_xfers = 0;
    write_xfers = 0;
    CHECK(ina228_set_shunt(&ctx, 0.1f, 0.5f) == 0);
    CHECK(read_xfers == 2);
    CHECK(write_xfers == 1);
}

// CHARGE is 40-bit two's complement scaled by CURRENT_LSB.
// Test calibration sets CURRENT_LSB to 0.0001 A.
static void
test_charge_positive(void)
{
    ina228_context_t ctx = fresh_device();
    CHECK(ina228_set_shunt(&ctx, 0.001f, 52.4288f) == NSX_SENSOR_OK);
    set_reg40(REG_CHARGE, 100000);
    float charge = -1.0f;
    CHECK(ina228_read_charge(&ctx, &charge) == 0);
    CHECK_NEAR(charge, 10.0f, 1e-4f);
}

static void
test_charge_negative_sign_extends(void)
{
    ina228_context_t ctx = fresh_device();
    CHECK(ina228_set_shunt(&ctx, 0.001f, 52.4288f) == NSX_SENSOR_OK);
    // -3 as 40-bit two's complement
    set_reg40(REG_CHARGE, 0xFFFFFFFFFDULL);
    float charge = 0.0f;
    CHECK(ina228_read_charge(&ctx, &charge) == 0);
    CHECK_NEAR(charge, -3.0f * 0.0001f, 1e-7f);

    // Most negative 40-bit value: -2^39
    set_reg40(REG_CHARGE, 0x8000000000ULL);
    CHECK(ina228_read_charge(&ctx, &charge) == 0);
    CHECK_NEAR(charge, -549755813888.0f * 0.0001f, 1e4f);
}

// ADCRANGE must be written to CONFIG bit 4, leaving ADC_CONFIG alone. Writing
// it to ADC_CONFIG bit 4 would silently retune VTCT from 1052 us to 4120 us.
static void
test_adc_range_uses_the_config_register(void)
{
    ina228_context_t ctx = fresh_device();
    ina228_conversion_time_t vtct;

    CHECK(ina228_set_adc_range(&ctx, 1) == 0);
    CHECK(get_reg16(REG_CONFIG) == (1 << 4));
    CHECK(get_reg16(REG_ADCCFG) == ADCCFG_RESET);
    CHECK(ina228_get_temperature_conversion_time(&ctx, &vtct) == 0);
    CHECK(vtct == INA228_TIME_1052_us);

    uint16_t range = 0;
    CHECK(ina228_get_adc_range(&ctx, &range) == 0);
    CHECK(range == 1);

    CHECK(ina228_set_adc_range(&ctx, 0) == 0);
    CHECK(ina228_get_adc_range(&ctx, &range) == 0);
    CHECK(range == 0);
}

// Changing the range after calibration must reprogram SHUNT_CAL for the new
// range (the x4 factor), in both directions.
static void
test_adc_range_recalibrates(void)
{
    ina228_context_t ctx = fresh_device();
    CHECK(ina228_set_shunt(&ctx, 0.1f, 0.5f) == 0);
    CHECK(get_reg16(REG_SHUNTCAL) == 1250);

    CHECK(ina228_set_adc_range(&ctx, 1) == 0);
    CHECK(get_reg16(REG_SHUNTCAL) == 5000);

    CHECK(ina228_set_adc_range(&ctx, 0) == 0);
    CHECK(get_reg16(REG_SHUNTCAL) == 1250);
}

// Before any calibration there is nothing to reprogram: the power-on
// SHUNT_CAL must be left alone.
static void
test_adc_range_before_calibration_leaves_shunt_cal(void)
{
    ina228_context_t ctx = fresh_device();
    CHECK(ina228_set_adc_range(&ctx, 1) == 0);
    CHECK(get_reg16(REG_SHUNTCAL) == 0x1000);
}

// If the x4 recalibration would overflow the 15-bit field, the range switch
// must report the failure and leave the old calibration value in place.
static void
test_adc_range_recalibration_overflow_is_an_error(void)
{
    ina228_context_t ctx = fresh_device();
    // 12500 fits; 4 x 12500 = 50000 does not.
    CHECK(ina228_set_shunt(&ctx, 0.1f, 5.0f) == 0);
    CHECK(get_reg16(REG_SHUNTCAL) == 12500);

    CHECK(ina228_set_adc_range(&ctx, 1) != 0);
    CHECK(get_reg16(REG_SHUNTCAL) == 12500);
}

// The raw 40-bit accessors must be exact over the full register range; a
// float path quantizes to ~512-LSB steps near 2^32, which corrupts windowed
// (delta) energy measurements.
static void
test_energy_raw_is_exact(void)
{
    ina228_context_t ctx = fresh_device();
    uint64_t begin, end;

    set_reg40(REG_ENERGY, (1ULL << 32));
    CHECK(ina228_read_energy_raw(&ctx, &begin) == 0);
    set_reg40(REG_ENERGY, (1ULL << 32) + 1000);
    CHECK(ina228_read_energy_raw(&ctx, &end) == 0);
    CHECK(begin == (1ULL << 32));
    CHECK(end - begin == 1000);

    set_reg40(REG_ENERGY, (1ULL << 40) - 1);
    CHECK(ina228_read_energy_raw(&ctx, &end) == 0);
    CHECK(end == (1ULL << 40) - 1);
}

// Joule conversion: 16 * 3.2 * CURRENT_LSB per LSB (calibrated LSB 0.0001).
static void
test_energy_float_conversion(void)
{
    ina228_context_t ctx = fresh_device();
    CHECK(ina228_set_shunt(&ctx, 0.001f, 52.4288f) == NSX_SENSOR_OK);
    set_reg40(REG_ENERGY, 100000);
    float energy = -1.0f;
    CHECK(ina228_read_energy(&ctx, &energy) == 0);
    CHECK_NEAR(energy, 512.0f, 1e-3f);
}

static void
test_charge_raw_sign_extends(void)
{
    ina228_context_t ctx = fresh_device();
    int64_t charge;

    set_reg40(REG_CHARGE, 0xFFFFFFFFFDULL); // -3
    CHECK(ina228_read_charge_raw(&ctx, &charge) == 0);
    CHECK(charge == -3);

    set_reg40(REG_CHARGE, 100000);
    CHECK(ina228_read_charge_raw(&ctx, &charge) == 0);
    CHECK(charge == 100000);
}

// A setter whose read-modify-write read fails must not write anything: a
// garbage RMW would silently reconfigure the device.
static void
test_write_bits_never_writes_after_a_failed_read(void)
{
    ina228_context_t ctx = fresh_device();
    fail_reads = 1;
    CHECK(ina228_set_mode(&ctx, INA228_MODE_SHUTDOWN) != 0);
    CHECK(get_reg16(REG_ADCCFG) == ADCCFG_RESET);
}

// A failed read preserves the caller output, even if transport fills scratch.
static void
test_failed_read_preserves_output(void)
{
    ina228_context_t ctx = fresh_device();
    set_reg16(REG_CONFIG, 1 << 4);

    uint16_t range = 0xFF;
    fail_reads = 1;
    CHECK(ina228_get_adc_range(&ctx, &range) != 0);
    CHECK(range == 0xFF);

    // A register with a nonzero address (0x3E): composing the result from
    // the failed transfer's buffer would leak the address byte into the
    // output's high byte.
    set_reg16(REG_MFG_UID, 0x5449);
    uint16_t mfg = 0xAA;
    fail_reads = 1;
    CHECK(ina228_get_manufacturer_id(&ctx, &mfg) != 0);
    CHECK(mfg == 0xAA);

    // And the same for a 40-bit read.
    set_reg40(REG_ENERGY, 12345);
    uint64_t energy = 0xFF;
    fail_reads = 1;
    CHECK(ina228_read_energy_raw(&ctx, &energy) != 0);
    CHECK(energy == 0xFF);
}

// The alert masks must match the DIAG_ALRT bit map (Table 7-16). The old
// values were all one bit position too low, starting from MEMSTAT.
static void
test_alert_masks_match_diag_alrt_bits(void)
{
    CHECK(INA228_ALERT_CONVERSION_READY == (1 << 1)); // CNVRF
    CHECK(INA228_ALERT_OVERPOWER == (1 << 2));        // POL
    CHECK(INA228_ALERT_UNDERVOLTAGE == (1 << 3));     // BUSUL
    CHECK(INA228_ALERT_OVERVOLTAGE == (1 << 4));      // BUSOL
    CHECK(INA228_ALERT_UNDERCURRENT == (1 << 5));     // SHNTUL
    CHECK(INA228_ALERT_OVERCURRENT == (1 << 6));      // SHNTOL
    CHECK(INA228_ALERT_OVERTEMP == (1 << 7));         // TMPOL
    CHECK(INA228_ALERT_MATH_OVERFLOW == (1 << 9));    // MATHOF
    CHECK(INA228_ALERT_CHARGE_OVERFLOW == (1 << 10)); // CHARGEOF
    CHECK(INA228_ALERT_ENERGY_OVERFLOW == (1 << 11)); // ENERGYOF

    // And they line up with what ina228_alert_functions() returns.
    ina228_context_t ctx = fresh_device();
    uint16_t functions;
    set_reg16(REG_DIAGALRT, 0x0001 | (1 << 9) | (1 << 1)); // MEMSTAT|MATHOF|CNVRF
    CHECK(ina228_alert_functions(&ctx, &functions) == 0);
    CHECK((functions & INA228_ALERT_MATH_OVERFLOW) != 0);
    CHECK((functions & INA228_ALERT_CONVERSION_READY) != 0);
    CHECK((functions & INA228_ALERT_ENERGY_OVERFLOW) == 0);
}

// CNVRF is DIAG_ALRT bit 1; bit 0 is MEMSTAT, which reads 1 on a healthy part.
static void
test_conversion_ready_reads_cnvrf(void)
{
    ina228_context_t ctx = fresh_device();
    uint8_t ready = 0xFF;

    // Only MEMSTAT set: no conversion has completed yet.
    CHECK(ina228_conversion_ready(&ctx, &ready) == 0);
    CHECK(ready == 0);

    set_reg16(REG_DIAGALRT, 0x0003); // MEMSTAT | CNVRF
    CHECK(ina228_conversion_ready(&ctx, &ready) == 0);
    CHECK(ready == 1);
}

// DEVICE_ID is DIEID in bits 15:4 plus REV_ID in bits 3:0, so a real part
// reads 0x2281 and the revision must be masked off before comparing.
static void
test_validate(void)
{
    ina228_identity_t identity;
    ina228_context_t ctx = fresh_device();
    set_reg16(REG_MFG_UID, 0x5449);
    set_reg16(REG_DVC_UID, 0x2281);
    CHECK(ina228_probe(&ctx, &identity) == 0);

    // Any silicon revision of the same die must pass.
    set_reg16(REG_DVC_UID, 0x2280);
    CHECK(ina228_probe(&ctx, &identity) == 0);
    set_reg16(REG_DVC_UID, 0x228F);
    CHECK(ina228_probe(&ctx, &identity) == 0);

    // The unshifted die id is not a valid DEVICE_ID and must be rejected.
    set_reg16(REG_DVC_UID, 0x0228);
    CHECK(ina228_probe(&ctx, &identity) == NSX_SENSOR_E_DEVICE);

    // A different part must be rejected.
    set_reg16(REG_DVC_UID, 0x2261);
    CHECK(ina228_probe(&ctx, &identity) == NSX_SENSOR_E_DEVICE);

    set_reg16(REG_DVC_UID, 0x2281);
    set_reg16(REG_MFG_UID, 0xDEAD);
    CHECK(ina228_probe(&ctx, &identity) == NSX_SENSOR_E_DEVICE);
}

static ina228_config_t valid_config(void)
{
    ina228_config_t config = ina228_default_config();
    config.shunt_ohms = 0.1f;
    config.max_current_amps = 0.5f;
    return config;
}
static void test_lifecycle(void)
{
    ina228_context_t ctx = fresh_device();
    unsigned reads = read_xfers, writes = write_xfers;
    nsx_sensor_bus_t bus = ctx.bus;
    ina228_context_t copy = ctx;
    bus.kind = NSX_SENSOR_BUS_SPI;
    CHECK(ina228_bind(&ctx, &bus) == NSX_SENSOR_E_UNSUPPORTED);
    CHECK(memcmp(&ctx, &copy, sizeof(ctx)) == 0);
    bus = ctx.bus; bus.max_rx_size = 4;
    CHECK(ina228_bind(&ctx, &bus) == NSX_SENSOR_E_UNSUPPORTED);
    bus = ctx.bus; bus.max_tx_size = 2;
    CHECK(ina228_bind(&ctx, &bus) == NSX_SENSOR_E_UNSUPPORTED);
    CHECK(ina228_bind(&ctx, NULL) == NSX_SENSOR_E_ARGUMENT);
    ina228_config_t cfg = ina228_default_config();
    CHECK(ina228_validate_config(&cfg) == NSX_SENSOR_E_ARGUMENT);
    CHECK(ina228_validate_config(NULL) == NSX_SENSOR_E_ARGUMENT);
    CHECK(ina228_init(&ctx, &cfg) == NSX_SENSOR_E_ARGUMENT);
    CHECK(read_xfers == reads && write_xfers == writes);
    cfg = valid_config();
    CHECK(ina228_init(&ctx, &cfg) == NSX_SENSOR_OK);
    CHECK(get_reg16(REG_CONFIG) == 0);
    CHECK(get_reg16(REG_ADCCFG) == (ADCCFG_RESET & 0x0fff));
    CHECK(get_reg16(REG_SHUNTCAL) == 1250 && ctx._calibrated);
    cfg.adc_range=1;
    cfg.current_conversion_time=INA228_TIME_150_us;
    cfg.voltage_conversion_time=INA228_TIME_84_us;
    cfg.temperature_conversion_time=INA228_TIME_50_us;
    cfg.averaging_count=INA228_COUNT_16;
    cfg.alert_latch=INA228_ALERT_LATCH_ENABLED;
    cfg.alert_polarity=INA228_ALERT_POLARITY_INVERTED;
    CHECK(ina228_init(&ctx, &cfg) == NSX_SENSOR_OK);
    CHECK(get_reg16(REG_ADCCFG) == 0x0282);
    CHECK(ina228_set_mode(&ctx,INA228_MODE_TRIGGERED)==NSX_SENSOR_OK);
    CHECK(get_reg16(REG_ADCCFG) == 0x7282);
    CHECK(ina228_init(&ctx,&cfg)==NSX_SENSOR_OK);
    CHECK(get_reg16(REG_ADCCFG) == 0x0282);
    CHECK(ina228_set_mode(&ctx,INA228_MODE_CONTINUOUS)==NSX_SENSOR_OK);
    CHECK(get_reg16(REG_ADCCFG) == 0xf282);
    CHECK(get_reg16(REG_CONFIG) == 0x10 && get_reg16(REG_SHUNTCAL) == 5000);
    CHECK((get_reg16(REG_DIAGALRT) & 0xf000) == 0x9000);
    CHECK(ina228_reset(&ctx) == NSX_SENSOR_OK && !ctx._calibrated);
    float value = 123;
    CHECK(ina228_read_current(&ctx,&value) == NSX_SENSOR_E_STATE && value==123);
    CHECK(ina228_init(&ctx,&cfg) == NSX_SENSOR_OK && ctx._calibrated);
    ina228_context_t unbound={0};
    CHECK(ina228_read_bus_voltage(&unbound,&value)==NSX_SENSOR_E_STATE && value==123);
    CHECK(ina228_read_bus_voltage(NULL,&value)==NSX_SENSOR_E_STATE);
    CHECK(ina228_read_bus_voltage(&ctx,NULL)==NSX_SENSOR_E_ARGUMENT);
}
static void test_invalid_config_and_calibration(void)
{
    ina228_context_t ctx=fresh_device();
    CHECK(ina228_set_shunt(&ctx,0.1f,0.5f)==NSX_SENSOR_OK);
    unsigned reads=read_xfers, writes=write_xfers;
    const float bad[] = {0, -1, NAN, INFINITY};
    for(size_t i=0;i<sizeof(bad)/sizeof(bad[0]);++i) {
        ina228_config_t cfg=valid_config(); cfg.shunt_ohms=bad[i];
        CHECK(ina228_validate_config(&cfg)==NSX_SENSOR_E_ARGUMENT);
        CHECK(ina228_set_shunt(&ctx,bad[i],0.5f)==NSX_SENSOR_E_ARGUMENT);
        cfg=valid_config(); cfg.max_current_amps=bad[i];
        CHECK(ina228_validate_config(&cfg)==NSX_SENSOR_E_ARGUMENT);
        CHECK(ina228_set_shunt(&ctx,0.1f,bad[i])==NSX_SENSOR_E_ARGUMENT);
    }
    CHECK(ina228_set_adc_range(&ctx,2)==NSX_SENSOR_E_ARGUMENT);
    CHECK(ina228_set_mode(&ctx,(ina228_meas_mode_t)-1)==NSX_SENSOR_E_ARGUMENT);
    CHECK(ina228_set_mode(&ctx,(ina228_meas_mode_t)16)==NSX_SENSOR_E_ARGUMENT);
    CHECK(ina228_set_averaging_count(&ctx,(ina228_avg_count_t)8)==NSX_SENSOR_E_ARGUMENT);
    CHECK(ina228_set_current_conversion_time(&ctx,(ina228_conversion_time_t)8)==NSX_SENSOR_E_ARGUMENT);
    CHECK(ina228_set_alert_latch(&ctx,(ina228_alert_latch_t)2)==NSX_SENSOR_E_ARGUMENT);
    CHECK(ina228_set_alert_polarity(&ctx,(ina228_alert_polarity_t)-1)==NSX_SENSOR_E_ARGUMENT);
    CHECK(read_xfers==reads && write_xfers==writes && ctx._calibrated);
    ina228_config_t cfg=valid_config(); cfg.max_current_amps=1e-20f;
    CHECK(ina228_validate_config(&cfg)==NSX_SENSOR_E_CALIBRATION);
    CHECK(ina228_set_shunt(&ctx,0.1f,1e-20f)==NSX_SENSOR_E_CALIBRATION);
    CHECK(ctx._calibrated && write_xfers==writes); /* rejected before any write */
}
static void test_calibration_failure_invalidation(void)
{
    ina228_context_t ctx=fresh_device();
    CHECK(ina228_set_shunt(&ctx,0.1f,0.5f)==NSX_SENSOR_OK);
    fail_read_nth=2; /* range read succeeds, new calibration lands, verification fails */
    CHECK(ina228_set_shunt(&ctx,0.1f,0.4f)==NSX_SENSOR_E_IO);
    CHECK(!ctx._calibrated && get_reg16(REG_SHUNTCAL)==1000);
    float value=19;
    CHECK(ina228_read_power(&ctx,&value)==NSX_SENSOR_E_STATE && value==19);
    CHECK(ina228_set_shunt(&ctx,0.1f,0.5f)==NSX_SENSOR_OK);
    fail_write_nth=1; apply_failed_write=true;
    CHECK(ina228_set_adc_range(&ctx,1)==NSX_SENSOR_E_IO);
    CHECK(!ctx._calibrated && get_reg16(REG_CONFIG)==0x10);
    CHECK(ina228_set_shunt(&ctx,0.1f,0.5f)==NSX_SENSOR_OK);
    fail_write_nth=2; /* range succeeds, calibration write fails */
    CHECK(ina228_set_adc_range(&ctx,0)==NSX_SENSOR_E_IO && !ctx._calibrated);
    CHECK(ina228_set_shunt(&ctx,0.1f,0.5f)==NSX_SENSOR_OK);
    fail_write_nth=1;
    CHECK(ina228_reset(&ctx)==NSX_SENSOR_E_IO && !ctx._calibrated);
    /* Failure at every init read/write leaves scaled reads disabled. */
    for(int phase=1;phase<=5;++phase) {
        ctx=fresh_device(); ina228_config_t cfg=valid_config();
        CHECK(ina228_set_shunt(&ctx,0.1f,0.5f)==NSX_SENSOR_OK);
        fail_write_nth=phase;
        CHECK(ina228_init(&ctx,&cfg)==NSX_SENSOR_E_IO && !ctx._calibrated);
        fail_write_nth=0;
        CHECK(ina228_init(&ctx,&cfg)==NSX_SENSOR_OK && ctx._calibrated);
    }
    for(int phase=1;phase<=5;++phase) {
        ctx=fresh_device(); ina228_config_t cfg=valid_config(); fail_read_nth=phase;
        CHECK(ina228_init(&ctx,&cfg)==NSX_SENSOR_E_IO && !ctx._calibrated);
    }
}
static void test_all_float_failures_and_signed_measurements(void)
{
    ina228_context_t ctx=fresh_device();
    CHECK(ina228_set_shunt(&ctx,0.001f,52.4288f)==NSX_SENSOR_OK);
    typedef nsx_sensor_status_t (*reader_t)(ina228_context_t *,float *);
    reader_t readers[]={ina228_read_die_temp,ina228_read_current,ina228_read_bus_voltage,
        ina228_read_shunt_voltage,ina228_read_power,ina228_read_energy,ina228_read_charge};
    for(size_t i=0;i<sizeof(readers)/sizeof(readers[0]);++i) {
        float value=17; fail_reads=1;
        CHECK(readers[i](&ctx,&value)==NSX_SENSOR_E_IO && value==17);
        CHECK(readers[i](&ctx,NULL)==NSX_SENSOR_E_ARGUMENT);
    }
    set_reg16(6,0xff80); float value;
    CHECK(ina228_read_die_temp(&ctx,&value)==NSX_SENSOR_OK); CHECK_NEAR(value,-1,1e-7f);
    /* CURRENT/VSHUNT are signed 20-bit left justified. Low nibble is undefined. */
    fake_regs[7][0]=0xff; fake_regs[7][1]=0xff; fake_regs[7][2]=0xff;
    CHECK(ina228_read_current(&ctx,&value)==NSX_SENSOR_OK); CHECK_NEAR(value,-0.1f,1e-7f);
    fake_regs[4][0]=0xff; fake_regs[4][1]=0xff; fake_regs[4][2]=0xff;
    CHECK(ina228_read_shunt_voltage(&ctx,&value)==NSX_SENSOR_OK); CHECK_NEAR(value,-0.0003125f,1e-9f);
    fake_regs[7][0]=0x80; fake_regs[7][1]=0; fake_regs[7][2]=0x0f;
    CHECK(ina228_read_current(&ctx,&value)==NSX_SENSOR_OK); CHECK_NEAR(value,-52428.8f,0.01f);
}
static void test_side_effects_and_two_instances(void)
{
    ina228_context_t first=fresh_device(), second;
    uint8_t second_regs[NUM_REGS][MAX_REG_BYTES]={{0}};
    nsx_sensor_bus_t other={&fake_ops,second_regs,NSX_SENSOR_BUS_I2C,3,5};
    CHECK(ina228_bind(&second,&other)==NSX_SENSOR_OK);
    CHECK(ina228_set_shunt(&first,0.1f,0.5f)==NSX_SENSOR_OK);
    CHECK(ina228_set_shunt(&second,0.05f,0.1f)==NSX_SENSOR_OK);
    CHECK(get_reg16(REG_SHUNTCAL)==1250);
    CHECK(((second_regs[REG_SHUNTCAL][0]<<8)|second_regs[REG_SHUNTCAL][1])==125);
    CHECK(first._current_lsb!=second._current_lsb);
    set_reg16(REG_DIAGALRT,0x8e83); /* latched, energy/charge/math overflow + temp/ready */
    uint16_t flags=0;
    CHECK(ina228_alert_functions(&first,&flags)==NSX_SENSOR_OK && flags==0xe83);
    CHECK((get_reg16(REG_DIAGALRT)&0xff)==1);
    set_reg40(REG_ENERGY,9); set_reg40(REG_CHARGE,7);
    CHECK(ina228_reset_accumulators(&first)==NSX_SENSOR_OK && first._calibrated);
    CHECK((get_reg16(REG_DIAGALRT)&0xe00)==0xc00); /* reset doesn't clear accumulator overflow */
    uint64_t energy; int64_t charge;
    CHECK(ina228_read_energy_raw(&first,&energy)==NSX_SENSOR_OK && energy==0);
    CHECK(ina228_read_charge_raw(&first,&charge)==NSX_SENSOR_OK && charge==0);
    CHECK((get_reg16(REG_DIAGALRT)&0xc00)==0);
    set_reg16(REG_DIAGALRT,0xe083);
    CHECK(ina228_set_alert_polarity(&first,INA228_ALERT_POLARITY_INVERTED)==NSX_SENSOR_OK);
    CHECK(get_reg16(REG_DIAGALRT)==0xf001); /* preserves controls, read clears flags */
    CHECK(ina228_reset(&first)==NSX_SENSOR_OK && !first._calibrated && second._calibrated);
}

static void test_identity_and_counter_failures(void)
{
    ina228_context_t ctx=fresh_device();
    ina228_identity_t id={0xaaaa,0xbbbb};
    fail_read_nth=2;
    CHECK(ina228_probe(&ctx,&id)==NSX_SENSOR_E_IO);
    CHECK(id.manufacturer_id==0xaaaa && id.device_id==0xbbbb);
    int64_t charge=42; fail_reads=1;
    CHECK(ina228_read_charge_raw(&ctx,&charge)==NSX_SENSOR_E_IO && charge==42);
    set_reg40(REG_CHARGE,UINT64_C(0x8000000000));
    CHECK(ina228_read_charge_raw(&ctx,&charge)==NSX_SENSOR_OK && charge==-INT64_C(549755813888));
    set_reg40(REG_CHARGE,UINT64_C(0x7fffffffff));
    CHECK(ina228_read_charge_raw(&ctx,&charge)==NSX_SENSOR_OK && charge==INT64_C(549755813887));
    ina228_meas_mode_t mode=INA228_MODE_TRIGGERED; fail_reads=1;
    CHECK(ina228_get_mode(&ctx,&mode)==NSX_SENSOR_E_IO && mode==INA228_MODE_TRIGGERED);
    uint8_t ready=0xaa; fail_reads=1;
    CHECK(ina228_conversion_ready(&ctx,&ready)==NSX_SENSOR_E_IO && ready==0xaa);
    /* A representable calibration can still overflow a float output at an
     * extreme scale. Do not return success with infinity. */
    CHECK(ina228_set_shunt(&ctx,1e-35f,1e35f)==NSX_SENSOR_OK);
    fake_regs[8][0]=0xff; fake_regs[8][1]=0xff; fake_regs[8][2]=0xff;
    float power=42;
    CHECK(ina228_read_power(&ctx,&power)==NSX_SENSOR_E_OVERFLOW && power==42);
}

int
main(void)
{
    test_identity_and_counter_failures();
    test_lifecycle();
    test_invalid_config_and_calibration();
    test_calibration_failure_invalidation();
    test_all_float_failures_and_signed_measurements();
    test_side_effects_and_two_instances();
    test_shunt_cal_adcrange_0();
    test_shunt_cal_adcrange_1();
    test_shunt_cal_ignores_adc_config_bit_4();
    test_shunt_cal_plain_full_write();
    test_shunt_cal_rejects_bad_inputs();
    test_shunt_cal_rejects_out_of_range();
    test_shunt_cal_readback_verify();
    test_set_shunt_transaction_shape();
    test_charge_positive();
    test_charge_negative_sign_extends();
    test_charge_raw_sign_extends();
    test_energy_raw_is_exact();
    test_energy_float_conversion();
    test_adc_range_uses_the_config_register();
    test_adc_range_recalibrates();
    test_adc_range_before_calibration_leaves_shunt_cal();
    test_adc_range_recalibration_overflow_is_an_error();
    test_write_bits_never_writes_after_a_failed_read();
    test_failed_read_preserves_output();
    test_alert_masks_match_diag_alrt_bits();
    test_conversion_ready_reads_cnvrf();
    test_validate();

    if (failures) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    printf("ina228 host unit tests passed\n");
    return 0;
}
