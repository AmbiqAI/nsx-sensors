# RFC 0001: Portable Sensor Driver Architecture for v1.0

Status: accepted by approval and merge of PR #32 on 2026-10-26
(`f3b313a`). The v1 source contract is frozen; release qualification is separate.
Original proposal: 2026-10-24; decision revision: 2026-10-26.
Repository: `AmbiqAI/nsx-sensors`.

## Decision

Adopt the implemented portable, typed, caller-owned driver architecture and
[the v1 source API contract](../v1-api-contract.md). That document and the public
headers replace this RFC's original rough C declarations, illustrative paths,
target names and intermediate migration inventories. The original proposal
remains in git history; its sketches are not alternative supported interfaces.

Approval and merge of this decision froze the documented source contract at
the approved revision. It does not release v1, bump version metadata, qualify
hardware or waive the remaining release gates. Version stays 0.3.0 until the
release process selects and validates the final v1 commit.

## Architecture and ownership

Keep one portable implementation per device. Device cores own register/command
protocol, byte order, page/bank selection, configuration, calibration, conversion
and device state. Platform endpoints own synchronous transaction execution,
address/CS binding, alignment/scratch, controller serialization, bounded
completion and platform error translation. Wrappers translate framework APIs
without duplicating device protocols.

Applications own power, pins, controller setup/settings, interrupts, scheduling
and storage. They serialize whole operations on each physical sensor. Bus locks
protect individual transactions against every controller user and are released
before conversion waits. A second context must not independently control the same
physical sensor. No generic synchronous API is assumed ISR-safe.

Contexts are caller-owned, typed and private by convention, without mandatory
heap allocation or mutable global device selection. Retained descriptors and
platform storage must outlive operations; bound storage remains address-stable.
ICM-45605 owns typed TDK storage per instance with context-aware transport
callbacks. Its vendor headers are necessary storage details, not public vendor
API promises. Freeze means documented source compatibility after recompilation,
not a stable binary layout, serialized context format or cross-toolchain ABI.

The shared synchronous bus requires repeated START without intervening STOP on
combined I2C transfers and one CS assertion across SPI phases. Unsupported shapes
fail before I/O; no splitting or STOP substitution. Transfers borrow buffers only
until return and failures can consume data or alter hardware. FIFO APIs report
published complete units, not exact physical consumption after errors. Runtime
minimum delays include sub-tick waits; portable deadlines cannot preempt a
synchronous HAL. Exact bounds and recovery obligations live in the contract and
per-device headers.

## Resolved choices and scope amendments

| Original proposal / open choice | Accepted implementation choice |
| --- | --- |
| `ports/nsx.h` sketch and device-specific adapters | `<nsx/sensors/port_nsx.h>` and `nsx::sensor_port_nsx`; shared reusable endpoint, no protocol inference |
| Aggregate including NSX adapters | `nsx::sensors` aggregates selected portable cores only; link the port explicitly |
| Unspecified selection defaults | All eight ordinary CMake core options ON; NSX port ON only when enclosing `nsx::core`, `nsx::i2c`, `nsx::spi` exist; port install export OFF by default |
| Target/package spelling | `nsx::sensor_common`, `nsx::sensor_<device>`, installed `nsx_sensors` package and named components; see contract for exhaustive mapping |
| TDK instance feasibility | Implemented context-aware callbacks and typed per-instance storage; no singleton/current-device switch |
| Common lifecycle sketch | Device-specific bind/probe/init/acquisition APIs; no invented probe for LED Stick/T5838, no forced start/collect abstraction for FIFO devices |
| Six-device migration set | Eight implemented cores: BME688, LED Stick, INA228, MPU6050, MAX86150, ICM-45605, TSL254x and T5838 control |
| T5838 boundary | Dedicated synchronous timed-waveform AAD control is included; PDM/PCM capture, board timing endpoint and shared-clock handoff qualification remain audio/board work |
| Zephyr scope/version | PR #32 BME688 wrapper plus shared I2C/SPI endpoints; Zephyr v4.4.0 commit `684c9e8f32e4373a21098559f748f06915f950c9` and manifest pins; no implied wrappers for other cores |
| FreeRTOS scope | Portable cores with application HAL/timing/locks; pinned real SDK/kernel link fixtures from PR #31, not a universal MCU HAL adapter |
| Compatibility package | No v0 shim/legacy aggregate; immutable v0 releases remain available and migration guides describe source changes |
| Async/full-duplex/STOP-separated capability | No extension to the synchronous v1 contract; a separately justified proposal is required |

