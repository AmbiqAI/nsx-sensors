# Release policy

## `v0.1.0`

`v0.1.0` is the first semantic release of this repository. The module metadata
already declared `0.1.0` but there are no prior tags, no published releases, and
no earlier compatibility promise. This release makes that declaration real: it
fixes the coherence between `version.txt`, `nsx-module.yaml`, and the CMake
project version, and records the provenance, license, ownership, and
compatibility surfaces that a consumable release needs.

`version.txt` is the release source of truth and is checked against
`nsx-module.yaml` and the standalone `project(... VERSION ...)` declaration.

## Scope

This release includes:

- Context-based drivers for MAX86150, MPU6050, ICM-45605, INA228, and the
  SparkFun Qwiic LED Stick, over `nsx-i2c` and `nsx-spi`.
- The vendored TDK ICM-45605 eMD common + basic-driver subset, redistributed
  under the InvenSense terms recorded in `NOTICE` and `PROVENANCE.md`.
- A single `nsx::sensors` target that exports as `nsx::sensors` into the shared
  `nsxTargets` export set and installs its public headers.
- License, notice, provenance, ownership, changelog, catalog, and compatibility
  metadata, plus host contract tests, three levels of compile smoke, and
  pinned CI.
- Build verification for every declared SoC against every declared toolchain.

This release does **not** include:

- ICM-45605 sample buffering or a frame-available callback. The unimplemented
  fields were removed rather than shipped as a promise.
- Hardware-in-the-loop qualification. No board was flashed for this release, so
  no driver is claimed as runtime validated. See `docs/compatibility.md`.
- The TDK advanced driver, eDMP, or self-test modules.
- Any registry, neuralSPOT, neuralSPOT-X, or application repository change.
- A published tag or GitHub release. Publication is a separate, manual step.

## Compatibility surfaces

Treat these as the public contract for semantic versioning decisions:

- The `nsx_*` public headers in `includes-api/`, including every context struct
  layout, enum value, and function signature.
- The CMake target name `nsx_sensors`, its `nsx::sensors` alias, its
  `EXPORT_NAME`, its export set, and its installed header layout.
- The declared dependency set and SoC/toolchain compatibility in
  `nsx-module.yaml`.
- The ICM-45605 single-instance and context-free interrupt-handler contracts
  documented in `includes-api/nsx_icm45605.h`.

Use patch releases for fixes that preserve those surfaces, minor releases for
additive compatible changes, and major releases for breaking changes. While the
version is `0.x`, a minor bump may break compatibility, and any such break must
be called out in `CHANGELOG.md`.

## Immutable publication

The release workflow is manual by design. A maintainer supplies the expected
version and the workflow:

1. verifies the checked-out commit is exactly `origin/main`;
2. verifies `version.txt` matches the requested version and `nsx-module.yaml`;
3. refuses to retarget an existing tag;
4. re-runs the host contract tests on the release commit;
5. waits for a successful `ci.yml` run on that **exact** commit SHA, on `main`,
   from a `push` event;
6. creates an annotated `vMAJOR.MINOR.PATCH` tag at that verified SHA;
7. uploads `nsx-sensors-VERSION.tar.gz` and its SHA-256 checksum.

That workflow is the only supported publication path. Tags are immutable: never
delete or move a published tag; publish a new patch version instead.
