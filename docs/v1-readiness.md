# v1 software readiness audit

Current production snapshot: `fdabd7756068dd2f8586d610c4489236d41b4883`
(PR #41), 2026-10-28. All nine cores are integrated. This refresh reconciles
reviewed changes and new resource evidence; it is **not a new full-source audit
of every driver**, a hardware qualification, or publication approval.

The current contracts live in [the API contract](v1-api-contract.md),
[catalog](../CATALOG.md), public headers and device/migration guides. Keep this
file as the readiness summary; PRs hold review discussions and completion logs,
and dated resource artifacts hold measurements.

## Reviewed implementation baseline

The full-snapshot audit at `f3b313a773f3e2eb39fc16fc340fbd8c94ae75bf` ([PR #32](https://github.com/AmbiqAI/nsx-sensors/pull/32))
used fresh reviewers for API/lifecycle, driver correctness and build/integration.
The source API freeze was accepted by its approved merge. Subsequent reviews
cover these deltas rather than claiming the current sources are unchanged:

| Change | Evidence and result |
| --- | --- |
| Publication checks, resource qualification and bounded simulation | [PR #33](https://github.com/AmbiqAI/nsx-sensors/pull/33), [PR #34](https://github.com/AmbiqAI/nsx-sensors/pull/34); both exact-main workflows required by publication; eight-core evidence remains dated |
| v1 metadata, release controls and finalization | [PR #35](https://github.com/AmbiqAI/nsx-sensors/pull/35), [PR #37](https://github.com/AmbiqAI/nsx-sensors/pull/37), [PR #38](https://github.com/AmbiqAI/nsx-sensors/pull/38); publication remains a separate approval |
| NSX consumer metadata declaration | [PR #39](https://github.com/AmbiqAI/nsx-sensors/pull/39); Zephyr support declared for the consumer schema |
| TDK namespace/header isolation | [PR #40](https://github.com/AmbiqAI/nsx-sensors/pull/40); generic vendor headers private, symbols device-prefixed; source/installed C/C++ coexistence checks; no driver logic/register changes |
| Native ICM-42670-P | [PR #41](https://github.com/AmbiqAI/nsx-sensors/pull/41); basic I2C/SPI core, shared integration, independent reviews and local Copilot; reset/restart timing finding fixed and regression-tested |

The [historical eight-core audit](https://github.com/AmbiqAI/nsx-sensors/blob/fdabd7756068dd2f8586d610c4489236d41b4883/docs/v1-readiness.md)
retains original findings and reproductions. Their dates, test counts and source
revisions must not be relabeled as current evidence.

## API, contracts and integration

All cores use caller-owned state and explicit lifecycle/configuration. Bus-based
cores share synchronous bus/runtime descriptors: combined transfers require
repeated START on I2C or continuous CS on SPI. Failed output reads preserve
caller storage; each device documents consuming reads, partial mutations and
recovery. Applications serialize whole device operations; ports own bus locking.
No heap allocation, scheduler or global device instance is required by the cores.
T5838 uses its separate timed control endpoint, not a register bus.

Root CMake is the source of truth for production targets and source lists.
Plain CMake and application-managed FreeRTOS use those targets with supplied
callbacks. NSX adds its common bus adapter; bare-metal and FreeRTOS use the same
sensor cores. Zephyr selects the same root targets through Kconfig and adds its
adapter; only BME688 currently has a native Zephyr sensor wrapper. There is no
separate FreeRTOS driver implementation or automatic task/ISR ownership.

The major version deliberately breaks pre-v1 source use. Public headers,
explicit binding/recovery and per-instance configuration replace old singleton
interfaces. Recompile applications and libraries together; binary ABI, context
layout/size and serialized structs are not promised. See [migration guidance](migration/api-cleanup.md).

| Core | Current supported scope | Hardware status |
| --- | --- | --- |
| BME688 | Temperature, pressure, humidity, raw gas resistance; explicit measurement/heater configuration | Unqualified; no BSEC/IAQ claim |
| LED Stick | Qwiic accessory configuration and LED commands | Unqualified |
| INA228 | Register configuration, voltage/current/power/energy/charge and temperature | Scoped AP510B identity/configuration/temperature smoke only; no calibrated electrical measurement |
| MPU6050 | Configuration, raw/scaled acquisition, FIFO and explicit calibration | Unqualified |
| MAX86150 | Configuration and FIFO acquisition of PPG/ECG channels | Unqualified; no physiological metrics claim |
| ICM-45605 | Basic SPI low-noise direct samples, DRDY and explicit calibration | Unqualified; advanced/FIFO features excluded |
| ICM-42670-P | Basic I2C/SPI low-noise raw/scaled accel/gyro/temperature, DRDY, explicit start/stop | Unqualified; FIFO/APEX/low-power/self-test/I3C excluded |
| TSL254x | TSL2540/2541 raw ambient-light channels and configuration | Unqualified; no calibrated lux claim |
| T5838 | Acoustic-activity control sequencing through a timed endpoint | Unqualified; audio capture and board timing endpoint remain separate |

## Current software evidence

Both push/main workflows passed on production SHA `fdabd7756068dd2f8586d610c4489236d41b4883`:
[CI](https://github.com/AmbiqAI/nsx-sensors/actions/runs/40761540506) and
[Zephyr integration](https://github.com/AmbiqAI/nsx-sensors/actions/runs/40761540414).
These establish evidence for that SHA; the audit PR and any future publication
candidate need their own checks.

- Nine host suites, individual-driver/none selections, source and relocated
  installed consumers, C/C++ headers, vendor isolation, sanitizers and short enums.
- Ten compiling negative controls across nine cores, with restored clean controls.
  The new control restores the actual ICM-42670-P reset/restart timing defect.
  Models use datasheet-derived expectations, but cannot prove physical behavior.
- 75 Python contract tests, metadata validation, NSX bus-adapter and compilation
  context checks, and all eight declared SoCs compiled with Arm GNU.
- Real NSX Apollo4p bare-metal and FreeRTOS final links, with implementation and
  startup/HAL/kernel provenance checks. These links are not execution on boards.
- Five Zephyr native tests, Arm cross-link/provenance including ICM-42670-P, and
  a busless T5838 build. The new fifth test requires matching evidence counts;
  the stale four-test checker found during PR #41 was corrected.

### Nine-core resource refresh

[Resource qualification](resource-qualification.md) contains the current table,
reproduction command and linked raw/JSON evidence. Clean measurement source
`0a2dbb612027c85d9b4a40791f19c1cc393ce74c` differs from `fdabd77` only in the
runner's explicit production-baseline pin. All nine contexts were measured in
portable, NSX and Zephyr profiles using the same pinned dependencies as the
original eight-core report.

The eight existing context/standalone flash sizes and both adapter layouts/object
sizes are unchanged. ICM-42670-P adds a 44-byte context, a 1850-byte standalone
complete-API link and a largest individual compiler frame of 72 bytes. The
nine-core retained link is 30892 bytes, 1606 above the old aggregate; shared
runtime code means individual costs are not additive. These numbers are not
application RAM budgets, runtime stack high-water or worst-case call-chain bounds.

## Remaining gates

1. Review and merge this evidence refresh, recording its exact reviewed/merged
   SHAs and successful CI in its PR. No sensor implementation changes are proposed.
2. Select the final publication candidate and obtain explicit maintainer approval
   of scope/migration/provenance and the hardware-unqualified release claim.
   Recheck mutable repository controls and run the exact-main publication gates.
3. Only after separate publication authorization, dispatch the protected release
   workflow and verify immutable tag, source archive and checksum. Downstream
   registry promotion remains a separate action.
4. Qualify hardware and real application behavior, including regression against
   known-good existing-driver applications, wire timing, calibrated measurements,
   shared-bus recovery and runtime stack high-water. [Hardware plan](hardware-validation.md);
   ICM-42670-P qualification/advanced scope is [issue #42](https://github.com/AmbiqAI/nsx-sensors/issues/42).

**No driver is hardware-in-the-loop qualified.** The INA228 bench smoke recorded
in [audit PR #25](https://github.com/AmbiqAI/nsx-sensors/pull/25) is deliberately
narrow and does not change that statement. Hardware qualification may follow a
software API release only with the limitation retained. Examples and advanced
features should follow application needs rather than expand this release scope.
The [release checklist](releases/v1.0.0.md) owns publication approvals and controls.
