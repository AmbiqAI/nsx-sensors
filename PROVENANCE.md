# Provenance and release audit

## Release identity

| Item | Value |
| --- | --- |
| Project | `AmbiqAI/nsx-sensors` |
| Release candidate | `v0.1.0` |
| Audited `main` baseline | `9a73d590fee7b377011af0b998a7563571acc228` |
| neuralSPOT-X registry pin audited | `9a73d590fee7b377011af0b998a7563571acc228` |
| Commits after the registry pin | 0 |
| Release tag policy | immutable annotated `vMAJOR.MINOR.PATCH` tag |

The audited source is the fetched `origin/main` commit above, which is byte-for-byte
the revision the stable neuralSPOT-X registry lock already pins for `nsx-sensors`.
The eventual `v0.1.0` tag must point at the later `main` commit that merges this
release foundation; that merge SHA is deliberately not guessed or published here.

## Dependency pins

| Dependency | Exact revision | Role |
| --- | --- | --- |
| `AmbiqAI/nsx-ambiq-sdk` | `v5.2.24` / `a9f4ec25a162f6f3700623feb691423bb5a51132` | supplies `nsx-core`, `nsx-i2c`, `nsx-spi`, and their AmbiqSuite/SoC-HAL substrate |
| `AmbiqAI/neuralspotx` | `6dafd6880331eee8fc6caa99de1ccd8321f18495` | board and toolchain graph used for consumer configure smoke |

Module metadata records only the direct logical dependencies (`nsx-core`,
`nsx-i2c`, `nsx-spi`). The SDK and NSX pins are reproducibility records. They are
not vendored copies and this release does not request a registry change.

## Ambiq-authored source lineage

The five existing drivers listed below are Ambiq-authored ports. The new native
BME688 driver and its Bosch-derived calculations are documented separately
below. Ports are
line-level derivations, not verbatim copies: the NSX ports drop `ns_core_api_t`
version handshakes, retarget `ns_i2c`/`ns_spi` to `nsx_i2c`/`nsx_spi`, and move
to a caller-owned context struct instead of passing a bus config and device
address through every call.

| NSX file | Upstream file | Upstream repository | Upstream revision |
| --- | --- | --- | --- |
| `includes-api/nsx_max86150.h`, `src/nsx_max86150.c` | `neuralspot/ns-i2c/includes-api/ns_max86150_driver.h`, `neuralspot/ns-i2c/src/ns_max86150_driver.c` | `AmbiqAI/neuralSPOT` | `4264b9309e03064ffad13a0468d5d0c1110c5288` |
| `includes-api/nsx_mpu6050.h`, `src/nsx_mpu6050.c` | `neuralspot/ns-i2c/includes-api/ns_mpu6050_i2c_driver.h`, `neuralspot/ns-i2c/src/ns_mpu6050_i2c_driver.c` | `AmbiqAI/neuralSPOT` | `4264b9309e03064ffad13a0468d5d0c1110c5288` |
| `includes-api/nsx_icm45605.h`, `src/nsx_icm45605.c` | `neuralspot/ns-imu/includes-api/ns_imu.h`, `neuralspot/ns-imu/src/ns_imu_icm45605_driver.c` | `AmbiqAI/neuralSPOT` | `69c58a11b8746dd6b72a0d6e7b2e796e1c1dd9c8` (driver), `6c0a87216c06a0f5692033f8bb0f5de86aa499b0` (header) |
| `includes-api/nsx_ina228.h`, `src/nsx_ina228.c` | `includes-api/ina228.h`, `src/ina228.c` | `AmbiqAI/ns-sensors` | `a59b545ec6d630f10c1af0a7f25bb4d8457cc5c8` (header), `407679a7b034ae38647bdc16977fd388b717337a` (source) |
| `includes-api/nsx_ledstick.h`, `src/nsx_ledstick.c` | `includes-api/ledstick.h`, `src/ledstick.c` | `AmbiqAI/ns-sensors` | `68a60afd713e0555fcd4f9322bc34ce29c7e984b` |

All listed revisions are reachable from the `main` branch of their repository.
`AmbiqAI/neuralSPOT` and `AmbiqAI/ns-sensors` are both BSD-3-Clause and are
Ambiq-owned, so the BSD-3-Clause `LICENSE` in this repository covers the ported
result. No third-party copyright was absorbed by these ports.

## Native BME688 and adapted Bosch calculations

The BME688 register/state/transport implementation is native NSX code, based
on the Bosch Sensortec **BME688 datasheet BST-BME688-DS000-03, rev 1.3,
February 2024**. It does not compile or call the Bosch sensor API.

| Item | Exact source |
| --- | --- |
| Upstream | `boschsensortec/BME68x_SensorAPI` |
| Version | v4.4.8 |
| Git commit | `80ea120a8b8ac987d7d79eb68a9ed796736be845` |
| Adapted source | `bme68x.c` floating-point `calc_temperature`, `calc_pressure`, `calc_humidity`, `calc_gas_resistance_high`, `calc_res_heat`; calibration decoding from `get_calib_data` and coefficient indices in `bme68x_defs.h` |
| NSX destination | `src/nsx_bme688_compensation.c` |
| Other adapted arithmetic | Forced-mode conversion-time estimate and gas-wait duration encoding in `src/nsx_bme688.c` |
| Copyright | Copyright (c) 2023 Bosch Sensortec GmbH. All rights reserved. |
| License | BSD-3-Clause, retained in `licenses/Bosch-BME68x-BSD-3-Clause.txt` and the compensation source header |

