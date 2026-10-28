# Provenance and release audit

## Current qualification scope

Source-review baseline: merged PR #32, `f3b313a`; subsequent qualification is
recorded in the readiness audit. Current release-candidate metadata is `1.0.0`;
publication and maintainer release signoff remain separate. The eight portable cores and shared adapters are inventoried in
[CATALOG.md](CATALOG.md). The merged source contract is recorded in
[docs/v1-api-contract.md](docs/v1-api-contract.md); it promises documented source
compatibility after recompilation, not a stable binary ABI or context layout.
The technical reconciliation and remaining approval/evidence gaps are recorded
in [docs/migration/release-review.md](docs/migration/release-review.md).

## Historical v0.1.0 release-foundation audit

| Item | Value |
| --- | --- |
| Project | `AmbiqAI/nsx-sensors` |
| Release candidate | `v0.1.0` |
| Audited `main` baseline | `9a73d590fee7b377011af0b998a7563571acc228` |
| neuralSPOT-X registry pin audited | `9a73d590fee7b377011af0b998a7563571acc228` |
| Commits after the registry pin | 0 |
| Release tag policy | immutable annotated `vMAJOR.MINOR.PATCH` tag |

At that historical audit, the audited source was the fetched `origin/main`
commit above, byte-for-byte the revision then pinned by the stable
neuralSPOT-X registry lock for `nsx-sensors`.
The audit did not establish the eventual tag SHA. These values are historical
reproduction evidence, not a claim about the current registry pin, release tag,
or successful CI for the v1 qualification commit.

## Historical v0.1.0 dependency pins

| Dependency | Exact revision | Role |
| --- | --- | --- |
| `AmbiqAI/nsx-ambiq-sdk` | `v5.2.24` / `a9f4ec25a162f6f3700623feb691423bb5a51132` | supplies `nsx-core`, `nsx-i2c`, `nsx-spi`, and their AmbiqSuite/SoC-HAL substrate |
| `AmbiqAI/neuralspotx` | `6dafd6880331eee8fc6caa99de1ccd8321f18495` | board and toolchain graph used for consumer configure smoke |

Module metadata records only the direct logical dependencies (`nsx-core`,
`nsx-i2c`, `nsx-spi`). The SDK and NSX pins are reproducibility records. They are
not vendored copies and this release does not request a registry change.

## Ambiq-authored source lineage

The five original drivers listed below are Ambiq-authored ports. The new native
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
| `include/nsx/sensors/ledstick.h`, `src/ledstick/ledstick.c` | `includes-api/ledstick.h`, `src/ledstick.c` | `AmbiqAI/ns-sensors` | `68a60afd713e0555fcd4f9322bc34ce29c7e984b` |

All listed revisions are reachable from the `main` branch of their repository.
`AmbiqAI/neuralSPOT` and `AmbiqAI/ns-sensors` are both BSD-3-Clause and are
Ambiq-owned, so the BSD-3-Clause `LICENSE` in this repository covers the ported
result. No third-party copyright was absorbed by these ports.

## Current source path map

Historical paths above identify the original port and must not be used as current
include instructions. The later portable migrations retain that lineage:

| Historical repository path | Current public header / implementation |
| --- | --- |
| `includes-api/nsx_max86150.h`, `src/nsx_max86150.c` | `include/nsx/sensors/max86150.h`, `src/max86150/max86150.c` |
| `includes-api/nsx_mpu6050.h`, `src/nsx_mpu6050.c` | `include/nsx/sensors/mpu6050.h`, `src/mpu6050/mpu6050.c` |
| `includes-api/nsx_icm45605.h`, `src/nsx_icm45605.c` | `include/nsx/sensors/icm45605.h`, `src/icm45605/icm45605.c` |
| `includes-api/nsx_ina228.h`, `src/nsx_ina228.c` | `include/nsx/sensors/ina228.h`, `src/ina228/ina228.c` |
| `includes-api/nsx_ledstick.h`, `src/nsx_ledstick.c` | `include/nsx/sensors/ledstick.h`, `src/ledstick/ledstick.c` |
| `includes-api/nsx_bme688.h`, `src/nsx_bme688.c`, `src/nsx_bme688_compensation.c` | `include/nsx/sensors/bme688.h`, `src/bme688/bme688.c`, `src/bme688/compensation.c` |
| New portable cores (no legacy path) | `include/nsx/sensors/tsl254x.h`, `src/tsl254x/tsl254x.c`; `include/nsx/sensors/t5838.h`, `src/t5838/t5838.c` |

