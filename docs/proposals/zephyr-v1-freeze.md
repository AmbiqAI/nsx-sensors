# Zephyr integration and v1 source-contract freeze proposal

Status: implementation proposal; final merge approval remains separate.
Baseline: PR #31 merged on main (`149cb78`). No release/tag/version bump is included.

## Intended outcome

Complete the remaining framework integration gate, then reconcile RFC 0001 with
an explicit v1 source contract. Preserve a single driver implementation and one
CMake source/target definition shared by NSX, plain CMake/FreeRTOS and Zephyr.

## Acceptance criteria

1. An external Zephyr module selects portable cores through Kconfig and consumes
   the existing shared CMake definitions. No copied driver source lists, NSX
   dependency, nested project reset or network discovery in normal configuration.
2. Reusable caller-owned Zephyr I2C/SPI endpoints preserve combined repeated-START
   and single-CS transactions, bounded buffers, status translation, instance
   ownership and synchronous completion. Reject unsupported requests before I/O.
3. A devicetree-instantiated BME688 wrapper exposes temperature, pressure and
   humidity through Zephyr fetch/get semantics. Use framework units (pressure
   kPa), serialize device operations, cache only complete successful samples,
   and retain recoverability after failed fetches. No BSEC/IAQ, trigger/async API
   or implied support for every driver through Zephyr's sensor API. Keep core
   configuration available directly; wrapper subset and defaults are explicit.
4. Native/mock-bus tests exercise actual port and wrapper code, transaction
   shapes, errors, units, freshness/recovery and independent instances. A real
   Zephyr Arm cross-build links the wrapper/core. Provision dependencies explicitly
   at Zephyr v4.4.0 commit 684c9e8f32e4373a21098559f748f06915f950c9 and manifest pins;
   record tool versions and separate host tests from framework integration CI.
5. RFC acceptance names the actual headers/targets/defaults; source compatibility,
   context ownership/lifetime, operation serialization, errors/output validity,
   bus/runtime contracts and platform scope. Establish a reviewable v1 API freeze
   decision without declaring release/resource/hardware gates complete. Public
   context binary layout is not a cross-toolchain ABI promise.
6. Update audit #25 with merged #31 evidence and pending/completed proposal
   evidence accurately. Independent review against this proposal, local Copilot,
   CI, finding reconciliation and a final judgment precede merge approval.

## Explicit exclusions

Polished product examples, other Zephyr wrappers, asynchronous command queues,
T5838 board timing/audio, advanced sensor features, resource qualification,
hardware runs and publication/version/registry changes remain separate work.
