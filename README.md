# nsx-sensors

External NSX module for reusable I2C/SPI-attached sensor and accessory device
drivers.

Current drivers:

| Device | Bus | Public header |
| --- | --- | --- |
| Bosch BME688 temperature/pressure/humidity/raw gas | I2C / 4-wire SPI | `include/nsx/sensors/bme688.h` |
| MAX86150 ECG/PPG sensor | I2C | `includes-api/nsx_max86150.h` |
| MPU6050 IMU | I2C | `includes-api/nsx_mpu6050.h` |
| INA228 power and current monitor | I2C | `includes-api/nsx_ina228.h` |
| SparkFun Qwiic LED Stick | I2C | `include/nsx/sensors/ledstick.h` |
| TDK ICM-45605 6-axis IMU | SPI | `includes-api/nsx_icm45605.h` |

This branch implements the first stage of [RFC 0001 (PR #17)](https://github.com/AmbiqAI/nsx-sensors/pull/17).
BME688 and LED Stick have portable cores. The other four drivers still require
NSX; framework wrappers and the shared NSX port are follow-up work. This is an
unreleased API transition, not a v1 release or a drop-in v0 replacement.

## Usage

Portable CMake builds need no SDK, RTOS, network, or sibling repository:

```cmake
add_subdirectory(third_party/nsx-sensors)
target_link_libraries(my_app PRIVATE nsx::sensor_bme688 nsx::sensor_ledstick)
```

Both drivers are enabled by default. Set `NSX_SENSOR_BME688=OFF` or
`NSX_SENSOR_LEDSTICK=OFF` to omit a driver. Installed consumers use
`find_package(nsx_sensors CONFIG REQUIRED COMPONENTS bme688 ledstick)` and the
same targets. The runnable [fake-HAL example](examples/baremetal/) exercises
binding/probing and command writes without pretending to read physical sensors.

Caller-owned endpoints implement synchronous transfers using the board HAL.
See [migration and transport contracts](docs/migration/portable-foundation.md)
and the [BME688 guide](docs/bme688.md).

To build the remaining NSX drivers, explicitly set `NSX_SENSORS_LEGACY_NSX=ON`
before adding this module to a build that supplies `nsx::core`, `nsx::i2c`, and
`nsx::spi`. The transitional `nsx::sensors` target contains INA228, MPU6050,
MAX86150 and ICM-45605 only. BME688 and LED Stick use the separate portable
targets and require application endpoints until the shared NSX port lands.
Existing apps remain pinned to v0 releases until deliberately migrated.

Run `tests/portable_build.sh` for offline host tests and real consumer links.

## Contracts worth knowing

- **ICM-45605 is single-instance.** The TDK device handle and its SPI transport
  binding live in file-static storage, so exactly one ICM-45605 may be active
  per firmware image and the most recent successful `icm45605_init()` wins.
- **`icm45605_handle_interrupt()` takes no context** by design, so it can be
  called directly from an ISR. It always targets that single bound device.

## Release status

Version metadata remains at v0.3.0 during this unreleased transition. **No driver is hardware-in-the-loop
qualified**; no board was flashed and no sensor part was exercised for this
release. See `docs/compatibility.md` before relying on any device.

| Document | Purpose |
| --- | --- |
| `CHANGELOG.md` | What changed, including breaking changes and known caveats |
| `RELEASE.md` | Versioning policy, compatibility surfaces, publication process |
| `PROVENANCE.md` | Exact port lineage and the vendored TDK subset |
| `docs/compatibility.md` | Declared vs build-verified vs runtime-validated |
| `tests/` | Host contracts and the three compile-smoke levels |
| `CATALOG.md` | Downstream registry view |
| `OWNERS.md` | What this repository owns and does not own |

## Licensing

Ambiq-authored code in `includes-api/` and `src/` is BSD-3-Clause; see
`LICENSE`. `vendor/tdk-icm45605/` is third-party TDK InvenSense code
redistributed under its own permissive terms and is **not** relicensed. See
`NOTICE` and `PROVENANCE.md`. The native BME688 compensation/calibration/heater
math is adapted from Bosch Sensortec BME68x Sensor API v4.4.8 under its
BSD-3-Clause terms in `licenses/Bosch-BME68x-BSD-3-Clause.txt`.