The original formulas are kept in small private helpers operating on typed
per-device calibration. `t_fine` is local to each compensation call. Heater
ambient temperature accepts a float; its public range is limited to -40..85 C.
The heater target is validated at 0..400 C, and the computed resistance code is
clamped to 0..255 before conversion to avoid undefined float-to-byte conversion
on unusual calibration. The pressure denominator is compared directly with
zero, removing the upstream float-to-int narrowing guard. Native code rejects
unprogrammed calibration, requires temperature oversampling for compensated
P/H, skips compensation for disabled P/H channels, and preserves zero numeric
outputs with explicit validity flags. Gas resistance uses the BME688 high-gas
variant only (variant ID 1); no BME680/engineering-sample fallback is assumed.

The duration encoder and conversion-time estimate preserve Bosch's arithmetic.
Every hardware write is a single register/value transaction. SPI page state is
read on each access, with no mutable cache to become inconsistent after an
ambiguous failed transfer. Mode changes, reset, and transport errors are handled
by the native state machine; the upstream driver's unbounded mode polling and
hidden forced-read retries are not carried over. No Bosch public header,
upstream device object, polling loop, or third-party driver runtime is shipped.
BSEC and its derived IAQ/VOC outputs are excluded.

`tests/bme688_host_unit/generate_bosch_oracle.c` is an optional offline maintainer
utility that runs the **unmodified upstream public APIs** against synthetic
factory-coefficient/register fixtures. Its comments record a reproducible
compile command; the exact commit above is required. Its three compensated
output vectors and four heater results were captured into the host tests.
Normal tests consume those fixed expected values and never clone/download or
execute the upstream source. They also independently check the datasheet's
100 ms heater example (`gas_wait_0 = 0x59`). None of these fixtures is a recorded
hardware measurement.

## Vendored third-party source

`vendor/tdk-icm45605/` is a partial copy of the TDK InvenSense ICM-45605 eMD
driver. It is redistributed under the permissive InvenSense terms reproduced in
`NOTICE` and in every vendored file header. It is **not** relicensed under
BSD-3-Clause.

| Item | Value |
| --- | --- |
| Component | TDK InvenSense ICM-45605 eMD driver |
| Version | `2.1.0` (`INV_IMU_VERSION_STRING` in `inv_imu_version.h`) |
| Intake path | `AmbiqAI/neuralSPOT`, `extern/drivers/tdk/icm45605/imu/`, revision `7a12440477411aa28aa732047d4dd33a6a3c8a73` |
| License | InvenSense permissive "use, copy, modify, and/or distribute ... with or without fee" grant, no warranty |

### Included subset

The upstream driver is organized into modules by its own `README.md`. This
repository vendors exactly the **common files** and the **basic driver**:

| File | Upstream module |
| --- | --- |
| `inv_imu.h` | common |
| `inv_imu_defs.h` | common |
| `inv_imu_regmap_le.h` | common |
| `inv_imu_regmap_be.h` | common |
| `inv_imu_transport.h` | common |
| `inv_imu_transport.c` | common |
| `inv_imu_driver.h` | basic driver |
| `inv_imu_driver.c` | basic driver |
| `inv_imu_version.h` | basic driver |

Only `inv_imu_driver.c` and `inv_imu_transport.c` are compiled; they are listed
explicitly in `CMakeLists.txt`.

### Deliberate exclusions

| Excluded file | Upstream module | Reason |
| --- | --- | --- |
| `inv_imu_driver_advanced.h`, `inv_imu_driver_advanced.c` | advanced driver | out of scope for the basic 6DOF read path |
| `inv_imu_edmp.h`, `inv_imu_edmp.c`, `inv_imu_edmp_defs.h`, `inv_imu_edmp_memmap.h` | eDMP | no eDMP feature is exposed |
| `inv_imu_selftest.h`, `inv_imu_selftest.c` | self-test | no self-test API is exposed |
| `README.md` | documentation | upstream module documentation, summarized here instead |

### Local modifications

The only change from the upstream copies is the removal of the `imu/` include
path prefix, so that `#include "imu/inv_imu_defs.h"` becomes
`#include "inv_imu_defs.h"`. This affects the include lines of `inv_imu_defs.h`,
`inv_imu_driver.h`, `inv_imu_driver.c`, and `inv_imu_transport.c` only. The
remaining five files are byte-identical to the intake path above. No register
map, no scaling constant, and no license header was altered.

## Release-relevant changes since the previous audit point

| Change | Where | Release treatment |
| --- | --- | --- |
| `float32_t` -> `float` across the INA228 public API and implementation, dropping the `arm_math.h` include | `includes-api/nsx_ina228.h`, `src/nsx_ina228.c`, commit `9a73d59` | Included. `float32_t` is a CMSIS-DSP typedef for `float`, and nsx-sensors never declared or linked CMSIS-DSP, so every consumer failed to build on a missing `arm_math.h`. The two types are identical, so this is source- and ABI-compatible for any consumer that could previously compile. |
| ICM-45605 driver and TDK vendor subset added | commit `9a73d59` | Included, first appearance in a release. |
| Removal of the unimplemented `frame_available_cb`, `frame_size`, and `frame_buffer` context fields | this release branch | Included. See `CHANGELOG.md`; the fields were never read or written by the driver. |
