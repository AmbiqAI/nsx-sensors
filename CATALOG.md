# Module catalog entry

This is the catalog view intended for downstream neuralSPOT-X registry
promotion **after** the release is published. The registry should continue to
point at this project and should move to the immutable tag, not a branch.

| Module | Metadata | CMake target | Required dependencies | Release version |
| --- | --- | --- | --- | --- |
| `nsx-sensors` | `nsx-module.yaml` | `nsx::sensors` | `nsx-core`, `nsx-i2c`, `nsx-spi` | `0.1.0` |

## Devices

| Device | Bus | Public header |
| --- | --- | --- |
| Bosch BME688 T/P/H/raw gas (forced mode) | I2C / 4-wire SPI | `include/nsx/sensors/bme688.h` |
| MAX86150 ECG/PPG | I2C | `include/nsx/sensors/max86150.h` |
| MPU6050 6-axis IMU | I2C | `include/nsx/sensors/mpu6050.h` |
| INA228 power/current monitor | I2C | `include/nsx/sensors/ina228.h` |
| SparkFun Qwiic LED Stick | I2C | `include/nsx/sensors/ledstick.h` |
| TDK ICM-45605 6-axis IMU | SPI | `include/nsx/sensors/icm45605.h` |

The compatibility declaration covers the Apollo3, Apollo3P, Apollo4L, Apollo4P,
Apollo330P, Apollo510, Apollo510B, and Apollo510L families declared by
`nsx-i2c` and `nsx-spi`. Catalog support is a build expectation, not a claim of
hardware validation: no driver in `v0.1.0` is hardware-in-the-loop qualified.
See `docs/compatibility.md` for the evidence and for why `apollo5b` is
deliberately excluded.

The current registry lock already pins `nsx-sensors` at
`9a73d590fee7b377011af0b998a7563571acc228`. No registry, `neuralspotx`,
`neuralSPOT`, `ns-sensors`, or application repository is changed by this
release-preparation branch.

Unreleased migration: all six cores have portable per-driver targets.
`nsx::sensors` aggregates selected cores without SDK dependencies. Link the
shared `nsx::sensor_port_nsx` explicitly in NSX applications. ICM-45605 exposes
the basic SPI subset; see `docs/migration/icm45605.md`. Registry consumers must
not repin automatically during this transition.
