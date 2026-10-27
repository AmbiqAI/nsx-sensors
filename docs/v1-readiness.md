# v1 RFC readiness audit

Current merged snapshot: `c7707fa6accc58b0f4eb796a4aaf331d45f25144` (PR #34),
2026-10-27. This refresh reconciles merged qualification PR #33 (`29c6315`) and
simulation PR #34 with the earlier audit. It is not a new full-source audit.

The full-snapshot audit at `f3b313a773f3e2eb39fc16fc340fbd8c94ae75bf` (PR #32),
2026-10-26, used three fresh reviewers with API/lifecycle, driver and
build/integration emphases, plus coordinating release-control checks. Subsequent
reviews covered the qualification and simulation deltas, including fixes and
final evidence. Production driver/header/vendor/CMake/Zephyr implementation is
unchanged from that full-audit baseline. No hardware qualification is claimed.

All eight cores, shared CMake integration and the v1 source-contract decision are
merged. Approval and merge of PR #32 accepted the source API freeze. Version
metadata remains `0.3.0`; release qualification and publication are not complete.

## Resolved publication finding

**Closed by merged PR #33 — publication previously ignored Zephyr CI.**
The following reproduction describes the original `f3b313a` defect, not current
main. The fix and verification are recorded below.

At the original baseline,
[release.yml at the audited revision](https://github.com/AmbiqAI/nsx-sensors/blob/f3b313a773f3e2eb39fc16fc340fbd8c94ae75bf/.github/workflows/release.yml#L85)
queries only `ci.yml`, then allows publication. The independently running
`zephyr.yml` can fail, remain pending, or be absent without affecting this gate.
That permits publication without the Zephyr final-candidate evidence required by
RFC 0001. This blocked release readiness, not acceptance of the source API.

The coordinator extracted the actual gate shell unchanged and executed it with
an offline `gh` stand-in: exact-SHA push/main CI succeeded and Zephyr was modeled
as failed. The gate returned success and never queried Zephyr. The build reviewer
independently confirmed the source path and requirement mismatch after recording
its own initial candidate ledger. No tag or release was created.

The required correction was successful runs of both workflows on the exact release SHA, push event
and main branch, plus successful required jobs. Behavioral tests were required for absent,
failed, cancelled, pending and skipped integration checks, and an all-success
control, including the real firmware job in the explicit job list as skip protection;
the old overall `ci.yml` success already rejected an ordinary firmware-job failure.

## Merged qualification: PR #33

[PR #33](https://github.com/AmbiqAI/nsx-sensors/pull/33) merged as
`29c6315c190b98432c0fac8f6d2a12989978c4ba`, completing the publication fix,
static resource measurements and technical provenance/migration review. Reviewed
PR head was `b93a315cc0a572d2d2afbd52035ac293662b95c8`; maintainer release
signoff remains separate from technical completion.

- Publication now checks both exact-SHA push/main workflows, latest runs/current
  attempts, all required jobs, complete pagination and bounded waiting. Offline
  positive/negative tests exercise the actual gate. A fresh independent gate
  review found no blocking issue. Copilot's publication timing finding was fixed
  by rerunning the gate in the publish job immediately before tag creation/retry;
  a failing gate is exercised through the actual workflow shell.
- Resource evidence records all eight context sizes, both adapter/scratch layouts,
  complete-API retained flash, framework fixture costs, maps/commands and compiler
  stack frames. Independent full reproduction matched all layouts/footprints and
  portable ELF hashes exactly. A fresh run from clean `12609fe` additionally
  checks pinned clean SDK, Zephyr, CMSIS and CMSIS 6 trees before/after building,
  addressing Copilot's external-input finding. Final artifact hashes and sizes
  were independently checked. Largest frames/source paths are not worst-case
  task stacks; runtime high-water and actual controller/ISR paths remain open.
- NOTICE paths, historical/current provenance, eight-core catalog, release policy
  and migration defaults/recovery were reconciled. Independent technical review
  found no lost license text or consequential migration discrepancy. OWNERS
  maintainer approval is still separate; no legal signoff is manufactured.
- Existing portable/selected/installed-consumer and ASan/UBSan, NSX adapter, ABI
  profile and INA228 suites pass. All 73 Python tests and all six hosted CI checks
  pass at this head. Production driver/header/vendor/CMake/Zephyr trees are
  unchanged from the audited baseline. Focused independent and local Copilot
  reviews found no consequential issue in the fixes. Copilot's final artifact
  supplement at `b93a315` verified all four pins, 45 input hashes, archive
  integrity and 10 portable ELF hashes with no consequential findings.

The [hardware plan at that revision](https://github.com/AmbiqAI/nsx-sensors/blob/b93a315cc0a572d2d2afbd52035ac293662b95c8/docs/hardware-validation.md)
starts with a preserved known-good application/firmware snapshot, explicit
configuration, identity/readback, stable-input acquisition and isolated recovery
checks before FIFO/concurrency/clock handoff. Hardware cannot readily be added
at present. No hardware was flashed and no existing driver behavior changed.

## Merged simulation qualification: PR #34

[PR #34](https://github.com/AmbiqAI/nsx-sensors/pull/34) merged as
`c7707fa6accc58b0f4eb796a4aaf331d45f25144`. Reviewed head was
`c2f446a2b7abd42dd1e1944046c8b4779ad7f328`, with all six CI checks passing.
It adds bounded time-driven FIFO/model checks and before/after-effect init
failure/recovery tests, retaining existing coverage where sufficient.

Nine compiling mutations across eight cores were rejected; restored controls
passed. Actual pre-migration INA228 and current source agreed on 22 independently
asserted vectors. This is not a side-by-side comparison of every former driver.
Independent review reproduced these checks and eight sanitizer tests, and rejected
three separately selected model corruptions. Local Copilot found no consequential
findings. Sources, scenario assumptions and limitations are recorded in
[sensor simulation](sensor-simulation.md). No physical accuracy, electrical
waveform or real-time scheduling qualification follows from these models.

## Original full-audit evidence and subsequent closure

The following observations are from `f3b313a`. PR #33 closes the publication,
static-resource and technical documentation gaps; PR #34 adds simulation evidence.
Maintainer signoff and final-release-candidate qualification remain open.

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
  gate finding above was the confirmed build/release defect, now fixed by PR #33.
- Static resource/stack evidence was missing at the full-audit baseline and is
  now recorded in [resource qualification](resource-qualification.md) by PR #33.
  Runtime high-water and worst-case call-chain budgets remain unqualified.
- PR #33 corrected stale NOTICE paths, catalog scope and historical/current
  provenance labeling. Original license text remains intact. Technical review
  is complete; recorded maintainer provenance approval remains separate.
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
identified the separate publication-gate defect subsequently fixed by PR #33.
Its source-contract freeze is accepted; these checks still do not establish
release or hardware qualification.
At that PR #32 baseline, resource qualification, provenance signoff,
final-candidate revalidation and release metadata remained open. The current
checklist below supersedes that historical status. No firmware execution or hardware qualification
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

PRs #28–#34 are merged. Source API acceptance, representative framework
integration, publication-gate repair, static resource evidence, technical
provenance/migration review and bounded regression simulation are complete.
No unresolved consequential software finding remains from these reviews.

1. **Record maintainer release/provenance signoff.** Confirm the current scope,
   source mappings and retained notices under OWNERS.md. Technical review and PR
   merge approval do not by themselves record the final release signoff.
2. **Prepare coherent v1 metadata.** Update version.txt, nsx-module.yaml, CMake
   version, changelog, catalog and compatibility claims together in a release PR.
   Verify repository controls tracked by [issue #5](https://github.com/AmbiqAI/nsx-sensors/issues/5).
   No registry repin or publication is implied by preparing these files.
3. **Qualify and publish the exact final candidate.** Rerun offline suites,
   eight-SoC compile matrix, real NSX bare-metal/FreeRTOS links and pinned Zephyr
   tests/cross-build on the final SHA. Record toolchains actually rerun. Publish
   only after required exact-main-commit checks and maintainer approval through
   the immutable workflow. Coordinate downstream registry promotion separately.

Hardware qualification is a separately disclosed limitation of any software API
release, not an automatically completed result or a newly imposed blocker.

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
