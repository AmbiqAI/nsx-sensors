# NSX sensor firmware integration acceptance proposal

Status: implemented by [PR #31](https://github.com/AmbiqAI/nsx-sensors/pull/31).
Historical acceptance scope; see the [resulting guidance](../firmware-validation.md) for supported
use or release prerequisites. Original scope and authorization limits below apply
to that proposal, not subsequent work.

## Problem

The shared CMake targets and ELF/map verifier are tested, but the sensor module
has no reproducible final firmware link using real NSX startup, board, HAL,
transport and scheduler dependencies. Synthetic link tests cannot close that gap.

## Scope

Add small integration fixtures, not product examples, for one declared NSX board
and Arm GNU toolchain in bare-metal and FreeRTOS modes. Both consume the existing
root sensor targets; no duplicate driver source lists or RTOS-specific driver
forks. Select and record an immutable SDK revision (including its vendored kernel
and HAL), compiler version, board, mode and sensor revision. Dependencies are
explicit caller-provided paths; ordinary offline host tests remain offline.

## Acceptance criteria

1. Both modes link real board startup/vector table, linker script, HAL and NSX
   I2C/SPI transport implementations. No SDK stubs or fabricated peripheral APIs.
2. All eight portable cores and both NSX binding entry points survive linking;
   the existing ELF/map verifier checks their defining archive members.
3. The FreeRTOS fixture links the real kernel/port and a task entry; application
   locking and delay callbacks respect synchronous bus ownership and microsecond
   waits. Bare-metal uses the same core and adapter targets without the kernel.
4. CPU/FPU/ABI settings propagate from the SDK compilation profile. Capture
   configure/build commands and artifact evidence for reproducibility.
5. A bounded explicit integration command fails on missing prerequisites or
   artifacts. CI runs the links with immutable dependencies where available;
   it does not silently substitute stub builds or skip unsupported prerequisites.
6. Independent review checks these criteria and surrounding integration behavior;
   local Copilot and CI findings are reconciled before the final merge assessment.

## Limits and follow-ups

Build/link evidence does not establish scheduler execution, sensor communication,
waveforms or hardware qualification. Do not flash hardware. T5838's timed board
endpoint/audio handoff, Zephyr wrappers, resource qualification and the v1 release
remain separate work. Any required SDK fix or change in scope is recorded rather
than hidden in a sensor-driver workaround.
