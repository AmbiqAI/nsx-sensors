# v1 RFC readiness audit

Audited baseline: `149cb782b0845aa556a656745a56ea9453019964` (PR #31),
updated after PR #31 merged.
All eight implemented cores are now on main. PR #28 was reviewed at
`717baf0d0886eed2112d5cdc3e1707f8228abc61` before squash merge.
This is an implementation and release-evidence audit against
[RFC 0001](rfcs/0001-portable-sensor-driver-architecture.md), not a fresh
independent driver review or a freeze of the v1 API. Version metadata
remains `0.3.0`; the breaking portable transition is unreleased.

## Driver inventory

All eight implemented cores use caller-owned contexts, pure binding/configuration
validation, and explicit initialization. Seven use the common synchronous I2C/SPI
bus contract; T5838 uses a dedicated synchronous timed-waveform endpoint.
Identity probing exists where supported; LED Stick and T5838 have no invented probe.
Public headers live under `include/nsx/sensors/`; private protocol helpers live
with each core under `src/<device>/`.

| Driver | Implemented scope | Host evidence and migration guide |
| --- | --- | --- |
| BME688 | I2C/SPI forced-mode temperature, pressure, humidity and optional raw gas; native compensation | `tests/bme688_host_unit/`; [guide](bme688.md), [portable migration](migration/portable-foundation.md) |
| LED Stick | I2C commands, validated lengths/colors, explicit rebind after address changes | `tests/portable/test_bus_ledstick.c`; [migration](migration/portable-foundation.md) |
| INA228 | I2C configuration, raw/scaled readings, integer accumulators, explicit shunt calibration | `tests/ina228_host_unit/`; [migration](migration/ina228.md) |
| MPU6050 | I2C direct/FIFO acquisition, scaling, explicit bounded calibration and recovery | `tests/mpu6050_host/`; [migration](migration/mpu6050.md) |
| MAX86150 | I2C ECG/PPG FIFO acquisition, snapshot/continuous behavior and recovery | `tests/max86150_host/`; [migration](migration/max86150.md) |
| ICM-45605 | Basic SPI direct raw/scaled acquisition, DRDY and explicit calibration; typed per-instance TDK state | `tests/icm45605_host/`; [migration](migration/icm45605.md) |
| TSL2540/TSL2541 (PR #28) | Shared I2C core, explicit variant, raw visible/IR counts, gain/integration/thresholds, saturation and TSL2541 factory scaling; no application lux calibration | `tests/tsl254x_host/`; [guide at reviewed revision](https://github.com/AmbiqAI/nsx-sensors/blob/717baf0d0886eed2112d5cdc3e1707f8228abc61/docs/tsl254x.md) |
| T5838 (PR #28) | Analog/D1/D2 acoustic activity control, THSEL frame encoder, shared-clock pair example; PDM/PCM capture remains in audio layer | `tests/t5838_host/`; [guide at reviewed revision](https://github.com/AmbiqAI/nsx-sensors/blob/717baf0d0886eed2112d5cdc3e1707f8228abc61/docs/t5838.md) |

These tests establish software behavior with synthetic devices. No row claims
physical accuracy, successful electrical communication or hardware qualification.
ICM advanced functionality remains in [issue #23](https://github.com/AmbiqAI/nsx-sensors/issues/23).

## Completed review follow-up

[PR #26](https://github.com/AmbiqAI/nsx-sensors/pull/26) closes the two
consequential findings from the six-driver technical review:

- MAX86150 failed consuming interrupt reads invalidate configuration and FIFO
  state; recovery requires init because reset evidence may already be consumed.
  Fault-injection tests cover failures before and after status consumption.
- The NSX adapter accepts populated pointers for unused zero-length transfer
  segments, matching the common bus contract. I2C/SPI regressions cover both
  directions and reject invalid requests before locking or I/O.

[PR #27](https://github.com/AmbiqAI/nsx-sensors/pull/27) completes the requested
API cleanup: MPU6050 constants are prefixed, MAX86150 configuration selections
use named enums, and INA228 exposes its cached calibration scale without I/O.
Lifecycle, consuming-read recovery and device-operation serialization are
consolidated in the [API migration guide](migration/api-cleanup.md). Defaults,
register encodings and measurement units remain unchanged; callers must migrate
source names/types and recompile. MAX86150 has persistent short-enum regression
coverage. INA228 distinguishes rejected preflight calibration requests, which
preserve valid calibration, from failed programming/readback, which invalidates it.

Three local agents reviewed API, driver behavior and build/design coverage.
The resulting short-enum test correction and calibration wording clarification
were applied before the final Copilot passes. Hosted Copilot
[recommended approval with no open issues](https://github.com/AmbiqAI/nsx-sensors/pull/27#pullrequestreview-5645328774);
local Copilot found no consequential defects. Those Copilot passes reviewed
`31c646c`; rebasing onto merged #26 produced `8624975` with identical file contents
and fresh passing CI before squash merge. The local Copilot pass ran C/C++ syntax
and enum-layout probes, not the full linked suites or hardware tests; its
datasheet fetch failed. Full suite evidence comes from CI below. These reviews
and merge approval complete this cleanup, not the remaining RFC release gates.

### Spark additions: PR #28 (merged)

Three independent agents and hosted/local Copilot reviewed `5d90d03`.
Local Copilot identified insufficient T5838 pre/post-write clocking. Visual
inspection of DS-000383 rev1.2 Figure 22 confirmed **>50 CLK cycles** on each
side; text extraction had incorrectly reduced the annotation to five.
Commit `717baf0` supplies 60 cycles, documents the endpoint obligation, and
requires >50 in the fake endpoint for every write. The strengthened regression
failed against the old encoder. The full portable suite passed after the fix,
and a focused protocol follow-up confirmed it and neighboring symbol timing.

[Hosted Copilot](https://github.com/AmbiqAI/nsx-sensors/pull/28#pullrequestreview-5645700609)
reported no code issues and requested final human datasheet/timing verification;
its state is COMMENTED, not formal approval. Initial independent reviews and
Copilot covered the preceding SHA; the focused fix review and CI cover `717baf0`.
No review establishes physical waveform correctness or hardware qualification.

## Platform and build evidence

| Platform | Available integration | Evidence at audited baseline | Remaining acceptance work |
| --- | --- | --- | --- |
| Plain CMake / application HAL | Eight independently selectable cores; common bus/runtime and T5838 timed endpoint | Offline tests, ASan/UBSan, source and relocated installed C/C++ consumers; C++ consumer references functions from every driver | Real board HAL/example and resource measurements |
| NSX | Shared I2C/SPI adapter for seven cores; T5838 needs a board timing endpoint | Actual adapter with mocked SDK helpers, 3/5-byte command limits, installed consumers; real SDK configure; all eight declared SoCs compile with Arm GNU; real Apollo4 Plus bare-metal/FreeRTOS links | Representative real firmware link on the final release candidate; electrical qualification later |
| FreeRTOS | Portable cores usable through application bus/timing/locking callbacks | Real kernel/HAL/task/port link at pinned SDK, with strong handler/vector checks; no runtime execution | Final-candidate revalidation; hardware scheduler/bus qualification remains pending |
| Zephyr | Portable C cores only | No Zephyr sensor wrapper, devicetree binding, native simulation or cross-build | BME688 wrapper/example, pinned Zephyr version, mocked-bus/native tests and board cross-build |

Main CI [run 40234941475](https://github.com/AmbiqAI/nsx-sensors/actions/runs/40234941475)
passed all four jobs at the preceding six-core baseline `2ac3039`. See `.github/workflows/ci.yml`,
`tests/portable_build.sh` and `tests/nsx_port_host.sh` for exact commands.
The SDK pin is `1f18f01555897ed8e06437ff9727f2bb61771e70`, containing the
combined-I2C prerequisite. Historical Armclang/ATfE results are not evidence for
that six-core baseline. SDK-stub consumer links are not real firmware links.

PR #28 CI [run 40241138132](https://github.com/AmbiqAI/nsx-sensors/actions/runs/40241138132)
passed all four jobs at `717baf0`: host release/API contracts, nested CMake
consumer, vendored TDK compile, and all eight SoCs with Arm GNU. The full portable
suite passed locally, including ASan/UBSan, selected drivers, relocated installed
C/C++ consumers and short enums. At preceding `5d90d03`, independent checks also
built individual installed components and compiled both new cores plus the pair
helper with Arm GNU, Armclang and ATfE. Those three-toolchain probes were not
repeated after the timing fix; current embedded evidence is the Arm GNU CI matrix.

After squash merge, main CI [run 40242270537](https://github.com/AmbiqAI/nsx-sensors/actions/runs/40242270537)
passed all four jobs at the audited eight-core baseline `572a3d9`. This records the prior eight-core baseline; hardware qualification remains pending.

## Shared CMake and firmware verification: PRs #29 and #30

PR #29 merged as `b070b10`: one SDK or explicitly selected interface compilation
profile propagates CPU/FPU/ABI settings through common, all cores, vendor code,
NSX adapters and callers. Installed packages reconnect the recorded profile;
matching names alone do not establish ABI compatibility. The linked C/C++
short-enum regression, relocated consumers and negative configuration checks
passed. See [the compilation contract](cmake-integration.md).

PR #30 merged as `d3cda01`: the smoke script now requires a final Arm executable,
matching map and selected APIs. It checks code addresses and expected defining
archive members, rejecting same-named consumer replacements. GNU ld, LLVM lld
and Arm map layouts (including the optional load-address column) were exercised.
See [firmware verification](firmware-validation.md) for supported formats and limits.

Three independent reviewers covered the original stack. Local Copilot identified
the symbol-origin gap; follow-up reviews found a command documentation error and
an alternate Arm map layout. All were corrected and verified. The final parser
delta received a focused independent review; Copilot did not rereview that delta.
PR #30 was rebased to `4413daa` with identical file contents and all four checks
passed in [run 40377920546](https://github.com/AmbiqAI/nsx-sensors/actions/runs/40377920546)
before squash merge. These checks cover production targets in synthetic firmware,
not a real board application or hardware operation.

[Merged PR #31](https://github.com/AmbiqAI/nsx-sensors/pull/31) implements real NSX
bare-metal and FreeRTOS link fixtures for `apollo4p_evb` against SDK
`40b893d859672caf0b17b0e51db351a77b423e56`. At reviewed head `cc72e41`, clean local builds reproduced both links. An
independent reviewer also built the preceding implementation and checked the
final timeout fix. All five CI jobs, including real firmware integration, passed in
[run 40384579683](https://github.com/AmbiqAI/nsx-sensors/actions/runs/40384579683).
Checks cover all eight cores, both bindings, startup/HAL/kernel provenance,
strong FreeRTOS handlers, actual vector routing and shared compilation profiles.
The fixture deliberately leaves controllers uninitialized; no sensor I/O or
scheduler execution was tested. SDK/HAL/kernel are pinned, while CI records its
Ubuntu-provided compiler version rather than pinning that package.

PR #31 merged as `149cb78` after independent and local Copilot reviews.
Copilot found an unbounded symbol-inspector subprocess; `cc72e41` adds a timeout
and clean CLI failure. The regression fails the old code; focused independent
follow-up is clean. Copilot did not rereview that fix. The representative NSX
bare-metal/FreeRTOS link gate is complete at this baseline, with final release
candidate revalidation still required. Polished examples remain deferred.

[Draft PR #32](https://github.com/AmbiqAI/nsx-sensors/pull/32) proposes Zephyr
integration and final RFC/source-contract acceptance. That work and its API-freeze
decision remain unmerged and must not yet be treated as accepted release evidence.

## RFC decisions to record before freezing v1

The implementation resolves several proposed choices; maintainers still need to
record these as the frozen v1 source contract. The merged cleanup and its
reviews above are complete; the remaining decision is final RFC acceptance:

- `<nsx/sensors/port_nsx.h>` is the implemented port header, rather than the
  RFC sketch's `<nsx/sensors/ports/nsx.h>`.
- `nsx::sensors` aggregates selected portable cores only. Applications link
  `nsx::sensor_port_nsx` explicitly. All eight drivers default on; the port defaults
  on only when enclosing SDK targets exist. No legacy aggregate remains.
- Context storage is public and typed, private by convention. ICM carries TDK
  headers and must remain address-stable. No stable binary context ABI is promised.
- Transfers are synchronous and bounded by endpoint limits; combined I2C means
  repeated START and SPI means one CS assertion. Unsupported transactions fail
  before I/O. Async command queues remain a separate proposal.
- Runtime hooks are optional only where the device operation permits it. Without
  a clock, documented timeouts bound requested waits, not bus wall time.
- Device-specific lifecycle APIs remain appropriate: FIFO reads and accessory
  commands need not mimic BME688 start/collect. Naming, units, consuming
  status reads, failure recovery and context lifetime have been reviewed and
  documented in the API migration guide; preserve these contracts in wrappers.

The historical RFC implementation notes describe intermediate commits. This
snapshot supersedes them as the current progress inventory, not as an automatic
change to RFC acceptance requirements.

## Ordered release checklist

PRs #28–#31 are merged; the eight-core main baseline is recorded above. Final RFC
acceptance and release qualification remain separate from this merge.

1. **Record final RFC acceptance.** The API cleanup is reviewed and merged.
   Record acceptance of the choices above and any explicit scope amendments
   before freezing v1; this is not another request to redo the completed reviews.
2. **Finish framework integration.** PR #31 supplies real NSX bare-metal/FreeRTOS link fixtures. Complete the
   BME688 Zephyr wrapper and integration checks in PR #32. Product examples can follow. Select and pin actual kernel/framework, HAL,
   SDK and toolchain revisions; add separate reproducible integration checks.
   A Zephyr wrapper for one driver does not qualify the other drivers.
3. **Measure resource costs.** Record per-context and NSX scratch sizes, linked
   flash/RAM and compiler stack-usage evidence on a named target/toolchain with
   optimization settings. Include deepest vendor/callback paths and distinguish
   static estimates from measured runtime high-water marks.
4. **Validate the release candidate.** Rerun the offline suites and eight-SoC
   compile matrix on the final SHA; link a real NSX firmware consumer and the
   FreeRTOS/Zephyr examples. Record which toolchains were actually rerun.
5. **Finish provenance and migration review.** Retain vendor notices and review
   documented patches; record maintainer review per `OWNERS.md`. The automated
   PR #24 license evidence check is not a separately recorded maintainer approval.
   Audit remaining historical path references in NOTICE/PROVENANCE and make
   current source mappings explicit without rewriting original license terms.
6. **Prepare v1 metadata in a release PR.** Update `version.txt`,
   `nsx-module.yaml`, CMake version, changelog, catalog and compatibility claims
   together. Verify repository controls tracked by
   [issue #5](https://github.com/AmbiqAI/nsx-sensors/issues/5). Publish only through
   the immutable workflow after exact-main-commit CI passes. Coordinate a
   downstream registry update separately; do not silently repin applications.

## Deferred hardware qualification

Hardware is unavailable. Preserve that limitation on any API release; do not
silently mark qualification complete or make it an implied result of CI.
[Issue #4](https://github.com/AmbiqAI/nsx-sensors/issues/4) tracks hardware work;
[issue #13](https://github.com/AmbiqAI/nsx-sensors/issues/13) covers Spark ICM integration.
Record board/sensor revisions, bus settings, SDK/toolchain and firmware SHA for:

- repeated-START and CS continuity waveforms, reset and injected bus failures;
- startup, conversion, FIFO/interrupt behavior and two-device shared-bus operation;
- independent reference comparisons, stationary calibration, heater behavior and
  power measurements as applicable to each driver.

The portable portions of TSL254x (issue #15) and T5838 (issue #12) are implemented
in merged PR #28; both tracking issues remain open for integration/qualification.
Spark's supplied draft selects TSL2540; select that explicit variant in board code.
Remaining T5838 work is a concrete timer-based THSEL endpoint, separate control
pins for the pair, shared programming/PDM-clock handoff, and waveform/WAKE/stereo
capture validation. Do not substitute ordinary RTOS delays for bounded pulse timing.
These board/audio tasks are separate from portable-driver merge readiness.
Expanded BME688 operation (#14) and ICM advanced features (#23) remain follow-ups.
