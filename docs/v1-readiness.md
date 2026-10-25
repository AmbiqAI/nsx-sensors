# v1 RFC readiness audit

Audited baseline: `e9a36a9dc9922e2da6c395e3a4bce96f20bb9d56` (PR #24).
This is an implementation and release-evidence audit against
[RFC 0001](rfcs/0001-portable-sensor-driver-architecture.md), not a fresh
independent driver review or acceptance of the proposed v1 API. Version metadata
remains `0.3.0`; the breaking portable transition is unreleased.

## Driver inventory

All six cores use caller-owned contexts, the common synchronous bus contract,
pure binding/configuration validation, and explicit initialization. Identity
probing exists where the device supports it; LED Stick has no invented probe.
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

These tests establish software behavior with synthetic devices. No row claims
physical accuracy, successful electrical communication or hardware qualification.
ICM advanced functionality remains in [issue #23](https://github.com/AmbiqAI/nsx-sensors/issues/23).

## Platform and build evidence

| Platform | Available integration | Evidence at audited baseline | Remaining acceptance work |
| --- | --- | --- | --- |
| Plain CMake / application HAL | Six independently selectable cores, common bus/runtime, portable aggregate | Offline tests, ASan/UBSan, source and relocated installed C/C++ consumers; C++ consumer references functions from every driver | Real board HAL/example and resource measurements |
| NSX | Shared I2C/SPI adapter for all six cores | Actual adapter with mocked SDK helpers, 3/5-byte command limits, installed consumers; real SDK configure; all eight declared SoCs compile with Arm GNU | Representative real firmware link on the final release candidate; electrical qualification later |
| FreeRTOS | Portable cores usable through application bus/timing/locking callbacks | No FreeRTOS example or pinned kernel/HAL integration build in this repository | Implement and link a task-based example with correct sub-tick delays and shared-bus serialization |
| Zephyr | Portable C cores only | No Zephyr sensor wrapper, devicetree binding, native simulation or cross-build | BME688 wrapper/example, pinned Zephyr version, mocked-bus/native tests and board cross-build |

Main CI [run 40153003552](https://github.com/AmbiqAI/nsx-sensors/actions/runs/40153003552)
passed all four jobs at the audited baseline. See `.github/workflows/ci.yml`,
`tests/portable_build.sh` and `tests/nsx_port_host.sh` for exact commands.
The SDK pin is `1f18f01555897ed8e06437ff9727f2bb61771e70`, containing the
combined-I2C prerequisite. Historical Armclang/ATfE results are not evidence for
this final six-core revision. SDK-stub consumer links are not real firmware links.

## RFC decisions to record before freezing v1

The implementation resolves several proposed choices; maintainers still need to
accept these as the v1 source contract:

- `<nsx/sensors/port_nsx.h>` is the implemented port header, rather than the
  RFC sketch's `<nsx/sensors/ports/nsx.h>`.
- `nsx::sensors` aggregates selected portable cores only. Applications link
  `nsx::sensor_port_nsx` explicitly. All six drivers default on; the port defaults
  on only when enclosing SDK targets exist. No legacy aggregate remains.
- Context storage is public and typed, private by convention. ICM carries TDK
  headers and must remain address-stable. No stable binary context ABI is promised.
- Transfers are synchronous and bounded by endpoint limits; combined I2C means
  repeated START and SPI means one CS assertion. Unsupported transactions fail
  before I/O. Async command queues remain a separate proposal.
- Runtime hooks are optional only where the device operation permits it. Without
  a clock, documented timeouts bound requested waits, not bus wall time.
- Device-specific lifecycle APIs remain appropriate: FIFO reads and accessory
  commands need not mimic BME688 start/collect. Review naming, units, consuming
  status reads, failure recovery and context lifetime before freezing headers.

The historical RFC implementation notes describe intermediate commits. This
snapshot supersedes them as the current progress inventory, not as an automatic
change to RFC acceptance requirements.

## Ordered release checklist

1. **Accept the API decisions.** Review the choices above and final public
   headers with maintainers; record RFC acceptance or explicit scope amendments.
2. **Finish framework integration.** Add the FreeRTOS example first, then the
   BME688 Zephyr wrapper/example. Select and pin actual kernel/framework, HAL,
   SDK and toolchain revisions; add separate reproducible integration checks.
   A Zephyr wrapper for one driver does not qualify the other five.
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

TSL254x support (issue #15), T5838 integration (issue #12), expanded BME688
operation (issue #14), and ICM advanced features (#23) are separate feature work,
not evidence that the six existing portable migrations remain incomplete.
