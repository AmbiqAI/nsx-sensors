/* SPDX-License-Identifier: BSD-3-Clause
 * Synthetic calibration and raw ADC inputs, shared with the optional Bosch
 * oracle generator. Not recorded sensor measurements or hardware evidence.
 */
#ifndef BME688_TEST_FIXTURE_H
#define BME688_TEST_FIXTURE_H
#include <stdint.h>
#include <string.h>
static void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void fixture(uint8_t memory[256])
{
    uint8_t c[42] = { 0 };
    memset(memory, 0, 256);
    memory[0xd0] = 0x61; memory[0xf0] = 1;
    put16(c, 26435); c[2] = 3;
    put16(c + 4, 36477); put16(c + 6, (uint16_t)-10685); c[8] = 88;
    put16(c + 10, 10435); put16(c + 12, (uint16_t)-320);
    c[14] = (uint8_t)-100; c[15] = 30;
    put16(c + 18, (uint16_t)-12000); put16(c + 20, 5000); c[22] = 30;
    c[23] = 0x16; c[24] = 0xab; c[25] = 0x04; /* H1=75, H2=362; distinct shared nibbles. */
    c[26] = 0; c[27] = 45; c[28] = 20; c[29] = 120; c[30] = (uint8_t)-100;
    put16(c + 31, 27504); put16(c + 33, (uint16_t)-2500);
    c[35] = (uint8_t)-30; c[36] = 4; c[37] = (uint8_t)-15; c[39] = 0x10;
    memcpy(memory + 0x8a, c, 23); memcpy(memory + 0xe1, c + 23, 14); memcpy(memory, c + 37, 5);
}
static void raw_field(uint8_t memory[256], uint32_t temperature, uint32_t pressure,
                      uint16_t humidity, uint16_t gas, uint8_t range, uint8_t index)
{
    uint8_t *f = memory + 0x1d;
    memset(f, 0, 17); f[0] = 0x80; f[1] = index;
    f[2] = (uint8_t)(pressure >> 12); f[3] = (uint8_t)(pressure >> 4); f[4] = (uint8_t)(pressure << 4);
    f[5] = (uint8_t)(temperature >> 12); f[6] = (uint8_t)(temperature >> 4); f[7] = (uint8_t)(temperature << 4);
    f[8] = (uint8_t)(humidity >> 8); f[9] = (uint8_t)humidity;
    f[15] = (uint8_t)(gas >> 2); f[16] = (uint8_t)((gas << 6) | 0x30 | range);
}
#endif
