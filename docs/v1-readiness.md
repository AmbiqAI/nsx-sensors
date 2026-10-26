# v1 RFC readiness audit

Audited snapshot: `f3b313a773f3e2eb39fc16fc340fbd8c94ae75bf` (merged PR #32),
2026-10-26. Three fresh reviewers examined the complete snapshot, with separate
API/lifecycle, driver and build/integration emphases; a coordinating reviewer
checked release controls and reconciled evidence. Initial discovery did not use
the historical audit below. This is a software audit, not hardware qualification.

All eight cores, shared CMake integration and the v1 source-contract decision are
merged. Approval and merge of PR #32 accepted the source API freeze. Version
metadata remains `0.3.0`; release qualification and publication are not complete.

## Current actionable finding

**P2 — the publication gate ignores Zephyr CI.**
[release.yml at the audited revision](https://github.com/AmbiqAI/nsx-sensors/blob/f3b313a773f3e2eb39fc16fc340fbd8c94ae75bf/.github/workflows/release.yml#L85)
queries only `ci.yml`, then allows publication. The independently running
`zephyr.yml` can fail, remain pending, or be absent without affecting this gate.
That permits publication without the Zephyr final-candidate evidence required by
RFC 0001. This blocks release readiness, not acceptance of the source API.

The coordinator extracted the actual gate shell unchanged and executed it with
an offline `gh` stand-in: exact-SHA push/main CI succeeded and Zephyr was modeled
as failed. The gate returned success and never queried Zephyr. The build reviewer
independently confirmed the source path and requirement mismatch after recording
its own initial candidate ledger. No tag or release was created.

Require successful runs of both workflows on the exact release SHA, push event
and main branch, plus successful required jobs. Add behavioral tests for absent,
failed, cancelled, pending and skipped integration checks, and an all-success
control. Include the real firmware job in the explicit job list as skip protection;
current overall `ci.yml` success already rejects an ordinary firmware-job failure.

## Current evidence and limits

The fresh review checks and unresolved qualification work are distinguished from
historical PR evidence below. Reports are full-snapshot reviews; a clean result
means no consequential defect was substantiated in that scope, not proof of all
supported configurations or silicon behavior.

- API/lifecycle: all eight cores, both ports, wrapper, shared bus and relevant
  SDK/TDK paths inspected; actual NSX adapter contracts pass ASan/UBSan using
  SDK helper stand-ins. No consequential finding.
- Drivers: all eight core implementations, BME688 compensation and relevant TDK
  transport/vendor paths inspected. Two additional ASan/UBSan probes passed:
  BME688 trigger-write side effect followed by an error, and MAX86150 full-FIFO
  status consumption/shutdown/partial drain/restart transitions. TSL2541 register
  and saturation semantics and MAX86150 FIFO transitions were cross-checked with
  manufacturer documents. No consequential driver defect was substantiated.
  This was not a register-by-register audit of every device datasheet.
- Build/integration: shared root targets/source lists, transitive compilation
  profile and installed profile reattachment inspected. Real pinned SDK
  bare-metal/FreeRTOS links with verifier negative tests and busless Zephyr T5838
  build pass in separate temporary storage. The source/relocated C++ ABI probe
  completed its expected output without logged errors, but its process exit code
  was not retained; treat that run as supporting evidence only. The release
  gate finding above is the confirmed build/release defect.
- Resource/stack costs remain unqualified. Record per-context and adapter sizes,
  linked flash/RAM and compiler stack-usage on named target/toolchain/optimization
  settings; distinguish static estimates from runtime high-water measurements.
- Provenance and release docs need current source mappings and clear historical
  labeling. `NOTICE` still names relocated Bosch files; `CATALOG.md` retains the
  old version/six-device inventory. Retain original notices and record maintainer
  provenance approval; this audit does not supply legal signoff.
- Final release metadata and exact-candidate CI are still required. Historical
  PR checks and even current-main checks do not qualify a later release commit.
- Electrical/timing/accuracy qualification, T5838 board timing/audio, other Zephyr
  wrappers and advanced driver features remain explicit deferred work.

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
| Zephyr | Shared root CMake targets, I2C/SPI endpoints and BME688 wrapper; eight directly selectable cores | Pinned native mock-controller tests, Arm cross-link/provenance and busless T5838 build | Final-candidate rerun; other wrappers and actual controller/sensor qualification remain separate |

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

[PR #32](https://github.com/AmbiqAI/nsx-sensors/pull/32) implements Zephyr
integration and submits final RFC/source-contract acceptance. At reviewed head
`ab18f1f4f6ed669955caf6cf3d10ca10e11e05c1`, local validation passes four native
mock-controller tests, an actual Arm cross-link with defining-object checks,
a busless T5838 build and 48 existing contract tests. The module consumes the
shared root CMake targets, supplies reusable Zephyr endpoints, and exposes a
BME688 temperature/pressure/humidity wrapper. Other cores are directly selectable;
this is not a claim that every core has a Zephyr sensor wrapper.

Two independent reviews and local Copilot covered the proposal. The independent
API review found SPI configuration-pointer caching across rebind; immutable
persistent caller specs and a failing-before/passing-after regression resolve it.
Focused follow-up also tested rejected rebind and endpoint storage reuse. Local
Copilot CLI 1.0.103 continued independently after its `/review` worker returned no
report; it ran native/Arm/T5838 validation on `c991ad9` and found missing loopback
rejection. That regression fails the old code; the final fix rejects before I/O
or mutation and received a clean focused independent review. A fresh local
Copilot CLI 1.0.103 `/review` subsequently reviewed `149cb78..ab18f1f` including
all fixes, found no actionable findings, and independently passed native4/4,
Arm/newlib link/provenance and busless T5838 checks. Initial hosted CI exposed
incompatible Ubuntu newlib; the
reviewed fix provisions checksum-verified official Arm GNU 15.2.Rel1 and checks
its actual newlib prerequisite without bypassing the framework assertion.

The eight-SoC hosted job later failed because its NSX-only smoke script globbed
all platform adapters, including Zephyr. Commit `7990dc1` selects `src/ports/nsx.c`
explicitly and retains the 13-source and produced-object guards. All eight SoCs
pass locally against the exact CI SDK `1f18f01555897ed8e06437ff9727f2bb61771e70`.
That commit and `b9b4ccf` bound stalled package downloads; focused independent
review of both CI-only fixes is clean. Copilot's full review precedes these fixes.

At final head `b9b4ccfd406ce5676617e689fa8a6f108ec29241`, the
[Zephyr job](https://github.com/AmbiqAI/nsx-sensors/actions/runs/40404641628) and
all five [existing CI jobs](https://github.com/AmbiqAI/nsx-sensors/actions/runs/40404641799)
pass, including all eight SoCs and real NSX bare-metal/FreeRTOS links. The
firmware job passed on a fresh-runner retry after stalled package installation
was cancelled. Those PR reviews had no unresolved findings at merge; PR #32
was subsequently squash-merged as `f3b313a`. The fresh whole-snapshot audit above
identifies the separate publication-gate defect.
Its source-contract freeze is accepted; these checks still do not establish
release or hardware qualification.
Resource qualification, provenance signoff, final-candidate revalidation and
release metadata remain open. No firmware execution or hardware qualification
is claimed by these builds.

## Accepted RFC source-contract decisions

PR #32 records these choices in the accepted v1 source contract. The following
list is a summary; the contract and public headers are authoritative:

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

The accepted RFC and source contract supersede the original sketches. Historical
PR evidence retains its actual revision and does not establish
qualification of future release candidates.

## Ordered release checklist

PRs #28–#32 are merged. Source acceptance and representative framework integration
are complete; final release qualification remains separate.

1. **Repair publication gating.** Require the separate Zephyr workflow and all
   required integration jobs on the exact release SHA, with offline negative
   tests of the actual gate. Resolve the confirmed P2 finding before publication.
2. **Measure resource costs.** Record per-context/adapter scratch sizes, linked
   flash/RAM and compiler stack-usage evidence on named target/toolchain and
   optimization settings. Include vendor/callback paths; distinguish static
   estimates from runtime high-water marks, which require execution.
3. **Finish provenance and migration review.** Map historical source names to
   current paths, distinguish old release scope from the accepted v1 contract,
   and align NOTICE/PROVENANCE/CATALOG without rewriting original license terms.
   Record required maintainer signoff per OWNERS.md. Automated notice tests and
   technical review are not a substitute for that recorded approval.
4. **Prepare coherent v1 metadata.** Update version.txt, nsx-module.yaml, CMake
   version, changelog, catalog and compatibility claims together in a release PR.
   Verify repository controls tracked by [issue #5](https://github.com/AmbiqAI/nsx-sensors/issues/5).
   No registry repin or publication is implied by preparing these files.
5. **Qualify and publish the exact final candidate.** Rerun offline suites,
   eight-SoC compile matrix, real NSX bare-metal/FreeRTOS links and pinned Zephyr
   tests/cross-build on the final SHA. Record toolchains actually rerun. Publish
   only after required exact-main-commit checks and maintainer approval through
   the immutable workflow. Coordinate downstream registry promotion separately.

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
The fresh driver audit also leaves MAX86150 A_FULL status behavior with external
interrupts disabled for explicit hardware confirmation; the available datasheet
wording and mock evidence did not establish a source defect.
