# nsx-sensors

External NSX module for reusable sensor and accessory device
drivers.

Current drivers:

| Device | Bus | Public header |
| --- | --- | --- |
| Bosch BME688 temperature/pressure/humidity/raw gas | I2C / 4-wire SPI | `include/nsx/sensors/bme688.h` |
| MAX86150 ECG/PPG sensor | I2C | `include/nsx/sensors/max86150.h` |
| MPU6050 IMU | I2C | `include/nsx/sensors/mpu6050.h` |
| INA228 power and current monitor | I2C | `include/nsx/sensors/ina228.h` |
| SparkFun Qwiic LED Stick | I2C | `include/nsx/sensors/ledstick.h` |
| TDK ICM-45605 6-axis IMU | SPI | `include/nsx/sensors/icm45605.h` |
| ams OSRAM TSL2540 / TSL2541 light | I2C | `include/nsx/sensors/tsl254x.h` |
| TDK T5838 microphone AAD control | THSEL one-wire; audio separate | `include/nsx/sensors/t5838.h` |

This branch implements the first stage of [RFC 0001 (PR #17)](https://github.com/AmbiqAI/nsx-sensors/pull/17).
All eight driver cores are portable. ICM-45605 retains the TDK basic
implementation with per-device transport callbacks. The shared NSX port is available; Zephyr wrappers remain follow-up work. This is an
unreleased API transition, not a v1 release or a drop-in v0 replacement.

See [TSL254x](docs/tsl254x.md), [T5838 control/audio boundaries](docs/t5838.md) and
[Spark integration examples](examples/spark/README.md). T5838 needs a board
THSEL timing endpoint; the NSX I2C/SPI adapter does not provide one.

## Usage

Portable CMake builds need no SDK, RTOS, network, or sibling repository:

See [shared CMake compilation settings](docs/cmake-integration.md) for framework
CPU/FPU/ABI propagation and installed-package requirements.

```cmake
add_subdirectory(third_party/nsx-sensors)
target_link_libraries(my_app PRIVATE nsx::sensor_bme688 nsx::sensor_ledstick nsx::sensor_ina228 nsx::sensor_mpu6050 nsx::sensor_max86150 nsx::sensor_icm45605)
```

All eight portable drivers are enabled by default. Set `NSX_SENSOR_BME688=OFF`,
`NSX_SENSOR_LEDSTICK=OFF`, `NSX_SENSOR_INA228=OFF`, `NSX_SENSOR_MPU6050=OFF`, `NSX_SENSOR_MAX86150=OFF`, `NSX_SENSOR_ICM45605=OFF`, `NSX_SENSOR_TSL254X=OFF`, or `NSX_SENSOR_T5838=OFF` to omit a driver. Installed consumers use
`find_package(nsx_sensors CONFIG REQUIRED COMPONENTS bme688 ledstick ina228 mpu6050 max86150 icm45605 tsl254x t5838)` and the
same targets. The runnable [fake-HAL example](examples/baremetal/) exercises
binding/probing and command writes without pretending to read physical sensors.

Caller-owned endpoints implement synchronous transfers using the board HAL.
See [migration and transport contracts](docs/migration/portable-foundation.md)
and the [BME688 guide](docs/bme688.md). See the
[pre-v1 API cleanup](docs/migration/api-cleanup.md) for enum/type migration and
a lifecycle comparison across the original six drivers. INA228 users should follow
the [INA228 migration guide](docs/migration/ina228.md). MPU6050 users should follow
the [MPU6050 migration guide](docs/migration/mpu6050.md). MAX86150 users should follow
the [MAX86150 migration and audit guide](docs/migration/max86150.md).

`nsx::sensors` is an always-available aggregate of the selected portable drivers.
The transitional `NSX_SENSORS_LEGACY_NSX` option is removed. For NSX transport,
link `nsx::sensor_port_nsx` explicitly; it defaults on when the enclosing build
supplies `nsx::core`, `nsx::i2c`, and `nsx::spi`, or can be selected with
`NSX_SENSOR_PORT_NSX`. See the [NSX port guide](docs/nsx-port.md).
ICM-45605 users should follow its [migration guide](docs/migration/icm45605.md).
Existing apps remain pinned to v0 releases until deliberately migrated.

Run `tests/portable_build.sh` for offline host tests and real consumer links.

## Contracts worth knowing

- **ICM-45605 contexts are independent and address-stable.** Typed TDK state
  lives in each caller-owned context. Do not copy/move one after binding.
- **Interrupt work is deferred.** An ISR schedules a worker; the worker calls
  the context-aware status/read APIs. Bus calls and calibration are blocking.

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
| [RFC 0001](docs/rfcs/0001-portable-sensor-driver-architecture.md) | Draft v1 portable architecture, API, and driver migration plan |
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
