# Technical provenance and migration review

Reviewed baseline: merged PR #32, `f3b313a`; qualification proposal follows in
`aa3cb50`. This record reconciles documentation against that local source tree.
Version metadata is still `0.3.0`; v1 remains unreleased. It is a technical
review record, not maintainer release approval or a new legal opinion.

## Evidence and source boundary

Reviewed `NOTICE`, `PROVENANCE.md`, `CATALOG.md`, `RELEASE.md`, the six migration
guides, `docs/v1-api-contract.md`, all eight device headers, shared bus/runtime/
status headers, default-configuration implementations, `CMakeLists.txt`, the
installed package loader and `zephyr/CMakeLists.txt`. The current source map in
PROVENANCE retains the original upstream repositories/revisions and separates
historical v0.1.0 audit pins from current paths. No upstream re-intake or network
verification of those historical hashes was performed in this reconciliation.

The accepted contract covers documented source use after recompilation. Context
storage and typed TDK internals allow allocation, but are not binary-stable or
serialized formats. All eight ordinary CMake core selections default ON;
installed components track built targets. NSX bus adaptation is separate from
the portable aggregate; Zephyr reuses the same core definitions and supplies
only one framework sensor wrapper (BME688). T5838 has its own waveform endpoint.

The Bosch full license remains in `licenses/Bosch-BME68x-BSD-3-Clause.txt` and
its source header. InvenSense terms remain in NOTICE and the vendor headers;
TDK code is not relicensed. Only NOTICE's repository path descriptions changed.
The original Ambiq lineage and recorded vendor modifications are preserved.

## Migration and recovery checks

| Core | Reviewed caller change/default | Recovery boundary checked against current declarations |
| --- | --- | --- |
| BME688 | Legacy transport/status becomes shared bus/runtime/status; bind then init. Defaults T 2x/P 16x/H 1x/filter 3, heater off. | Reset invalidates calibration/configuration. Failed consuming collect can leave a pending request with freshness lost; cancel with sleep before restarting. |
| LED Stick | Binding-only legacy init becomes bind plus explicit length programming/clear. One-based LEDs; brightness 0..31. | Length-write failure requires init per public contract. Any attempted address write invalidates binding; establish the actual address before rebind/init. |
| INA228 | Bind/probe/init separate; application must fill shunt/current defaults. ADC starts shutdown; 1052 us conversions. Cached scale getter is A/code. | Invalid preflight preserves calibration; attempted calibration programming/readback failure or reset invalidates it. Raw counters remain separate from scaled reads. |
| MPU6050 | Renamed `MPU6050_` enum members, explicit wake/calibration; defaults GX PLL, 44 Hz DLPF, ±250 dps, ±2 g, divider 9. | Uncertain config writes require init; ambiguous FIFO consumption requires reset_fifo then enable. Failed calibration may have changed offsets despite unchanged result. |
| MAX86150 | Typed slot/config enums; init shutdown/FIFO off. Default slots LED1/LED2/ECG; amplitudes zero, requested 400 Hz. | Data ambiguity allows FIFO clear/start; failed consuming status reads require init. Published count does not imply physical consumption or lossless acquisition. |
| ICM-45605 | Singleton/NSX wrapper becomes address-stable caller context. Defaults ±4 g/±500 dps, 100 Hz, ODR/4 filters, DRDY disabled; init idle. | Mutation/reset-status uncertainty requires init. Direct snapshots do not prove freshness; explicit calibration requires enabled DRDY and stationary +Z. |
| TSL254x | New core: explicit variant, 1x gain, 36 integration cycles, interrupts off; init asleep. Counts and cached scale are not lux. | Failed mutation/external reset requires init; sticky status needs explicit acknowledgement; snapshot is not a new-conversion guarantee. |
| T5838 | New core: analog AAD configuration defaults, init leaves AAD disabled. Timed CLK-cycle frames are separate from audio and I2C/SPI. | Failed writes/clock loss require invalidation and full init across the affected shared-clock group. Delivery success cannot prove device presence. |

Shared checks: binding is software-only, descriptor backing storage outlives
operations, whole physical-device operations are serialized, and endpoint
completion must be bounded. Minimum-duration delay callbacks alone cannot
establish wall-clock deadlines or T5838 pulse timing. Failed scalar/sample reads
preserve output; FIFO counts describe published results, not unambiguous hardware
consumption. Defaults require application review rather than automatic acquisition.

Confirmed documentation corrections were limited to stale source paths,
historical/current release labeling, eight-core build selection, MPU6050 enum
renaming, and LED Stick recovery wording. Existing per-device migration histories
and attribution were retained. The older API descriptions are historical maps;
this pass does not independently reproduce every old driver revision.

## Remaining release evidence and approval

- OWNERS.md still requires an `nsx-sensors` maintainer review and successful CI
  for the exact tagged commit. This technical record does not provide that approval.
  Vendor changes additionally require licensing review and a provenance update;
  this reconciliation changes no vendor files and does not reopen their intake.
- Successful CI/Zephyr job evidence must be collected for the eventual release
  SHA. Historical v0.1.0 evidence and merged PR #32 are insufficient substitutes.
- Resource results must name toolchain, optimization, target and retention scope;
  static stack frames/call analysis cannot establish runtime stack high water.
- Hardware inventory, board/sensor revisions, electrical transactions, actual
  acquisition rates/quality, interrupt behavior and shared-clock timing remain
  subject to the hardware validation plan. No hardware was exercised here.
- A separate release decision must choose the version/tag and any downstream
  immutable registry promotion. No publication or consumer repin occurs here.

The overall qualification audit should attach executed test/resource/CI results
and final reviewer dispositions. This document records source/document review;
it does not claim test runs, hardware results or independent human signoff.
