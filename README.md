# nsx-sensors

External NSX module for reusable I2C/SPI-attached sensor and accessory device
drivers.

Current drivers:

| Device | Bus | Public header |
| --- | --- | --- |
| Bosch BME688 temperature/pressure/humidity/raw gas | I2C / 4-wire SPI | `include/nsx/sensors/bme688.h` |
| MAX86150 ECG/PPG sensor | I2C | `include/nsx/sensors/max86150.h` |
| MPU6050 IMU | I2C | `include/nsx/sensors/mpu6050.h` |
| INA228 power and current monitor | I2C | `include/nsx/sensors/ina228.h` |
| SparkFun Qwiic LED Stick | I2C | `include/nsx/sensors/ledstick.h` |
| TDK ICM-45605 6-axis IMU | SPI | `includes-api/nsx_icm45605.h` |

This branch implements the first stage of [RFC 0001 (PR #17)](https://github.com/AmbiqAI/nsx-sensors/pull/17).
BME688, LED Stick, INA228, MPU6050 and MAX86150 have portable cores. ICM-45605 still requires
NSX. The shared NSX port is available; Zephyr wrappers remain follow-up work. This is an
unreleased API transition, not a v1 release or a drop-in v0 replacement.

## Usage

Portable CMake builds need no SDK, RTOS, network, or sibling repository:

```cmake
add_subdirectory(third_party/nsx-sensors)
target_link_libraries(my_app PRIVATE nsx::sensor_bme688 nsx::sensor_ledstick nsx::sensor_ina228 nsx::sensor_mpu6050 nsx::sensor_max86150)
```

All five portable drivers are enabled by default. Set `NSX_SENSOR_BME688=OFF`,
`NSX_SENSOR_LEDSTICK=OFF`, `NSX_SENSOR_INA228=OFF`, `NSX_SENSOR_MPU6050=OFF`, or `NSX_SENSOR_MAX86150=OFF` to omit a driver. Installed consumers use
`find_package(nsx_sensors CONFIG REQUIRED COMPONENTS bme688 ledstick ina228 mpu6050 max86150)` and the
same targets. The runnable [fake-HAL example](examples/baremetal/) exercises
binding/probing and command writes without pretending to read physical sensors.

Caller-owned endpoints implement synchronous transfers using the board HAL.
See [migration and transport contracts](docs/migration/portable-foundation.md)
and the [BME688 guide](docs/bme688.md). INA228 users should follow
the [INA228 migration guide](docs/migration/ina228.md). MPU6050 users should follow
the [MPU6050 migration guide](docs/migration/mpu6050.md). MAX86150 users should follow
the [MAX86150 migration and audit guide](docs/migration/max86150.md).

The remaining NSX drivers are enabled by default when the enclosing build
already supplies `nsx::core`, `nsx::i2c`, and `nsx::spi`. Standalone builds keep
that aggregate off. `NSX_SENSORS_LEGACY_NSX` can explicitly override this default.
The transitional `nsx::sensors` target contains ICM-45605 only.
BME688, LED Stick, INA228, MPU6050 and MAX86150 use the separate portable
targets plus the shared `nsx::sensor_port_nsx` adapter. See the
[NSX port guide](docs/nsx-port.md) for bindings and the SDK prerequisite.
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
