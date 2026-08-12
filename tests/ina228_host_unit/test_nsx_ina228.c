/**
 * Host unit tests for the INA228 driver register math.
 *
 * The driver is compiled against the stub nsx_i2c.h in this directory; the
 * fake below emulates the INA228 register map as big-endian byte arrays, the
 * same wire format the device uses. These tests pin down:
 *
 *   - SHUNT_CAL programming per the TI INA228 datasheet (SBOSA20):
 *     SHUNT_CAL = 13107.2e6 * CURRENT_LSB * R_shunt, multiplied by 4 when
 *     ADCRANGE = 1. A regression here previously multiplied by the raw
 *     ADCRANGE bit, writing SHUNT_CAL = 0 for ADCRANGE = 0.
 *   - CHARGE register decoding: 40-bit two's complement, so negative
 *     accumulated charge must sign-extend.
 *   - ina228_validate() against the fixed MFG/DEVICE ID registers.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "nsx_ina228.h"

#define REG_ADCCFG 0x01
#define REG_SHUNTCAL 0x02
#define REG_CHARGE 0x0A
#define REG_MFG_UID 0x3E
#define REG_DVC_UID 0x3F

#define NUM_REGS 0x40
#define MAX_REG_BYTES 5

static uint8_t fake_regs[NUM_REGS][MAX_REG_BYTES];

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
static void
test_shunt_cal_adcrange_1(void)
{
    ina228_context_t ctx = fresh_device();
    set_reg16(REG_ADCCFG, 1 << 4);
    CHECK(ina228_set_shunt(&ctx, 0.1f, 0.5f) == 0);
    CHECK(get_reg16(REG_SHUNTCAL) == 5000);
}

// SHUNT_CAL must not clobber the reserved top bit of the register.
static void
test_shunt_cal_preserves_reserved_bit(void)
{
    ina228_context_t ctx = fresh_device();
    set_reg16(REG_SHUNTCAL, 0x8000);
    CHECK(ina228_set_shunt(&ctx, 0.1f, 0.5f) == 0);
    CHECK(get_reg16(REG_SHUNTCAL) == (0x8000 | 1250));
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

static void
test_validate(void)
{
    ina228_context_t ctx = fresh_device();
    set_reg16(REG_MFG_UID, 0x5449);
    set_reg16(REG_DVC_UID, 0x228);
    CHECK(ina228_validate(&ctx) == 0);

    set_reg16(REG_MFG_UID, 0xDEAD);
    CHECK(ina228_validate(&ctx) == 1);
}

int
main(void)
{
    test_shunt_cal_adcrange_0();
    test_shunt_cal_adcrange_1();
    test_shunt_cal_preserves_reserved_bit();
    test_charge_positive();
    test_charge_negative_sign_extends();
    test_validate();

    if (failures) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    printf("ina228 host unit tests passed\n");
    return 0;
}
