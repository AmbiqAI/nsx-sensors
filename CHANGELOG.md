# Changelog

All notable changes to `nsx-sensors` are documented here. This project follows
[Semantic Versioning](https://semver.org/spec/v2.0.0.html).

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
