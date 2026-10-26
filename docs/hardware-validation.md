# Hardware validation and existing-driver regression protection

This is a plan, not completed hardware evidence. The available sensor/breakout and
Ambiq board inventory is still to be supplied. No board has been flashed as part
of release qualification. Keep device-specific configuration and board wiring out
of the portable driver implementation.

## Establish a reproducible baseline

For each available device, record the breakout/board revision, sensor variant,
controller and pins, supply, pull-ups/CS, bus frequency/mode, SDK/compiler,
application and sensor-module SHA, selected options, and measurement units.
Archive firmware, configuration and raw logs. Capture a known-good application
before changing its vendored sensor snapshot. Do not overwrite its working
vendored module or silently repin the application registry.

Use an isolated application copy/branch for the candidate. Keep board, wiring,
SDK, compiler and sensor configuration the same where feasible. The v0-to-v1 API
migration is deliberately source-breaking; apply documented API migration in the
candidate harness, and separately list intentional corrections/default changes.
Do not interpret every old output as a correctness oracle or hide unexplained
behavior differences behind the migration.

The first campaign should use the simplest device already available with a
known-good reference application. Prefer a register sensor with repeatable input
(e.g. stationary MPU6050, BME688 environmental acquisition, or TSL254x under fixed
illumination) over beginning with complex FIFO timing or T5838 clock handoff.
Hardware availability determines the actual order; this is not a purchase list.

## Common progression

1. Verify board wiring/controller configuration independently. Read device identity
   where supported; LED Stick/T5838 must not gain an invented identity API.
2. Initialize using an explicitly recorded configuration; read back owned settings
   where supported. Verify one acquisition/control operation before loops, FIFO,
   interrupts or concurrency. Record raw and scaled values together.
3. Run repeated acquisitions with stable input and compare distributions, freshness,
   units and status results against baseline and independent references where
   available. Choose tolerances from the actual part datasheet and fixture accuracy,
   not from arbitrary equality of floating-point samples.
4. Repeat initialization, stop/start, and documented recovery. Test an absent device
   or wrong address first for transport failures. Power/reset injection needs a
   fixture that prevents back-powering through bus pins; document the fixture.
   Confirm invalid output is not accepted as a successful new measurement.
5. Exercise shared-bus operation with two independent devices and serialized whole
   operations per physical device. Compare bare-metal and FreeRTOS with the same
   acquisition/configuration; framework differences must not change units/protocol.
6. Capture logic-analyzer traces for combined I2C repeated START, SPI CS continuity,
   reset/conversion waits and interrupt/FIFO timing. A successful read alone cannot
   establish these transport contracts.
7. Restore the baseline firmware/configuration and repeat a short sanity test.
   Keep a regression record for every discrepancy: trigger, raw data, expected
   contract, first differing transaction, intentional change or confirmed bug.

## Device-specific coverage

| Core | Minimal useful test | Follow-on regression checks |
| --- | --- | --- |
| BME688 | Chip/variant, forced temperature/pressure/humidity; heater disabled initially | Raw vs compensated values, fresh conversions, error recovery, optional gas validity and heater timing; no BSEC/IAQ claim |
| MPU6050 | Identity, stationary gravity/gyro and temperature with explicit range/filter/rate | Six orientations, changes invalidate FIFO interpretation, FIFO frame order/alignment/overflow, DRDY and opt-in calibration; do not overwrite saved calibration during first bring-up |
| MAX86150 | Identity/configuration readback and raw FIFO ordering using a repeatable fixture | PPG/ECG slot layout, full/empty/overflow, consuming interrupt reads, shutdown/drain/restart, producer interleaving and partial-transfer recovery; explicitly verify A_FULL status when interrupt output is disabled |
| INA228 | Identity, bus/shunt voltages against a meter using known shunt and low-current bench load | Positive/negative current, range/calibration readback, accumulator reset/sign/scaling, configuration changes and saturation; record actual shunt value/tolerance |
| ICM-45605 | Identity, SPI direct samples, stationary gravity/gyro with explicit ranges | Start/stop/reset recovery, DRDY, axis signs/scaling, opt-in calibration and independent instances; advanced/FIFO features are outside the implemented subset |
| TSL2540/2541 | Explicit variant identity and raw visible/IR counts under fixed light | Gain/integration changes, saturation, thresholds/read-clear configuration and TSL2541 factory scaling; calibrated lux is not part of this core |
| LED Stick | Known length, single LED/color/brightness, then bounded arrays | Index boundaries, length changes and recovery; postpone address changes until a known recovery route is available |
| T5838 | Board-specific timed THSEL endpoint and waveform capture before sensor writes | Guard clocks/pulse widths, AAD mode/threshold/WAKE, two-device shared-clock serialization and programming/PDM handoff; PDM/PCM capture belongs to audio integration |

## Acceptance and change discipline

A device is software-tested, board-communicating, functionally measured, or
qualified only for the explicitly recorded configuration. Do not infer one level
from another. No available device makes the untested devices hardware-qualified.

For any driver correction discovered during bring-up: preserve the original trace,
add a host fault/protocol regression where practical, make the smallest scoped
change in its own reviewed PR, rerun that driver's tests and all common bus/adapter
and portable suites, and rerun the existing hardware baseline before accepting it.
Maintain immutable known-good firmware snapshots. Driver source/API changes are
not part of the publication/resource/documentation work in PR33.

Before a v1 release, retain the existing per-driver host tests, ASan/UBSan,
selected-driver and source/installed C/C++ consumer checks, enum/ABI profile tests,
NSX adapter contracts, eight-SoC compilation, real NSX bare-metal/FreeRTOS links,
and Zephyr mocked-controller tests/cross-link. Hardware testing adds evidence;
it does not replace those regression gates.
