/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2023 Bosch Sensortec GmbH. All rights reserved.
 * Copyright (c) 2026 Ambiq Micro, Inc.
 * Compensation/calibration/heater math adapted from Bosch BME68x Sensor API
 * v4.4.8, 80ea120a8b8ac987d7d79eb68a9ed796736be845 (bme68x.c/defs.h).
 * Full Bosch license: licenses/Bosch-BME68x-BSD-3-Clause.txt; see PROVENANCE.md.
 *
 * Copyright (c) 2023 Bosch Sensortec GmbH. All rights reserved.
 *
 * BSD-3-Clause
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 *     contributors may be used to endorse or promote products derived from
 *     this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#include "private.h"

typedef struct {
    const bme688_calibration_t *calibration;
    float t_fine;
    float ambient_temperature_c;
} bme688_compensation_t;

static float calc_temperature(uint32_t temp_adc, bme688_compensation_t *dev)
{
    float var1;
    float var2;
    float calc_temp;

    /* calculate var1 data */
    var1 = ((((float)temp_adc / 16384.0f) - ((float)dev->calibration->par_t1 / 1024.0f)) * ((float)dev->calibration->par_t2));

    /* calculate var2 data */
    var2 =
        (((((float)temp_adc / 131072.0f) - ((float)dev->calibration->par_t1 / 8192.0f)) *
          (((float)temp_adc / 131072.0f) - ((float)dev->calibration->par_t1 / 8192.0f))) * ((float)dev->calibration->par_t3 * 16.0f));

    /* t_fine value*/
    dev->t_fine = (var1 + var2);

    /* compensated temperature data*/
    calc_temp = ((dev->t_fine) / 5120.0f);

    return calc_temp;
}

/* @brief This internal API is used to calculate the pressure value. */
static float calc_pressure(uint32_t pres_adc, const bme688_compensation_t *dev)
{
    float var1;
    float var2;
    float var3;
    float calc_pres;

    var1 = (((float)dev->t_fine / 2.0f) - 64000.0f);
    var2 = var1 * var1 * (((float)dev->calibration->par_p6) / (131072.0f));
    var2 = var2 + (var1 * ((float)dev->calibration->par_p5) * 2.0f);
    var2 = (var2 / 4.0f) + (((float)dev->calibration->par_p4) * 65536.0f);
    var1 = (((((float)dev->calibration->par_p3 * var1 * var1) / 16384.0f) + ((float)dev->calibration->par_p2 * var1)) / 524288.0f);
    var1 = ((1.0f + (var1 / 32768.0f)) * ((float)dev->calibration->par_p1));
    calc_pres = (1048576.0f - ((float)pres_adc));

    /* Avoid exception caused by division by zero */
    if (var1 != 0.0f)
    {
        calc_pres = (((calc_pres - (var2 / 4096.0f)) * 6250.0f) / var1);
        var1 = (((float)dev->calibration->par_p9) * calc_pres * calc_pres) / 2147483648.0f;
        var2 = calc_pres * (((float)dev->calibration->par_p8) / 32768.0f);
        var3 = ((calc_pres / 256.0f) * (calc_pres / 256.0f) * (calc_pres / 256.0f) * (dev->calibration->par_p10 / 131072.0f));
        calc_pres = (calc_pres + (var1 + var2 + var3 + ((float)dev->calibration->par_p7 * 128.0f)) / 16.0f);
    }
    else
    {
        calc_pres = 0;
    }

    return calc_pres;
}

/* This internal API is used to calculate the humidity in integer */
static float calc_humidity(uint16_t hum_adc, const bme688_compensation_t *dev)
{
    float calc_hum;
    float var1;
    float var2;
    float var3;
    float var4;
    float temp_comp;

    /* compensated temperature data*/
    temp_comp = ((dev->t_fine) / 5120.0f);
    var1 = (float)((float)hum_adc) -
           (((float)dev->calibration->par_h1 * 16.0f) + (((float)dev->calibration->par_h3 / 2.0f) * temp_comp));
    var2 = var1 *
           ((float)(((float)dev->calibration->par_h2 / 262144.0f) *
                    (1.0f + (((float)dev->calibration->par_h4 / 16384.0f) * temp_comp) +
                     (((float)dev->calibration->par_h5 / 1048576.0f) * temp_comp * temp_comp))));
    var3 = (float)dev->calibration->par_h6 / 16384.0f;
    var4 = (float)dev->calibration->par_h7 / 2097152.0f;
    calc_hum = var2 + ((var3 + (var4 * temp_comp)) * var2 * var2);
    if (calc_hum > 100.0f)
    {
        calc_hum = 100.0f;
    }
    else if (calc_hum < 0.0f)
    {
        calc_hum = 0.0f;
    }

    return calc_hum;
}

