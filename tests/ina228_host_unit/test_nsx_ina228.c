/**
 * Host unit tests for the INA228 driver register math.
 *
 * The driver is compiled against the stub nsx_i2c.h in this directory; the
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
 *   - ina228_validate() masks DEVICE_ID.REV_ID off before comparing the die
 *     id, so a real part reading 0x2281 is accepted.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "nsx_ina228.h"

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

// Transfer counters, for pinning a function's transaction shape.
static unsigned read_xfers = 0;
static unsigned write_xfers = 0;

uint32_t
nsx_i2c_write(nsx_i2c_config_t *cfg, const void *buf, uint32_t size, uint16_t addr)
{
    (void)cfg;
    (void)addr;
    const uint8_t *bytes = buf;
    // The driver only issues 16-bit register writes: [reg, msb, lsb].
    if (size != 3 || bytes[0] >= NUM_REGS) {
        return 1;
    }
    write_xfers++;
    if (drop_writes > 0) {
        drop_writes--;
        return 0;
    }
    fake_regs[bytes[0]][0] = bytes[1];
    fake_regs[bytes[0]][1] = bytes[2];
    return 0;
}

uint32_t
nsx_i2c_write_read(
    nsx_i2c_config_t *cfg, uint16_t addr, const void *writeBuf, size_t numWrite, void *readBuf,
    size_t numRead)
{
    (void)cfg;
    (void)addr;
    const uint8_t *reg = writeBuf;
    if (numWrite != 1 || reg[0] >= NUM_REGS || numRead > MAX_REG_BYTES) {
        return 1;
    }
    read_xfers++;
    if (fail_reads > 0) {
        fail_reads--;
        return 1;
    }
    memcpy(readBuf, fake_regs[reg[0]], numRead);
    return 0;
}

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
        if (fabsf(_a - _e) > (tol)) {                                                            \
            failures++;                                                                          \
            fprintf(stderr, "FAIL %s:%d: %s = %g, expected %g\n", __FILE__, __LINE__, #actual,   \
                    _a, _e);                                                                     \
        }                                                                                        \
    } while (0)

static nsx_i2c_config_t i2c_stub;

static ina228_context_t
fresh_device(void)
{
    ina228_context_t ctx;
    memset(fake_regs, 0, sizeof(fake_regs));
    fail_reads = 0;
    drop_writes = 0;
    // Power-on defaults for the registers whose reset value is not zero.
    set_reg16(REG_ADCCFG, ADCCFG_RESET);
    set_reg16(REG_SHUNTCAL, 0x1000);
    set_reg16(REG_DIAGALRT, 0x0001); // MEMSTAT = 1 (trim memory healthy)
    CHECK(ina228_init(&ctx, &i2c_stub, 0x40) == 0);
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
// bit always reads 0, so there is nothing to preserve), fully replacing the
// 0x1000 power-on value. The masked read-modify-write this replaced was
// observed to leave SHUNT_CAL at 0 on real hardware.
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

// A write that the bus accepts but the device never applies (the Apollo510B
// bring-up failure mode) must surface as an error, not as silently-zero
// current/power/energy readings.
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
// one readback. A read-modify-write of SHUNT_CAL (a third read) is the
// transaction sequence that corrupted the write on Apollo510B hardware.
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
// ina228_init defaults CURRENT_LSB to 0.0001 A.
static void
test_charge_positive(void)
{
    ina228_context_t ctx = fresh_device();
    set_reg40(REG_CHARGE, 100000);
    float charge = -1.0f;
    CHECK(ina228_read_charge(&ctx, &charge) == 0);
    CHECK_NEAR(charge, 10.0f, 1e-4f);
}

static void
test_charge_negative_sign_extends(void)
{
    ina228_context_t ctx = fresh_device();
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

// Joule conversion: 16 * 3.2 * CURRENT_LSB per LSB (init default LSB 0.0001).
static void
test_energy_float_conversion(void)
{
    ina228_context_t ctx = fresh_device();
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

// A failed read must report the error and yield a defined output value, not
// whatever was on the stack.
static void
test_failed_read_reports_error_and_zeroes_output(void)
{
    ina228_context_t ctx = fresh_device();
    set_reg16(REG_CONFIG, 1 << 4);

    uint16_t range = 0xFF;
    fail_reads = 1;
    CHECK(ina228_get_adc_range(&ctx, &range) != 0);
    CHECK(range == 0);

    // A register with a nonzero address (0x3E): composing the result from
    // the failed transfer's buffer would leak the address byte into the
    // output's high byte.
    set_reg16(REG_MFG_UID, 0x5449);
    uint16_t mfg = 0xAA;
    fail_reads = 1;
    CHECK(ina228_get_manufacturer_id(&ctx, &mfg) != 0);
    CHECK(mfg == 0);

    // And the same for a 40-bit read.
    set_reg40(REG_ENERGY, 12345);
    uint64_t energy = 0xFF;
    fail_reads = 1;
    CHECK(ina228_read_energy_raw(&ctx, &energy) != 0);
    CHECK(energy == 0);
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
    ina228_context_t ctx = fresh_device();
    set_reg16(REG_MFG_UID, 0x5449);
    set_reg16(REG_DVC_UID, 0x2281);
    CHECK(ina228_validate(&ctx) == 0);

    // Any silicon revision of the same die must pass.
    set_reg16(REG_DVC_UID, 0x2280);
    CHECK(ina228_validate(&ctx) == 0);
    set_reg16(REG_DVC_UID, 0x228F);
    CHECK(ina228_validate(&ctx) == 0);

    // The unshifted die id is not a valid DEVICE_ID and must be rejected.
    set_reg16(REG_DVC_UID, 0x0228);
    CHECK(ina228_validate(&ctx) == 1);

    // A different part must be rejected.
    set_reg16(REG_DVC_UID, 0x2261);
    CHECK(ina228_validate(&ctx) == 1);

    set_reg16(REG_DVC_UID, 0x2281);
    set_reg16(REG_MFG_UID, 0xDEAD);
    CHECK(ina228_validate(&ctx) == 1);
}

int
main(void)
{
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
    test_failed_read_reports_error_and_zeroes_output();
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
