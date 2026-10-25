# Changelog

## Unreleased ICM-45605 portable basic migration

- Replace the NSX-only singleton API with `<nsx/sensors/icm45605.h>` and
  `nsx::sensor_icm45605`: caller-owned typed TDK state and per-instance callbacks.
- Provide validated basic SPI low-noise configuration, idle init, explicit
  start/stop, raw/scaled register samples, deferred data-ready status, opt-in
  INT2 output and explicit bounded stationary calibration.
- Audit native ranges, read-error publication, partial writes, fresh calibration
  and vendor interrupt reserved bits. Keep all TDK notices and record patches.
- Complete core portability: `nsx::sensors` aggregates all selected cores in the
  portable export; remove `NSX_SENSORS_LEGACY_NSX` and the last legacy header.
  NSX users link the shared adapter separately. Version remains 0.3.0 pending v1.
- Add fake indirect-register/two-device/failure/calibration tests and installed
  C/C++/NSX build coverage. FIFO/APEX/additional interfaces follow in issue #23;
  Spark integration and hardware qualification remain pending in #13/#4.

## Unreleased MAX86150 portable migration

- Fix review findings: retain one frame during continuous reads to prevent stale
  A_FULL from fabricating full queues, invalidate ambiguous pre-burst read failures,
  and detect power resets during FIFO recovery and initialization. Add producer
  interleaving, bus-failure and power-loss regressions.

- Move MAX86150 to `<nsx/sensors/max86150.h>` / `nsx::sensor_max86150`, using
  shared bus/runtime/status types, pure bind/probe/validation, idle init, and
  explicit start. Only ICM-45605 remains in the legacy aggregate.
- Audit/fix empty/full FIFO accounting, signed 18-bit ECG decoding, ignored
  transfer errors, unsafe caller-supplied slot counts, invalid LED selectors and
  reserved range encodings. Use native-width endian-independent decoding.
- Add capacity-aware continuous and frozen-snapshot FIFO APIs with complete-frame
  bursts, explicit recovery and unchanged outputs on failure. Remove unchecked
  raw register/pointer writes, including writes to the read-only overflow counter.
- Document hardware overflow-counter clearing, flush/synchronization behavior,
  unsupported automatic proximity mode, continuous loss-detection limits, and
  deferred hardware qualification. Add fault/slot/overflow/multi-instance tests.

## Unreleased MPU6050 portable migration

- Address review findings: clear stale DRDY before each averaged sample, reject
  FIFO consumption while disabled, and batch complete FIFO frames to reduce I2C
  overhead. Add independently timed conversion, disabled-read, multi-frame burst,
  failure/overflow publication, and bus-clock-budget regressions.

- Move MPU6050 to `<nsx/sensors/mpu6050.h>` and `nsx::sensor_mpu6050`;
  the transitional aggregate now contains only MAX86150 and ICM-45605.
- Separate pure binding/validation, identity probe, initialization into sleep,
  and explicit acquisition start. Use caller-owned common bus/runtime descriptors.
- Correct full-scale register bit positions, signed decoding, FIFO data register
  access, temperature/axis scaling, and accelerometer offset compensation-bit handling.
- Add typed raw/physical samples and capacity-aware FIFO reads with explicit
  overflow/partial-consumption recovery. Bound resets, averaging and calibration;
  calibration remains explicit and preserves +1g on Z.
- Add host fault injection, every native FIFO format, independent CMake selection,
  relocated C/C++ consumers and shared NSX adapter coverage. Hardware qualification
  remains pending. See `docs/migration/mpu6050.md` for breaking API changes.

## Unreleased INA228 portable migration

- Move INA228 to `include/nsx/sensors/ina228.h` / `src/ina228/ina228.c` and
  `nsx::sensor_ina228`; remove it from the legacy aggregate.