Shared Ambiq-authored code is in `src/common/bus.c`, `src/ports/nsx.c`,
`src/ports/zephyr.c`, and the Zephyr BME688 wrapper `zephyr/bme688.c`. Public
shared declarations are `status.h`, `bus.h`, `runtime.h`, `port_nsx.h`, and
`port_zephyr.h` under `include/nsx/sensors/`. CMake compiles the ICM wrapper
with the same two TDK sources recorded below. Source relocation does not
relicense those files or Bosch-derived arithmetic.

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
| NSX destination | `src/bme688/compensation.c` |
| Other adapted arithmetic | Forced-mode conversion-time estimate and gas-wait duration encoding in `src/bme688/bme688.c` |
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

The original intake removed the `imu/` include prefix in `inv_imu_defs.h`,
`inv_imu_driver.h`, `inv_imu_driver.c` and `inv_imu_transport.c`.

The v1 portable ICM-45605 migration adds these narrow local patches:

- `inv_imu_transport.h`: add a caller-owned `void *user` and pass it to read,
  write and sleep callbacks. Device layout still starts with the typed transport.
- `inv_imu_transport.c`: propagate that pointer through direct/indirect register
  accesses and all delays. Initialize indirect-read output before the address
  write so early failure cannot expose uninitialized vendor temporaries. No
  global current-device switch.
- `inv_imu_driver.c`: propagate the pointer through `inv_imu_sleep_us`; initialize
  the interrupt configuration struct so unused/reserved bits are deterministic.

The vendor-isolation patch additionally prefixes C identifiers beginning with
`inv_imu_` as `nsx_icm45605_tdk_` in the retained implementation and declarations.
It moves the unchanged callback, transport and device structure definitions to
`include/nsx/sensors/detail/icm45605_types.h`, with the original InvenSense
notice retained. That shared header is the actual type definition used by both
the NSX context and vendor core; no duplicate layout or aliasing cast is used.
Generic TDK headers are private build inputs and are no longer installed or
exported on consumer include paths. Upstream filenames and version guards stay
private; no public macro aliases rename another driver's API.

No register map, scaling constant, vendor version string or license header was
changed. Timing remains per-device through the earlier callback patch. The application wrapper suppresses
later physical transfers after a transport failure and initializes read buffers
because the retained vendor implementation can continue after errors.

License assessment for this patch: the existing InvenSense grant expressly
permits modification and redistribution. Every vendor notice is retained; the
installed typed-storage headers carry the same terms and NOTICE is installed
with the package. Vendor code is not relicensed as BSD-3-Clause. Maintainer
licensing review remains part of PR review per OWNERS.md. There is no new vendor
intake and no advanced/eDMP/self-test source in this change.

## Historical v0.1.0 changes since its previous audit point

| Change | Where | Release treatment |
| --- | --- | --- |
| `float32_t` -> `float` across the INA228 public API and implementation, dropping the `arm_math.h` include | `includes-api/nsx_ina228.h`, `src/nsx_ina228.c`, commit `9a73d59` | Included. `float32_t` is a CMSIS-DSP typedef for `float`, and nsx-sensors never declared or linked CMSIS-DSP, so every consumer failed to build on a missing `arm_math.h`. The two types are identical, so this is source- and ABI-compatible for any consumer that could previously compile. |
| ICM-45605 driver and TDK vendor subset added | commit `9a73d59` | Included, first appearance in a release. |
| Removal of the unimplemented `frame_available_cb`, `frame_size`, and `frame_buffer` context fields | v0.1.0 release-foundation branch | Included. See `CHANGELOG.md`; the fields were never read or written by the driver. |

## Portable foundation migration

