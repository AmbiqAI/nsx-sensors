# nsx-sensors

External NSX module for reusable I2C/SPI-attached sensor and accessory device
drivers.

Current drivers:

| Device | Bus | Public header |
| --- | --- | --- |
| MAX86150 ECG/PPG sensor | I2C | `includes-api/nsx_max86150.h` |
| MPU6050 IMU | I2C | `includes-api/nsx_mpu6050.h` |
| INA228 power and current monitor | I2C | `includes-api/nsx_ina228.h` |
| SparkFun Qwiic LED Stick | I2C | `includes-api/nsx_ledstick.h` |
| TDK ICM-45605 6-axis IMU | SPI | `includes-api/nsx_icm45605.h` |

The module is intentionally external to the unified SDK baseline. It builds on
focused transport modules such as `nsx-i2c` and `nsx-spi`, and exposes a
consistent context-based initialization pattern across drivers.

## Usage

Add the module from an NSX build that already provides `nsx::core`, `nsx::i2c`,
and `nsx::spi`, then link the exported target:

```cmake
target_link_libraries(my_app PRIVATE nsx::sensors)
```

Callers own bus bring-up. Initialize the `nsx_i2c_config_t` or
`nsx_spi_config_t` first, then pass it into the device context. For ICM-45605
the caller also owns the chip-select pin, the interrupt GPIO, and the NVIC
wiring; set `enable_drdy_interrupt` to have `icm45605_init()` configure the
INT2 data-ready source.

No driver in this module buffers samples. Sample storage, framing, and batching
are the application's responsibility.

## Contracts worth knowing

- **ICM-45605 is single-instance.** The TDK device handle and its SPI transport
  binding live in file-static storage, so exactly one ICM-45605 may be active
  per firmware image and the most recent successful `icm45605_init()` wins.
- **`icm45605_handle_interrupt()` takes no context** by design, so it can be
  called directly from an ISR. It always targets that single bound device.

## Release status

`v0.1.0` is a build-and-metadata release. **No driver is hardware-in-the-loop
qualified**; no board was flashed and no sensor part was exercised for this
release. See `docs/compatibility.md` before relying on any device.

| Document | Purpose |
| --- | --- |
| `CHANGELOG.md` | What changed, including breaking changes and known caveats |
| `RELEASE.md` | Versioning policy, compatibility surfaces, publication process |
| `PROVENANCE.md` | Exact port lineage and the vendored TDK subset |
| `docs/compatibility.md` | Declared vs build-verified vs runtime-validated |
| `CATALOG.md` | Downstream registry view |
| `OWNERS.md` | What this repository owns and does not own |

## Licensing

Ambiq-authored code in `includes-api/` and `src/` is BSD-3-Clause; see
`LICENSE`. `vendor/tdk-icm45605/` is third-party TDK InvenSense code
redistributed under its own permissive terms and is **not** relicensed. See
`NOTICE` and `PROVENANCE.md`.