- Replace binding-only init with shared-bus bind, identity probe, pure config
  validation, and explicit initialization into shutdown. Start conversions
  separately with `ina228_set_mode`. Return common sensor statuses.
- Preserve units and exact raw accumulators. Failed reads preserve output;
  scaled reads require verified calibration. Reject invalid enums/nonfinite or
  zero calibration and invalidate cached scaling on uncertain writes/reset.
- Preflight range recalibration, ignore undefined low measurement bits, correct
  alert-polarity documentation, and document read-to-clear behavior.
- Extend portable host, sanitizer, package, adapter, and SoC compile coverage.


All notable changes to `nsx-sensors` are documented here. This project follows
[Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Portable foundation (draft, breaking)

- Introduce shared synchronous bus/status/runtime contracts and portable BME688
  and LED Stick targets, with selectable builds and relocatable CMake exports.
- Split BME688 bind/probe/validate/init; reset now requires reinitialization.
  Remove device-specific NSX adapters pending the shared port. Use common statuses.
- Validate LED commands and invalidate bindings after attempted address changes.
- Keep the four unmigrated drivers in a `NSX_SENSORS_LEGACY_NSX` aggregate,
  enabled by default when the enclosing build supplies the NSX dependencies. See the migration guide; this branch is not a drop-in v0 upgrade.
- Add offline protocol, lifecycle, deadline, package-link and sanitizer checks.


### Initial BME688 implementation (PR #16 baseline)

The following describes the baseline before the portable migration above.

- Native BME688 forced-mode temperature, pressure, humidity, and optional raw
  gas resistance, using caller-owned per-device contexts and abstract register
  transports. Separate start/collect calls permit sleeping during conversion;
  a bounded blocking helper is also provided.
- Typed oversampling/filter/heater configuration and readback, reset and device
  validation, bounded sleep transitions, stale-sample rejection, and independent
  gas-valid/heater-stable flags. NSX I2C and 4-wire SPI adapters reuse initialized
  caller-owned buses with aligned/padded transfer storage.
- Bosch v4.4.8-derived calibration, compensation, and heater calculations with
  retained BSD-3-Clause terms and exact provenance; no vendor-driver dependency.
- Offline host tests for the core and real NSX adapters, fixed Bosch golden
  vectors, transport faults, and address/page handling, including sanitizer CI.
  BME688 hardware is unqualified. Parallel heater-profile scanning remains
  follow-up work for issue #14; no BSEC/IAQ/VOC outputs are implemented.

## [0.3.0] - 2026-08-12

A full audit of the INA228 driver against the TI datasheet (SLYS021). Every
unit conversion was re-derived and verified correct; the changes below are
calibration edge cases, error handling, and public-surface corrections. Minor
rather than patch because the public surface changes (see Breaking below).

> **Correction (2026-08-12, after release):** this entry originally attributed
> the SHUNT_CAL write change to an Apollo510B hardware failure of the masked
> read-modify-write. That report was withdrawn: the failing firmware was
> unknowingly running v0.1.0 (a consumer-side dependency-pinning bug), whose
> `set_shunt()` computed SHUNT_CAL = 0 arithmetically. No transport-level
> write corruption was ever observed. The plain full-register write and
> readback verify remain, on their own merits: bit 15 is reserved-always-0 so
> there is nothing to preserve, and the verify converts any genuinely lost
> write into an error instead of silently-zero measurements. v0.3.0 was
> subsequently verified end-to-end on Apollo510B hardware.

### Breaking

- **`ina228_alert_type_t` values changed.** Every value was one bit position
  too low for the DIAG_ALRT register it is masked against (the old values
  started from MEMSTAT, bit 0, instead of CNVRF, bit 1), so any caller masking
  `ina228_alert_functions()` output with these constants misread every flag.
  The values now match Table 7-16, and `INA228_ALERT_OVERTEMP`,
  `INA228_ALERT_MATH_OVERFLOW`, `INA228_ALERT_CHARGE_OVERFLOW`, and
  `INA228_ALERT_ENERGY_OVERFLOW` are added. Callers using the old numeric
  values must recompile against the new header.
- **`ina228_context_t` gained a `_calibrated` field**, changing the struct
  layout. Recompile; do not mix objects built against 0.2.x headers.
- **`ina228_set_shunt()` now fails** (returns nonzero, device untouched) for
  non-positive inputs and when the computed SHUNT_CAL exceeds the register's
  15-bit field. Previously an oversized value was silently masked into the
  field — R = 1 Ω, Imax = 2 A programmed 17232 instead of 50000, a silent
  2.9x miscalibration — and values above 65535 hit undefined float-to-uint16
  conversion first.

### Fixed

- `ina228_set_adc_range()` now reprograms SHUNT_CAL when the device is
  already calibrated: the calibration carries a x4 factor when ADCRANGE = 1,
  so changing the range after `ina228_set_shunt()` silently invalidated it
  (4x low after 0→1). If the x4 recalibration would overflow the 15-bit
  field, the call reports failure and `ina228_set_shunt()` must be called
  before measurements are trusted.
- `ina228_set_shunt()` writes SHUNT_CAL as a plain full-register write and
  verifies it by readback. Bit 15 is reserved and always reads 0, so the
  previous masked read-modify-write preserved nothing and cost an extra
  transfer; the readback verify converts a lost write — the one register
  where that means silently-zero current, power, energy, and charge — into
  an error return. (See the correction note above: the hardware-failure
  report that originally motivated this change was withdrawn.)
- A failed I2C read now propagates out of every getter with the output value
  zeroed, instead of fabricating a result from an uninitialized buffer, and
  every read-modify-write setter aborts on a failed read instead of writing
  back garbage (previously a single transient I2C error could silently
  reconfigure the device).
- `ina228_read_energy()` and `ina228_read_charge()` scale through double
  instead of accumulating the 40-bit register in float, which quantized
  large values (about 512-LSB steps near 2^32).

### Added

- `ina228_read_energy_raw()` and `ina228_read_charge_raw()`: exact 64-bit
  register values for windowed (delta) measurements, which stay exact where
  the float conversions cannot (a float carries 24 mantissa bits against the
  registers' 40).
- Documented that `ina228_set_shunt()` is required after `ina228_init()` and
  after `ina228_reset()` before current, power, energy, or charge readings
  are meaningful (the device powers up with SHUNT_CAL = 0x1000, which
  matches no driver calibration), and that DIAG_ALRT reads clear the latched
  flags when ALATCH = 1.

## [0.2.1] - 2026-08-12

Four INA228 register-map defects, all verified against the TI INA228 datasheet
(SLYS021, January 2021). The first was reported from Apollo510B hardware
bring-up; the rest were found while confirming it against the register tables.

### Fixed

- `ina228_validate()` compared the whole 16-bit DEVICE_ID register against
  `0x228`. Per Table 7-24 that register is `DIEID` in bits 15:4 plus silicon
  `REV_ID` in bits 3:0, so a real part reads `0x2281` and validation rejected
  every genuine INA228. The revision is now masked off. Reported from hardware
  bring-up on Apollo510B.
- `ina228_set_adc_range()` and `ina228_get_adc_range()` addressed bit 4 of
  ADC_CONFIG (`0x01`). `ADCRANGE` is bit 4 of CONFIG (`0x00`); bit 4 of
  ADC_CONFIG is the middle bit of `VTCT`. Selecting the ±40.96 mV range
  therefore left the shunt range untouched and silently retuned the
  temperature conversion time from 1052 µs to 4120 µs, while
  `ina228_get_adc_range()` returned a `VTCT` bit. Because that read feeds
  `ina228_set_shunt()` and `ina228_read_shunt_voltage()`, a caller that
  selected the ±40.96 mV range got a SHUNT_CAL 4x too large and a shunt
  voltage 4x too small. At power-on defaults both registers read 0 in bit 4,
  so a caller that never touched the ADC range was unaffected.
- `ina228_conversion_ready()` read DIAG_ALRT bit 0, which is `MEMSTAT` and
  reads `1` on any part with healthy trim memory, so polling reported ready
  immediately and unconditionally. `CNVRF` is bit 1.
- `INA228_REG_PWRLIMIT` was defined as `0x10`, the address of `TEMP_LIMIT`.
  `PWR_LIMIT` is `0x11`. The macro is not referenced by any driver function,
  so this was latent.

### Added

- Documented on `ina228_reset_accumulators()` that `RSTACC` clears
  `DIAG_ALRT.MATHOF` but not `ENERGYOF` or `CHARGEOF`, which clear only when
  the ENERGY or CHARGE register is read, and that a free-running ADC can raise
  `MATHOF` again while SHUNT_CAL is still zero.
- Host unit test coverage for all four fixes, including that ADC_CONFIG is
  left untouched when the ADC range is set. The new checks fail against 0.2.0.

## [0.2.0] - 2026-08-12

### Added

- `ina228_validate()` is now declared in `includes-api/nsx_ina228.h`. The
  function was always compiled into the library but missing from the public
  header; the additive header-surface change is why this is a minor release.
- Host unit tests for the INA228 register math
  (`tests/ina228_host_unit.sh`), compiled against a stubbed nsx-i2c
  transport and run in the host-contracts CI job. They pin the SHUNT_CAL
  and CHARGE arithmetic to the TI INA228 datasheet (SBOSA20) and fail
  against the pre-fix driver.

### Fixed

- `ina228_set_shunt()` programmed SHUNT_CAL by multiplying with the raw
  ADCRANGE register bit: ADCRANGE = 0 wrote SHUNT_CAL = 0, so current,
  power, energy, and charge all read zero, and ADCRANGE = 1 underreported
  every derived measurement by 4x. The datasheet formula is
  SHUNT_CAL = 13107.2e6 * CURRENT_LSB * R_shunt, multiplied by 4 only when
  ADCRANGE = 1. A failed ADC-range read now propagates its error instead of
  scaling by an uninitialized value.
- `ina228_read_charge()` decoded the 40-bit two's-complement CHARGE
  register as unsigned, so negative accumulated charge read as a huge
  positive value. It now sign-extends. The ENERGY register is unsigned and
  is unchanged.

## [0.1.0] - 2026-08-06

First semantic release. There is no prior tag, so everything below describes the
state of the release rather than a delta from a published version, except where
a change is called out against the audited `main` baseline
`9a73d590fee7b377011af0b998a7563571acc228`.

### Added

- Release foundation: `LICENSE` (BSD-3-Clause), `NOTICE`, `PROVENANCE.md`,
  `RELEASE.md`, `OWNERS.md`, `CATALOG.md`, `version.txt`, and
  `docs/compatibility.md`.
- Exact port lineage for every Ambiq-authored driver, and the exact vendored
  TDK ICM-45605 eMD subset, version, intake revision, exclusions, and local
  modifications, in `PROVENANCE.md`.
- Explicit `nsx::core`, `nsx::i2c`, and `nsx::spi` target guards with a named
  diagnostic instead of an unresolved-link failure.
- Host contract tests (`tests/test_release_foundation.py`), a nested-CMake
  consumer harness (`tests/nested_contract/`), a vendored-subset standalone
  compile smoke (`tests/vendor_compile_smoke.sh`, `-Wall -Wextra -Werror`), a
  module compile smoke
  against real NSX and AmbiqSuite headers (`tests/module_compile_smoke.sh`),
  and a full NSX consumer configure/build smoke
  (`tests/configure_target_smoke.sh`).
- Build verification for all eight declared SoCs against all three declared
  toolchains; see `docs/compatibility.md`.
- Pinned CI and manual immutable-release workflows.

### Changed

- **Breaking:** removed `frame_available_cb`, `frame_size`, and `frame_buffer`
  from `icm45605_context_t`, and removed the `icm45605_frame_available_cb`
  typedef. The driver never collected frames, never wrote `frame_buffer`, and
  never invoked the callback. The only observable effect was that a non-NULL
  callback opted the device into INT2 data-ready configuration during
  `icm45605_init()`. That opt-in is now the explicit
  `icm45605_context_t.enable_drdy_interrupt` flag. Callers that previously set
  `frame_available_cb` to enable interrupts should set
  `enable_drdy_interrupt = 1` instead; no caller can lose buffering, because
  none was ever implemented.
- **Breaking:** renamed the reserved include guards `__INA228_H` and
  `__LED_STICK_H` to `NSX_INA228_H` and `NSX_LEDSTICK_H`. Identifiers beginning
  with two underscores are reserved to the implementation. Code that tested the
  old guard macros must be updated; normal `#include` users are unaffected.
- `install(TARGETS ...)` now uses the shared `nsxTargets` export set instead of
  a private `nsx_sensorsTargets` set that no NSX package config installed, so
  the target is no longer dropped from the exported package.
- The target now sets `EXPORT_NAME sensors`, so the exported name matches the
  advertised `nsx::sensors` alias instead of `nsx::nsx_sensors`.
- `project()` is now declared only for a top-level standalone configure, with
  `VERSION 0.1.0 LANGUAGES C`. Nested `add_subdirectory()` use by an NSX build
  no longer opens a second project scope. Target name, alias, include
  interface, link interface, and install destinations are unchanged.
- Vendor sources are listed explicitly instead of being pulled in by
  `file(GLOB CONFIGURE_DEPENDS)`.
- `nsx_max86150.h` preprocessor directives are unindented to match the rest of
  the public API. No declaration or macro value changed.

### Fixed

- `GNUInstallDirs` is now included, so `CMAKE_INSTALL_INCLUDEDIR` is defined for
  the install interface and both `install()` rules. Previously the module relied
  on a consumer having included `GNUInstallDirs` first; without that, the
  variable expanded empty and the export interface and headers landed on a bare
  relative path.
- `CMakeLists.txt` ends with a newline.

### Carried forward from `9a73d59`

- The INA228 public API and implementation use plain `float` instead of the
  CMSIS-DSP `float32_t` typedef, and no longer include `arm_math.h`.
  `nsx-sensors` never declared or linked CMSIS-DSP, so every consumer failed on
  a missing `arm_math.h`. `float32_t` is a typedef for `float`, so this is
  source- and ABI-compatible for any consumer that could previously compile.
- The ICM-45605 SPI driver and its vendored TDK subset appear in a release for
  the first time.

### Known caveats

- ICM-45605 keeps its TDK device handle and SPI transport binding in file-static
  storage, so exactly one device may be active per firmware image and the most
  recent successful `icm45605_init()` wins. `icm45605_handle_interrupt()` is
  deliberately context-free so it can be called from an ISR, and therefore
  always targets that single bound device.
- `icm45605_init()` checks `WHOAMI` before the soft reset and accumulates status
  with `|=` across the configuration calls, so a late failure is reported but
  does not abort the remaining configuration.
- `icm45605_calibrate()` blocks for roughly 5.2 s (260 samples at a 20 ms
  busy-wait) and requires the device to be stationary and level.
- `icm45605_get_data()` indexes its scale tables with `ctx->accel_fsr` and
  `ctx->gyro_fsr` without a range check; callers must supply valid TDK FSR
  enum values.
- No driver in this release is hardware-in-the-loop qualified. See
  `docs/compatibility.md`.

[0.1.0]: https://github.com/AmbiqAI/nsx-sensors/releases/tag/v0.1.0