static float calc_gas_resistance_high(uint16_t gas_res_adc, uint8_t gas_range)
{
    float calc_gas_res;
    uint32_t var1 = UINT32_C(262144) >> gas_range;
    int32_t var2 = (int32_t)gas_res_adc - INT32_C(512);

    var2 *= INT32_C(3);
    var2 = INT32_C(4096) + var2;

    calc_gas_res = 1000000.0f * (float)var1 / (float)var2;

    return calc_gas_res;
}

/* This internal API is used to calculate the heater resistance value using float */
static uint8_t calc_res_heat(uint16_t temp, const bme688_compensation_t *dev)
{
    float var1;
    float var2;
    float var3;
    float var4;
    float var5;
    uint8_t res_heat;

    if (temp > 400) /* Cap temperature */
    {
        temp = 400;
    }

    var1 = (((float)dev->calibration->par_gh1 / (16.0f)) + 49.0f);
    var2 = ((((float)dev->calibration->par_gh2 / (32768.0f)) * (0.0005f)) + 0.00235f);
    var3 = ((float)dev->calibration->par_gh3 / (1024.0f));
    var4 = (var1 * (1.0f + (var2 * (float)temp)));
    var5 = (var4 + (var3 * (float)dev->ambient_temperature_c));
    float code =
        (3.4f *
                  ((var5 * (4 / (4 + (float)dev->calibration->res_heat_range)) *
                    (1 / (1 + ((float)dev->calibration->res_heat_val * 0.002f)))) -
                   25));

    if (code < 0.0f) code = 0.0f;
    if (code > 255.0f) code = 255.0f;
    res_heat = (uint8_t)code;
    return res_heat;
}


static uint16_t le16(const uint8_t *bytes)
{
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8));
}

void bme688_decode_calibration(bme688_calibration_t *c, const uint8_t b[42])
{
    c->par_t1 = le16(b + 31); c->par_t2 = (int16_t)le16(b); c->par_t3 = (int8_t)b[2];
    c->par_p1 = le16(b + 4); c->par_p2 = (int16_t)le16(b + 6); c->par_p3 = (int8_t)b[8];
    c->par_p4 = (int16_t)le16(b + 10); c->par_p5 = (int16_t)le16(b + 12);
    c->par_p6 = (int8_t)b[15]; c->par_p7 = (int8_t)b[14];
    c->par_p8 = (int16_t)le16(b + 18); c->par_p9 = (int16_t)le16(b + 20); c->par_p10 = b[22];
    c->par_h1 = (uint16_t)(((uint16_t)b[25] << 4) | (b[24] & 15));
    c->par_h2 = (uint16_t)(((uint16_t)b[23] << 4) | (b[24] >> 4));
    c->par_h3 = (int8_t)b[26]; c->par_h4 = (int8_t)b[27]; c->par_h5 = (int8_t)b[28];
    c->par_h6 = b[29]; c->par_h7 = (int8_t)b[30];
    c->par_gh1 = (int8_t)b[35]; c->par_gh2 = (int16_t)le16(b + 33); c->par_gh3 = (int8_t)b[36];
    c->res_heat_val = (int8_t)b[37]; c->res_heat_range = (uint8_t)((b[39] & 0x30) >> 4);
}

void bme688_compensate(const bme688_calibration_t *c, const uint8_t b[17], bme688_sample_t *s)
{
    bme688_compensation_t work = { .calibration = c };
    uint32_t pressure = ((uint32_t)b[2] << 12) | ((uint32_t)b[3] << 4) | (b[4] >> 4);
    uint32_t temperature = ((uint32_t)b[5] << 12) | ((uint32_t)b[6] << 4) | (b[7] >> 4);
    uint16_t humidity = (uint16_t)(((uint16_t)b[8] << 8) | b[9]);
    uint16_t gas = (uint16_t)(((uint16_t)b[15] << 2) | (b[16] >> 6));
    s->temperature_c = calc_temperature(temperature, &work);
    if (s->pressure_valid) s->pressure_pa = calc_pressure(pressure, &work);
    if (s->humidity_valid) s->humidity_percent = calc_humidity(humidity, &work);
    if (s->gas_valid) s->gas_resistance_ohm = calc_gas_resistance_high(gas, b[16] & 15);
}

uint8_t bme688_heater_resistance(const bme688_calibration_t *c, uint16_t temperature_c,
                                float ambient_temperature_c)
{
    bme688_compensation_t work = { .calibration = c, .ambient_temperature_c = ambient_temperature_c };
    return calc_res_heat(temperature_c, &work);
}
