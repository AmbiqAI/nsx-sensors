/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 Ambiq Micro, Inc.
 */
#ifndef NSX_BME688_PRIVATE_H
#define NSX_BME688_PRIVATE_H
#include <nsx/sensors/bme688.h>

void bme688_decode_calibration(bme688_calibration_t *calibration, const uint8_t bytes[42]);
void bme688_compensate(const bme688_calibration_t *calibration, const uint8_t field[17],
                      bme688_sample_t *sample);
uint8_t bme688_heater_resistance(const bme688_calibration_t *calibration,
                                uint16_t temperature_c, float ambient_temperature_c);
#endif
