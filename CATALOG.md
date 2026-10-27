# Module catalog entry

Current metadata is `0.3.0`; the v1 source contract was accepted in merged
PR #32 (`f3b313a`) and v1 publication remains separate. This inventory describes
the current source tree, not the historical v0.1.0 release audit. Downstream
registry promotion requires an explicitly selected immutable tag or full SHA;
this qualification work does not repin consumers.

| Module | Metadata | Aggregate target | NSX resolver dependencies |
| --- | --- | --- | --- |
| `nsx-sensors` | `nsx-module.yaml` | `nsx::sensors` | `nsx-core`, `nsx-i2c`, `nsx-spi` |

The dependencies above apply to NSX resolution. Portable cores and their
`nsx::sensors` interface aggregate build without the SDK. All eight ordinary
CMake source options default ON; the aggregate includes only selected cores.

| Core | Interface | Public header under `include/nsx/sensors/` | Target / option suffix / installed component |
| --- | --- | --- | --- |
| BME688 forced T/P/H/raw gas | I2C / 4-wire SPI | `bme688.h` | `nsx::sensor_bme688` / `BME688` / `bme688` |
| MAX86150 ECG/PPG | I2C | `max86150.h` | `nsx::sensor_max86150` / `MAX86150` / `max86150` |
| MPU6050 6-axis IMU | I2C | `mpu6050.h` | `nsx::sensor_mpu6050` / `MPU6050` / `mpu6050` |
| INA228 power/current monitor | I2C | `ina228.h` | `nsx::sensor_ina228` / `INA228` / `ina228` |
| Qwiic LED Stick | I2C | `ledstick.h` | `nsx::sensor_ledstick` / `LEDSTICK` / `ledstick` |
| ICM-45605 basic 6-axis IMU | 4-wire SPI | `icm45605.h` | `nsx::sensor_icm45605` / `ICM45605` / `icm45605` |
| TSL2540/TSL2541 raw visible/IR counts | I2C | `tsl254x.h` | `nsx::sensor_tsl254x` / `TSL254X` / `tsl254x` |
| T5838 acoustic activity control | Timed CLK/THSEL waveform | `t5838.h` | `nsx::sensor_t5838` / `T5838` / `t5838` |

Options use the `NSX_SENSOR_` prefix. `nsx::sensor_common` supplies shared bus
helpers (`common` installed component); `bus.h`, `runtime.h`, and `status.h`
define shared contracts. Disabled device headers and libraries are not installed.
Portable exports use `nsxSensorTargets` with namespace `nsx::`.

NSX users explicitly link `nsx::sensor_port_nsx` and include `port_nsx.h`.
Its default is ON only when the enclosing build already provides `nsx::core`,
`nsx::i2c`, and `nsx::spi`. Installed `port_nsx` is opt-in through
`NSX_SENSOR_INSTALL_NSX_PORT` and requires consumer-loaded SDK bus targets.
Zephyr uses `zephyr/module.yml` and opt-in Kconfig core selection, the same
production core targets, `port_zephyr.h`, and `nsx_sensor_port_zephyr` when I2C
or SPI is enabled. Only BME688 has a Zephyr sensor API wrapper. Neither shared
bus adapter implements T5838 waveform delivery or PDM/PCM capture.

The compatibility declaration covers Apollo3, Apollo3P, Apollo4L, Apollo4P,
Apollo330P, Apollo510, Apollo510B, and Apollo510L. This is a build expectation,
not hardware qualification; see [compatibility](docs/compatibility.md),
[firmware validation](docs/firmware-validation.md), and
[Zephyr validation](docs/zephyr-validation.md) for scoped evidence.
The [source contract](docs/v1-api-contract.md) requires recompilation and
promises no stable binary ABI. See the [migration review](docs/migration/release-review.md)
for defaults, recovery, historical lineage and remaining release gates.
