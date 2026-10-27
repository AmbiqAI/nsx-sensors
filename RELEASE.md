# Release policy and current qualification scope

Version metadata remains `0.3.0`. V1 is unreleased: this qualification work
neither bumps the version nor creates a tag, release, registry pin or hardware
qualification. PR #32, merged at baseline `f3b313a`, established the v1 source
contract. Release acceptance remains a separate maintainer decision under
[OWNERS.md](OWNERS.md). See the [readiness audit](docs/v1-readiness.md)
for merged qualification evidence and remaining release steps.

`version.txt` is the version source of truth and must match `nsx-module.yaml`
and the standalone CMake project version. Historical v0.1.0 foundation evidence
and dependency hashes remain in [PROVENANCE.md](PROVENANCE.md); they do not
establish current release readiness or current downstream registry state.

## Current scope

Eight portable C11 cores cover BME688, MAX86150, MPU6050, INA228, LED Stick,
ICM-45605, TSL2540/TSL2541, and T5838 AAD control. Independently selected core
targets and `nsx::sensor_common` form the `nsx::sensors` interface aggregate.
The portable install export is `nsxSensorTargets`; NSX transport is linked
separately as `nsx::sensor_port_nsx`. Zephyr selects the same core definitions,
provides a shared I2C/SPI endpoint and a BME688 sensor wrapper. See
[CATALOG.md](CATALOG.md) for selection, installation and dependency boundaries.

The TDK ICM-45605 common/basic subset retains its InvenSense license; Bosch
adapted BME688 arithmetic retains its BSD-3-Clause attribution and license.
NOTICE, LICENSE and the Bosch license are installed with the package.
Advanced TDK/eDMP/self-test, BSEC/IAQ, other Zephyr sensor wrappers, T5838 board
waveform implementation and audio capture remain outside this scope.

## Compatibility policy

The [v1 source contract](docs/v1-api-contract.md) covers documented functions,
types, enum names/values, configuration/sample fields, behavior, public target
selection and header usage. Recompile callers, cores and adapters together.
There is no stable binary ABI, context layout/size, vendor-internal API, raw
struct serialization or cross-toolchain archive compatibility promise.

Use patch releases for compatible fixes, minor releases for compatible
additions, and major releases for breaking the accepted v1 source contract.
Pre-v1 source breaks must be called out in CHANGELOG and migration guidance;
existing applications should retain immutable dependencies until deliberately
migrated. A source freeze does not establish hardware accuracy, timing, resource
budgets or publication readiness. The [technical migration review](docs/migration/release-review.md)
records the reviewed scope and outstanding evidence.

## Immutable publication

The manual release workflow is the supported publication path. It checks the
requested version and release metadata, verifies HEAD equals fetched main,
refuses tag retargeting, and reruns host contracts. Publication requires successful
`ci.yml` and `zephyr.yml` push runs on main for the exact release SHA, with every
required job successful, including real NSX firmware. Missing, skipped, cancelled
or failed required evidence must not permit publication; waiting and evidence
queries are bounded. The workflow's gate defines the required job list.

Only after validation does it create an annotated `vMAJOR.MINOR.PATCH` tag and
publish `nsx-sensors-VERSION.tar.gz` plus its SHA-256 checksum. An explicitly
requested retry may reuse a tag only when it already points at the verified
commit. Never delete or move a published tag; publish a new version instead.
No historical green run, local host run or source-freeze approval substitutes
for exact-release-commit CI and maintainer review. No electrical qualification
is implied by publication automation.
