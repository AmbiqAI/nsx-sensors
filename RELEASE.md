# Release policy and current qualification scope

Version metadata is `1.0.0`. This major version adopts the accepted v1 source
contract and requires v0 callers to migrate and recompile. Publication is a
separate gated maintainer action; metadata or PR merge alone does not create a
tag, release or downstream registry pin. See the [candidate checklist](docs/releases/v1.0.0.md)
for pending approval and the [readiness audit](docs/v1-readiness.md) for evidence.
Release ownership is defined in [OWNERS.md](OWNERS.md).

**No driver is hardware-in-the-loop qualified.** This software release scope
makes no electrical timing, physical accuracy or runtime stack guarantee.

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

## Repository controls and release approval

The required configuration below is separate from the workflow source. Track
application and read-back evidence in [issue #5](https://github.com/AmbiqAI/nsx-sensors/issues/5);
merging this policy does not configure GitHub or prove these settings are active.

- Keep main's PR review, deletion and force-push protection. A separate active
  `main-required-ci` branch ruleset requires every job in
  `scripts/release_gate.py::REQUIRED_JOBS`, from the GitHub Actions app, with
  branches up to date before merging and no bypass actors. Separating this rule
  allows the existing administrator review bypass without bypassing required CI.
  A job rename must update the gate and repository rule together.
- An active `immutable-release-tags` tag ruleset matches `refs/tags/v*`, forbids
  updates and deletions, and has no bypass actors. Creation remains allowed so
  the release workflow can create a new tag; this rule does not restrict who can
  create tags or freeze GitHub release assets. Only the manual workflow is the
  supported publication path.
- The `release` environment requires approval by `apage224`, permits that user to
  approve their own dispatch, and disables administrator protection-rule bypass.
  Its custom deployment policy allows only the **branch** `main`, not tags.
  This is an explicit maintainer approval gate, not independent two-person review.
  No environment secrets are needed; the publication job explicitly requests
  `contents: write` and `actions: read`.
- Default workflow token permissions are read-only, and workflows cannot approve
  pull requests. Explicit job permissions remain the authority for tasks such as
  release publication.

The publication job waits on `release` after validation. The approver checks the
version, exact SHA, CI, provenance and release checklist before approving. The
job rechecks CI and fetched main after approval before any tag write. If main
advanced while waiting, dispatch a new candidate and validate it again. GitHub
reads and tag creation are not an atomic operation; serialize release work and
avoid merging new changes during final approval/publication.

### Bypass and retry discipline

An administrator may use the existing main review bypass only after recording
explicit maintainer merge approval and completed reviews; the separate CI rule
still applies. Never disable required checks or tag protections to force a
release. Repository administrators can edit settings, so record and review any
control change in issue #5 and read it back after application.

For a transient CI failure, rerun the failed workflow on the same SHA and wait
for both main workflows to pass; historical green attempts do not qualify.
For an interrupted publication, first inspect the run, tag target, release and
assets. Use `retry_existing_tag=true` only when the existing tag still targets
current main and the requested version matches. The retry needs fresh environment
approval and the same exact-SHA checks. Never move/delete the tag. If main has
advanced or published content needs correction, stop and obtain a separately
reviewed recovery decision; prefer a new version for changed content.

After settings changes, verify effective main rules, tag rules and bypass lists,
the environment's reviewer/branch policy, and workflow defaults through GitHub's
read APIs. Do not dispatch a real publication just to test the settings.