The [compilation-context contract](../cmake-integration.md) propagates one
CPU/FPU/ABI profile through cores, vendor code, adapters and consumers. Both source
and installed consumers are supported; profile target existence alone is not ABI
validation. Zephyr selects the same root targets through its module/Kconfig entry
point. Normal configuration neither discovers sibling SDKs nor fetches dependencies.

## Implementation and evidence

All eight cores have migrated. The migration guides, public headers and
[readiness audit tracked in separate PR #25](https://github.com/AmbiqAI/nsx-sensors/pull/25) replace the obsolete per-stage status notes
previously appended here. BME688 forced-mode freshness does not require advancing
parallel-mode indices; MAX86150 consuming status failures invalidate configuration;
MPU6050 names and MAX86150 configuration selections are typed/prefixed; INA228
provides cached current scale; T5838 frames have >50 clock cycles on both sides.
These are implemented source behaviors, not hardware evidence.

PR #31 merged real NSX bare-metal and FreeRTOS firmware link fixtures against a
pinned SDK. PR #32 merged Zephyr integration and accepted this source-freeze
decision. An implementation or synthetic link does not alone satisfy a final
release-candidate gate. Record test/review results at their actual revisions in
the audit; do not infer completion from acceptance of architecture.

## Release acceptance gates

These remain separate from this RFC/source-freeze decision:

- Offline per-driver tests for protocol, conversions, errors, bounds, consuming
  side effects and independent instances, executing actual adapter code.
- Host sanitizers, C/C++ headers, portable subset builds and linked source and
  relocated installed consumers that reference actual driver functions.
- Supported-SoC compile matrix and representative real NSX firmware link;
  FreeRTOS integration link with declared kernel/HAL/toolchain; Zephyr native or
  mocked-bus tests and a real wrapper cross-build on pinned dependencies.
- Separate provisioned SDK/framework integration CI; ordinary portable tests
  require no network or private dependencies. Record exact revisions and actual
  tool versions rerun on the final release candidate.
- Per-context/adapter scratch sizes, linked flash/RAM, stack costs and their
  target/toolchain/optimization settings, distinguishing static estimates from
  measured runtime high-water marks.
- License/provenance and maintainer review, migration documentation, no unresolved
  consequential review findings, and repository release controls.
- A separate coherent release change for version files, module metadata,
  changelog/catalog and compatibility claims, immutable publication after final
  commit CI, and separately coordinated downstream registry updates.

Publish a per-driver/platform matrix distinguishing core availability, wrapper
availability, compile/link evidence and hardware qualification. Product-quality
examples may follow integration fixtures; the fixtures still need actual pinned
framework links and must disclose runtime limitations.

Physical devices are unavailable. Waveforms, repeated START/CS continuity on real
controllers, FIFO/interrupt behavior, analog accuracy, calibration, power and
heater performance remain explicit deferred hardware work. This limitation must
remain visible on any API release. Neither source stability nor CI qualifies it.

## Deferred work and rationale

Async command queues require a separate lifetime/completion/cancellation contract
and evidence from multiple devices. Never implement them by returning early from
a synchronous callback. Additional Zephyr wrappers, advanced ICM functionality,
expanded BME688 modes/BSEC/IAQ, calibrated application lux and T5838 board/audio
integration remain separate scope.

The selected architecture avoids duplicating protocols across NSX and Zephyr,
forcing framework types into portable cores, or inventing a universal sensor
object with optional fields. Keeping vendor implementation code where appropriate
avoids an unnecessary rewrite while preserving instance isolation and notices.