The BME688 calculations are unchanged; relocation does not change their Bosch
provenance or licensing. LED Stick's existing `src/nsx_ledstick.c` port is now
`src/ledstick/ledstick.c`, using the shared transaction interface. Command values
and one-based numbering were cross-checked against SparkFun's
[Qwiic LED Stick reference](https://github.com/sparkfun/SparkFun_Qwiic_LED_Stick_Arduino_Library/blob/master/src/Qwiic_LED_Stick.cpp).
No additional vendor source was copied in this migration.

## Shared NSX adapter dependency

The v1 portable adapter is new Ambiq-authored code with no copied
vendor implementation. Its CI SDK prerequisite is
`1f18f01555897ed8e06437ff9727f2bb61771e70` (SDK PR #130), superseding the
v5.2.24 CI baseline above for this branch. It supplies the genuine combined
I2C operation. Hardware protocol reasoning uses Apollo3 Blue SoC Datasheet
DS-A3-1p3p0 sections 8.14.9/8.14.12 and 8.15.4; SDK limits come from each
SoC's HAL headers. See `docs/nsx-port.md` for limits and validation scope.

## Portable INA228 migration

The Ambiq-authored INA228 implementation moved from `src/nsx_ina228.c` to
`src/ina228/ina228.c`; its public header moved from `includes-api/nsx_ina228.h`
to `include/nsx/sensors/ina228.h`. No vendor source was copied. Register and
calibration semantics were cross-checked against TI INA228 SLYS021A (May 2022),
https://www.ti.com/lit/ds/symlink/ina228.pdf. The portable migration preserves
units/raw counters and tightens lifecycle, calibration, and error contracts;
see `docs/migration/ina228.md`.

## Portable MPU6050 migration

The Ambiq-authored MPU6050 port listed in the lineage table moved from
`src/nsx_mpu6050.c` / `includes-api/nsx_mpu6050.h` to
`src/mpu6050/mpu6050.c` / `include/nsx/sensors/mpu6050.h`, with native portable
transport, bounded timing, and corrected register/FIFO/calibration behavior.
No external implementation was copied. The InvenSense MPU-6000/MPU-6050
Product Specification PS-MPU-6000A-00 revision 3.4 (2013-08-19), available as
[the original manufacturer PDF mirrored by MikroElektronika](https://download.mikroe.com/documents/datasheets/MPU-6000_datasheet.pdf),
was consulted for sensitivities, temperature conversion, and operating modes.
The [i2cdevlib MPU6050 reference](https://github.com/jrowberg/i2cdevlib/blob/master/Arduino/MPU6050/MPU6050.cpp)
was consulted for register-map annotations and existing offset compensation
semantics. This is a rewrite of the existing Ambiq port, not a vendored import;
`vendor/tdk-icm45605` and its license are unchanged.
See `docs/migration/mpu6050.md` for behavior changes and qualification limits.

## Portable MAX86150 migration

The Ambiq-authored `src/nsx_max86150.c` / `includes-api/nsx_max86150.h` port moved
to `src/max86150/max86150.c` / `include/nsx/sensors/max86150.h`. The migration
keeps the original attribution; no external implementation was copied and no
vendor files changed. Register fields, FIFO behavior and signed ECG format were
audited against Maxim Integrated MAX86150 datasheet 19-8402, Rev 2 (12/18):
[manufacturer PDF](https://www.analog.com/media/en/technical-documentation/data-sheets/MAX86150.pdf),
also available as [the same manufacturer document mirrored by MikroElektronika](https://download.mikroe.com/documents/datasheets/MAX86150_datasheet.pdf).
See `docs/migration/max86150.md` for audit corrections and deliberate API limits.

### V1 ICM-45605 portable basic API

`include/nsx/sensors/icm45605.h` and `src/icm45605/icm45605.c` replace the
historical NSX wrapper listed above. The Ambiq wrapper uses the retained TDK
basic subset with caller-owned typed state, checked callbacks, native range
validation, separate raw/scaled types and explicit fresh-sample calibration.
Hardware remains unqualified. Future expansion is tracked in issue #23.

## Native Spark additions for v1

`src/tsl254x/` and `src/t5838/` are original implementations under the repository
BSD-3-Clause license; no additional vendor code is imported. Register/protocol
references are ams OSRAM DS000564 v2-00 (2022-12-05), DS000570 v2-00
(2023-03-10) and TDK DS-000383 rev1.2 (2025-09-04). The device guides link those
documents and explain variant, calibration and timing limits. Zephyr's TSL2540
implementation was inspected for comparison; no source or empirical lux formula
was incorporated. Existing TDK ICM vendor files and notices are unchanged.
