# RFC 0001: Portable Sensor Driver Architecture for v1.0

Status: Draft — proposed interfaces, not an implemented contract  
Date: 2026-10-24  
Repository: `AmbiqAI/nsx-sensors`  
Baseline: released v0.3.0 and the BME688 forced-mode proposal in [PR #16](https://github.com/AmbiqAI/nsx-sensors/pull/16)

## Summary

Make `nsx-sensors` a collection of portable C device drivers with shared
transport contracts and small platform adapters. Support NSX, ordinary CMake
applications, FreeRTOS applications using board HALs, and Zephyr. Keep sensor
configuration, calibration, and measurement behavior in one implementation per
device. Platform integrations supply bus access, timing, synchronization, and
framework registration.

Use the v1.0 transition to make intentional source-breaking changes across the
existing drivers. Standardize ownership, status reporting, lifecycle naming,
and transport interfaces; retain device-specific configuration and sample types.
Do not introduce a universal sensor object with a growing set of optional fields.

Acceptance of this RFC authorizes an implementation design, not a release or a
claim of hardware qualification. Version files remain at their current version
until the release process selects the final v1.0 commit.

## Goals and limits

- Compile and link selected driver cores without NSX, an RTOS, or vendor HAL headers.
- Reuse common I2C/SPI bindings across drivers, including command-based accessories.
- Support multiple independently configured instances without required allocation.
- Provide direct, typed operations plus convenience operations where useful.
- Keep register definitions and implementation-only constants private.
- Preserve a clear route to asynchronous transfers and Ambiq command queues.
- Test core behavior and real adapter mappings without physical devices.

V1 does not require a runtime device registry, devicetree outside Zephyr, a generic
channel API for all applications, every Zephyr wrapper, DMA/queue support, BSEC,
or new sensors beyond the agreed migration set. T5838 audio capture belongs in
the audio/PDM architecture; its control integration is a separate decision.

## Current module and migration inventory

The released module has four sensor drivers and one accessory. BME688 adds a
fifth sensor in the draft branch. The current single `nsx::sensors` static target
requires `nsx::core`, `nsx::i2c`, and `nsx::spi` and compiles every driver.

| Driver | Current bus | Current structure | Main migration work |
| --- | --- | --- | --- |
| INA228 | I2C | Context carries NSX bus, address, shunt/calibration state; separate identity validation | Replace transport; preserve calibrated units, raw wide accumulators, and v0.3 error-path fixes |
| MPU6050 | I2C | Native context/config types, raw reads, FIFO and calibration helpers | Remove NSX dependencies; separate bind/probe/init; define FIFO capacity and bounded calibration contracts |
| MAX86150 | I2C | Native context; mixed value-returning, `void`, and status-returning APIs | Make failures distinguishable from valid zero values; define FIFO frames, capacity, overflow and partial-read behavior |
| ICM-45605 | SPI | NSX wrapper around vendored TDK; global device and bus binding | Make state and callbacks instance-specific; retain TDK implementation; change context-free interrupt interface |
| Qwiic LED Stick | I2C | Native command-based accessory | Reuse command transport, standardize errors, define address-change recovery; no artificial measurement API |
| BME688 | I2C/SPI | Proposed portable core, adapted Bosch calculations, device-specific NSX adapters | Adopt shared transport/status/lifecycle; preserve calculation provenance and forced-mode freshness regression |

The BME688 review identified a forced-mode dependency on `sub_meas_index`
advancement that the Bosch datasheet only documents for parallel mode. PR #16 commit
`b600b2d` removes that dependency, drains the previous field before triggering,
and adds I2C/SPI regressions for repeated and zero indices. The regression failed
before the fix; host tests, sanitizers, and GCC compilation for all eight declared
SoCs passed afterward. The fix remains in the draft PR, not a released version.
Hardware behavior has not been measured; evidence remains specification analysis
and synthetic reproduction.

## Architecture and ownership

```text
NSX app             Plain C / FreeRTOS app         Zephyr application
   |                         |                           |
   |                         |                   Zephyr sensor wrapper
   +-------------------------+---------------------------+
                             |
                 Typed portable device drivers
                 configuration, state, conversion
                             |
                 Shared synchronous bus contract
                             |
           NSX adapter / application HAL / Zephyr adapter
                             |
                      Physical bus controller
```

The portable layer owns device protocol: command bytes, register widths,
endianness, SPI read bits/dummy bytes, bank selection, calibration, and device
state transitions. Adapters own transaction execution, chip select/address
binding, alignment requirements, and platform error translation. They never
infer a register protocol from the device name.

The application owns board power, pinmux, bus initialization and speed, interrupt
wiring, scheduling, and sample storage. A framework wrapper can perform those
tasks through its framework's normal APIs. Sensor initialization does not
silently reconfigure a shared bus.

`nsx-sensors` continues to own drivers and adapters, not controller drivers or
board support. Any required `nsx-i2c`/`nsx-spi` change is a separate SDK change.
Update `OWNERS.md` to describe this expanded adapter boundary when implemented.

## Proposed layout and build targets

```text
include/nsx/sensors/
  status.h, bus.h, runtime.h
  bme688.h, ina228.h, mpu6050.h, max86150.h, icm45605.h, ledstick.h
  ports/nsx.h
src/
  common/
  bme688/                  # control, compensation, private headers
  ina228/, mpu6050/, max86150/, icm45605/, ledstick/
ports/
  nsx/                     # reusable I2C/SPI bindings
  zephyr/                  # reusable Zephyr bus bindings
zephyr/
  module.yml, CMakeLists.txt, Kconfig
  drivers/                 # per-device Zephyr sensor wrappers
dts/bindings/              # only bindings needed by the Zephyr integration
vendor/tdk-icm45605/
examples/{baremetal,nsx,freertos,zephyr}/
tests/{core,ports,build}/
docs/{rfcs,migration}/
licenses/
```

Names are proposed; `nsx` is a library namespace, not a mandatory NSX dependency.
Keep vendor sources and private include directories out of public usage
requirements except where an explicitly documented storage type requires them.

| Proposed CMake target | Contents and dependencies |
| --- | --- |
| `nsx::sensor_common` | Portable contracts and small common helpers |
| `nsx::sensor_bme688`, etc. | One portable driver and required calculation/vendor code |
| `nsx::sensor_port_nsx` | Shared adapters; depends on relevant NSX bus targets |
| `nsx::sensors` | NSX convenience aggregate for enabled drivers and NSX adapters |

Provide `add_subdirectory()` support and an installed package such as
`find_package(nsx_sensors CONFIG REQUIRED)`. Driver selection options should
control compilation, not just rely on the linker's dead-code elimination.
The NSX adapter is explicitly selectable and off by default for standalone
builds. The NSX integration opts it in and retains `nsx::sensors`; no sibling
repository discovery or network fetch is part of normal configuration.

```cmake
# Illustrative standalone consumer; platform callbacks come from the app.
set(NSX_SENSOR_BME688 ON CACHE BOOL "" FORCE)
add_subdirectory(third_party/nsx-sensors)
target_link_libraries(app PRIVATE nsx::sensor_bme688)
```

The Zephyr module entry point selects the same core sources without bringing in
NSX dependencies or starting a nested top-level CMake project. Existing NSX
registry metadata remains truthful about its dependencies; Zephyr support is
not advertised until its integration checks pass.

## Rough common API

These declarations illustrate the contract and are not drop-in replacements.
Finalize names and size limits after implementing two different device protocols.

```c
typedef enum {
    NSX_SENSOR_OK = 0,
    NSX_SENSOR_AGAIN,
    NSX_SENSOR_E_ARGUMENT,
    NSX_SENSOR_E_IO,
    NSX_SENSOR_E_DEVICE,
    NSX_SENSOR_E_STATE,
    NSX_SENSOR_E_BUSY,
    NSX_SENSOR_E_TIMEOUT,
    NSX_SENSOR_E_UNSUPPORTED,
    NSX_SENSOR_E_CALIBRATION,
    NSX_SENSOR_E_OVERFLOW
} nsx_sensor_status_t;

typedef enum {
    NSX_SENSOR_BUS_I2C,
    NSX_SENSOR_BUS_SPI
} nsx_sensor_bus_kind_t;

typedef struct {
    const uint8_t *tx;
    size_t tx_size;
    uint8_t *rx;
    size_t rx_size;
} nsx_sensor_transfer_t;

typedef struct {
    nsx_sensor_status_t (*transfer)(void *user,
                                  const nsx_sensor_transfer_t *transfer);
} nsx_sensor_bus_ops_t;

typedef struct {
    const nsx_sensor_bus_ops_t *ops;
    void *user;                  /* Platform endpoint binding. */
    nsx_sensor_bus_kind_t kind;
    size_t max_tx_size;
    size_t max_rx_size;
} nsx_sensor_bus_t;

typedef struct {
    void *user;
    uint64_t (*now_us)(void *user);       /* Monotonic elapsed time. */
    /* Wait at least duration_us of elapsed time; longer waits are permitted. */
    void (*delay_us)(void *user, uint32_t duration_us);
} nsx_sensor_runtime_t;
```

### Transfer semantics

One transfer addresses one bound device. At least one length must be nonzero;
zero lengths permit null pointers. Both lengths nonzero mean write-then-read:
I2C uses a repeated START with no intervening STOP, and SPI holds CS throughout
both phases. Write-only and read-only operations are also supported. On SPI,
receive clocks use an adapter-documented fill byte and bytes received during
the transmit phase are discarded. Simultaneous full-duplex transfer is not
part of this initial contract.

The caller constructs the entire command/payload; this supports BME688's
address/value pairs and LED Stick commands without per-device adapter branches.
A driver handles page boundaries and FIFO protocols. Adapters must not split
a transaction if that changes STOP/CS semantics, nor silently substitute STOP
for repeated START. Unsupported operations return `E_UNSUPPORTED` before I/O.

The current NSX `write_read` implementation issues separate operations. Verify
or add a combined-transfer path in the SDK before claiming the stronger generic
contract. BME688 permits STOP between its pointer write and read, but that
exception must not become the contract for all devices.

Transfer limits are explicit and validated before I/O. Drivers may deliberately
chunk transfers where their device permits it. Adapter scratch buffers must be
aligned/padded as required by the HAL and must not silently truncate FIFO data.
The driver core cannot assume platform alignment of caller buffers.

Callbacks return only after completion. Stack buffers are therefore valid for
the duration of the call. A failed transfer can have device side effects and
partial receive data; cores must not publish partial samples as successful
reads. Adapters retain native diagnostic errors in their own bindings while
returning the common status to drivers.

### Optional facilities and concurrency

Simple devices can omit runtime hooks; operations needing time or delay validate
those hooks before touching hardware. Avoid reserved pointers or a catch-all
options dictionary. Add an extension only after a concrete use case establishes
its semantics and compatibility requirements.

The caller serializes operations on a device context. Adapters serialize each
physical bus transaction against other users, using the platform's bus lock or
an application-supplied policy. A driver does not hold a shared bus lock while
waiting for a sensor conversion. A second context must not independently control
the same physical sensor. No generic synchronous operation is assumed ISR-safe.

Use monotonic time for bounded polling where available. Document transport
blocking limits separately: a driver deadline cannot preempt a synchronous HAL
call. An adapter must have a finite documented completion bound; do not advertise
a hard end-to-end timeout until the controller path can enforce it. Blocking
helpers should accept a timeout rather than use unbounded polling. FreeRTOS
timing adapters must ensure `delay_us` waits at least the requested elapsed
interval, including sub-tick requests and calls near a scheduler tick boundary.
Rounding to a nonzero tick count alone does not guarantee this minimum. Select
an appropriate platform delay or monotonic deadline loop and verify the minimum
delay contract in adapter tests; longer waits are permitted.

## Device contexts and lifecycle

Each device retains its own context. Shared types replace repeated transport
fields; calibration and measurement state remain typed and device-specific.
No required heap and no mutable global device state. Contexts are caller-owned,
zero-initialized, and must remain at a stable address after binding. The context
copies lightweight bus/runtime descriptors; their referenced bindings and ops
tables must outlive it. Configuration inputs are copied unless an API explicitly
documents a retained buffer and its lifetime.

```c
/* Conceptual layout; actual private state is defined by each driver. */
typedef struct {
    nsx_sensor_bus_t bus;
    nsx_sensor_runtime_t runtime;
    bme688_state_t state;        /* Calibration, settings, pending state. */
} bme688_context_t;

nsx_sensor_status_t bme688_bind(bme688_context_t *ctx,
                              const nsx_sensor_bus_t *bus,
                              const nsx_sensor_runtime_t *runtime);
nsx_sensor_status_t bme688_validate_config(const bme688_config_t *config);
nsx_sensor_status_t bme688_probe(bme688_context_t *ctx,
                               bme688_identity_t *identity);
nsx_sensor_status_t bme688_init(bme688_context_t *ctx,
                              const bme688_config_t *config);
nsx_sensor_status_t bme688_set_config(bme688_context_t *ctx,
                                    const bme688_config_t *config);
nsx_sensor_status_t bme688_get_config(bme688_context_t *ctx,
                                    bme688_config_t *config);
nsx_sensor_status_t bme688_start(bme688_context_t *ctx, uint32_t *wait_us);
nsx_sensor_status_t bme688_collect(bme688_context_t *ctx,
                                 bme688_sample_t *sample);
nsx_sensor_status_t bme688_measure(bme688_context_t *ctx,
                                 bme688_sample_t *sample, uint32_t timeout_us);
```

`bind` checks software arguments and attaches transport without traffic.
`validate_config` is pure. `probe` is available after binding, reads identity,
and does not reset or apply configuration; minimal protocol side effects such
as SPI page selection are documented. `init` validates, probes, resets where
the device requires it, loads calibration, and applies explicit configuration.
Each driver's docs state these side effects and its failure recovery path.

Successful init leaves the device configured in a documented idle state.
Continuous sampling starts explicitly. Calibration that requires stillness or
other physical conditions is an explicit operation, not an init side effect.
Reset invalidates configuration/calibration caches as required; init restores
them. Do not silently resume measurements after reset.

Use `status + output pointer` consistently. Scalar/sample outputs remain
unchanged on failure. FIFO/batch APIs use explicit capacity and completed-count
outputs; the completed count reports only complete frames successfully published
to the caller, including any published before a later transfer fails. Drivers
must separately document hardware consumption and recovery. A failed transfer
may consume an unknown number of FIFO bytes, since the transport returns status
without byte-level progress. Do not promise exact consumption on failure; require
device-specific resynchronization or FIFO reset when necessary before reading
again. No generic transport progress field is required for v1.
Register reads that clear flags are named/documented as consuming operations.
Getters must distinguish actual readback from cached requested settings.

Sample types document units and validity. Keep raw and compensated types
distinct. Preserve INA228 integer accumulator access; do not make float the
only representation for wide counters. A device without useful identity
registers, such as a command accessory, need not invent a successful `probe`.
FIFO devices need `read_fifo` rather than artificial start/collect methods.

For allocation without a heap, public context definitions may include a typed
implementation-state header whose fields applications must not access. Avoid
casting arbitrary byte arrays to vendor structs. The v1 promise is documented
source compatibility after recompilation, not a stable binary context layout
or serialized struct format. Decide the TDK storage representation before
freezing headers; hidden state cannot be achieved through a global singleton.

## Platform bindings and application usage

Shared NSX bindings contain the NSX bus pointer and I2C address or SPI chip
select. They contain no BME688 register limits, calibration, or initialization
calls. Constructors validate a binding and produce `nsx_sensor_bus_t` without
device I/O. Bus configuration such as clock and SPI mode is already applied by
the application and validated against the sensor's documented requirements.

The initial NSX SPI binding supports fixed controller settings: every user of a
shared controller must accept the configured mode and clock, and no user may
change them while these bindings are active. Transaction serialization alone
does not restore settings changed by another user. Supporting devices with
different settings requires a platform endpoint that records each device's
configuration and restores it under the same physical bus lock held through
the transfer. Such configuration belongs in the platform binding, not the
portable sensor context. Until that adapter exists, incompatible SPI settings
on one controller are unsupported.

```c
#include <nsx/sensors/bme688.h>
#include <nsx/sensors/ports/nsx.h>

/* Illustrative: all objects retained while the sensor is in use. */
nsx_sensor_i2c_binding_t endpoint = { .bus = &app_i2c, .address = 0x76 };
nsx_sensor_bus_t bus;
bme688_context_t sensor = {0};
bme688_config_t config = bme688_default_config();
nsx_sensor_status_t status = nsx_sensor_i2c_bind(&endpoint, &bus);
if (status == NSX_SENSOR_OK) status = bme688_bind(&sensor, &bus, &app_runtime);
if (status == NSX_SENSOR_OK) status = bme688_init(&sensor, &config);
/* Handle status, then start/collect or use the blocking convenience API. */
```

A bare-metal app includes the device and common headers and implements the bus
ops using its HAL. FreeRTOS uses the same core and build targets; its integration
supplies timing, locks, and task/ISR scheduling. An example port can demonstrate
this without claiming one FreeRTOS adapter can drive every MCU's peripherals.

Zephyr supplies its own bus bindings and wraps the core with its sensor API.
Devicetree creates immutable configuration and one runtime context per instance;
Kconfig selects wrappers. Use standard channels/units at the Zephyr boundary
and explicitly convert from the portable driver's documented units. Never
bind both an upstream Zephyr driver and this wrapper to the same node/device.
Select and test supported Zephyr versions and binding/Kconfig names before
publishing the wrapper.

Start with one BME688 wrapper using Fetch/Get: fetch updates wrapper-owned
sample storage; get reads that storage. Read/Decode is a later integration with
its own asynchronous submission and self-contained decode-buffer requirements;
it is not a synchronous callback relabeled asynchronous. See the official
[sensor API](https://docs.zephyrproject.org/latest/hardware/peripherals/sensor/index.html),
[devicetree guide](https://docs.zephyrproject.org/latest/build/dts/howtos.html), and
[external module guide](https://docs.zephyrproject.org/latest/develop/modules.html).

## Driver-by-driver migration

| Driver | Proposed v1 changes | Required regression evidence |
| --- | --- | --- |
| BME688 | Replace `bme688_transport_t` with shared bus/runtime; remove `nsx_bme688_nsx.*`; separate bind/probe/init; use common statuses; preserve forced-mode scope and adapted math | Fresh forced samples with repeated/zero index, stale-data prevention using documented semantics, I2C/SPI protocol, reset failures, calibration vectors, gas validity, bounded waits |
| INA228 | Map binding-only old `init` to `bind`; replace hardware `validate` with `probe`; introduce explicit init config; retain shunt/current-LSB state, signed charge and wide raw readings | Preserve all v0.3 tests; ADCRANGE/calibration changes, side-effect registers, conversion readiness, failed writes and raw counter precision |
| LED Stick | Shared I2C command transfers; retain output commands; validate ranges and propagate errors; bind without claiming identity detection | Exact command byte sequences; transaction bounds; successful address change followed by explicit endpoint rebind; failed/ambiguous address write recovery |
| MPU6050 | Portable bus and timing; typed raw/physical sample APIs; explicit calibration; capacity-aware FIFO reads and per-context scale settings | Burst decoding, signed axes, scale/rate/filter settings, FIFO overflow/partial reads, reset/timeouts, two independent devices |
| MAX86150 | Convert value-returning getters and void setters to status/out APIs; typed FIFO frames with caller-owned buffers; explicit read-to-clear interrupt semantics | Error versus zero distinction, FIFO slot ordering, frame capacity, overflow, partial transfers, enabled measurement channels and interrupt reads |
| ICM-45605 | Keep TDK code; replace file-static device/bus/CS binding with per-instance transport state; context-aware deferred interrupt processing; separate raw and scaled types; explicit calibration | Two devices on one/different buses, callback routing, failure propagation, signed raw data, units, DRDY setup, interrupt-to-worker handoff |

ICM-45605 is an early feasibility gate, not a late cleanup. Its current TDK
callback signatures lack a user context. Investigate a context-aware upstream
version or a narrow, documented vendor transport patch. Do not implement a
process-global "current device" switch to imitate per-instance support. Vendor
changes require the repository's licensing review and provenance update.
If the constraint cannot be resolved, explicitly revise the RFC before v1.

### Application migration examples

| v0.x usage | v1 migration |
| --- | --- |
| `#include "nsx_ina228.h"` | Include `<nsx/sensors/ina228.h>` and the chosen platform port header |
| `ina228_init(ctx, bus, addr)` | Construct endpoint, bind common transport, bind device, then explicit init/config |
| `ina228_validate(ctx)` | `ina228_probe(ctx, &identity)` |
| `uint8_t id = max86150_get_part_id(ctx)` | `status = max86150_get_part_id(ctx, &id)`; handle failure |
| `icm45605_handle_interrupt()` | GPIO ISR schedules work for a specific context; worker reads/clears device status |
| `bme688_init_i2c(ctx, binding)` | Common I2C binding plus `bme688_bind` and `bme688_init` |
| Every app links `nsx::sensors` | Existing NSX aggregate remains available; portable apps link selected device targets |

Do not keep ambiguous old signatures under the same names. Publish a migration
guide with complete before/after examples. Keep immutable v0.3.x releases
available; old applications retain their version pins until deliberately
migrated. No automatic registry repin or silent app-vendored source replacement.
Temporary compatibility headers, if needed by a named consumer, must be an
explicit opt-in with a removal plan rather than part of the core design.

## Implementation sequence and release gates

1. Resolve BME688's review finding and establish a regression test. Agree on
   this RFC's ownership, API, source compatibility, and v1 support claims.
2. Prototype shared transport with BME688 and LED Stick, covering both I2C/SPI
   and non-register command payloads. Assess ICM-45605 callback feasibility in
   the same phase. Confirm the NSX combined-I2C transaction path.
3. Add portable per-driver CMake targets, install/export support, and shared
   NSX adapters. Keep existing consumers working until each driver migration
   and its migration example land together.
4. Migrate INA228, MPU6050, MAX86150, and ICM-45605 in focused changes. Preserve
   retained functionality; document deliberate changes rather than silently
   dropping FIFO, calibration, or interrupt features.
5. Add a standalone application, an NSX application, a FreeRTOS integration
   example, and a Zephyr BME688 wrapper/example to exercise the architecture.
6. Freeze the documented API after review, publish the migration/compatibility
   matrix, update release metadata coherently, and release an immutable v1.0
   through the repository's normal review and release workflow.

### Foundation implementation status

[Draft PR #18](https://github.com/AmbiqAI/nsx-sensors/pull/18), stacked on #16,
implements the first portable foundation slice. It migrates BME688 and LED Stick
to the shared transaction/status types, separates BME688 lifecycle operations,
and adds selectable CMake targets, package exports and a fake-HAL consumer.
The following choices are exercised by the prototype and remain reviewable
before the v1 API freeze:

- Headers use `include/nsx/sensors/`; portable targets use the names above.
- TX/RX limits are inclusive; zero means that direction is unsupported.
  BME688 requires TX >= 2 / RX >= 23 bytes; LED Stick requires TX >= 5 bytes.
- Runtime `now_us` is optional. BME688 uses elapsed polling budgets when supplied,
  otherwise documented requested-delay budgets; synchronous calls remain bounded
  by the endpoint's own completion guarantee.
- BME688 reset invalidates configuration/calibration until explicit init. LED
  Stick init programs length and clears colors; address changes invalidate the
  binding on success or error and require application-directed recovery.
- Both portable drivers default on and are independently selectable. Installed
  packages export enabled components and use exact version compatibility during
  the pre-v1 transition. Public contexts retain typed implementation fields for
  caller-owned allocation; callers must not access internal state.

Local evidence includes host protocol/lifecycle/failure tests, ASan/UBSan,
40 existing Python contracts, C/C++ consumer links, nested/relocated installation,
single-driver/common-only builds and GCC compilation for all eight declared
SoCs. Hardware, Zephyr, and representative NSX firmware links remain pending.

PR #18 removes the BME688-specific adapters and retains the remaining four
NSX drivers in a transitional aggregate. The shared NSX port is implemented
in [PR #19](https://github.com/AmbiqAI/nsx-sensors/pull/19), stacked on #18.
Following independent review, commit `7e86936` enables that aggregate by default
when all declared NSX dependency targets already exist; standalone builds keep
it off. The default NSX consumer test no longer forces an option override.
The transition is not a drop-in update for NSX apps: applications explicitly
link each portable driver and `nsx::sensor_port_nsx`, then bind/init contexts. Existing applications retain their immutable version pins. This is
implementation progress, not acceptance of the full RFC or a v1 release.

The SDK prerequisite is now implemented in
[draft SDK PR #130](https://github.com/AmbiqAI/nsx-ambiq-sdk/pull/130), commit
`40b893d`: `nsx_i2c_write_read_combined` uses a single IOM instruction-plus-RX
transaction with repeated START and final STOP. It supports HAL-defined short
prefix limits (3 bytes on Apollo3/3p, 5 on newer supported IOMs), validates bounds
and RX alignment before I/O, and preserves native HAL errors. Existing
STOP-separated helpers are unchanged. Local evidence includes 1587 SDK tests
passed (172 skipped), recording-HAL tests for both instruction layouts,
ASan/UBSan, and real-header C/C++ GCC compilation for all eight I2C SoCs.
Waveform and physical recovery qualification remain pending.

The shared adapter uses that combined operation and enforces its prefix limits.
Caller-owned endpoint contexts retain addresses/nCE selectors, optional IOM
lock hooks, aligned 256-byte staging buffers, and the last SDK helper status.
SPI reads use one instruction/RX transaction; MOSI behavior during RX is
hardware-defined rather than a selectable fill byte. Legacy helpers do not
always retain native HAL errors, so the adapter records their available result
and maps failures to `E_IO`. Its separate package export keeps portable consumers
independent of the SDK. Host sanitizer tests cover both prefix limits, failed
locks/transfers, padding, actual portable-driver calls, and relocated C/C++
consumers; all module C sources and the port C++ header compile on eight SoCs.
The SDK prerequisite must land before the shared adapter. The remaining four
driver migrations, TDK per-instance feasibility, firmware links, and hardware
qualification remain release gates.

### Delivery plan

Use focused implementation PRs against this RFC. PR #16 supplies the native
BME688 baseline and PR #17 carries this proposal; neither implements the v1
migration. Do not merge either PR merely to create the implementation branches.
Branches may stack on the needed prerequisite and clearly identify their base.

| Change | Concrete scope | Completion evidence / dependency |
| --- | --- | --- |
| Portable foundation and first two drivers | Add `include/nsx/sensors/{status,bus,runtime}.h`; move BME688 into `src/bme688/`; port LED Stick commands into `src/ledstick/`; implement bind/validate/probe/init where applicable; add per-driver CMake targets and package exports | No NSX headers in selected portable targets; preserve Bosch vectors and freshness tests; exact LED command bytes, bounds and address-change recovery; two independent instances; linked add-subdirectory and installed-package consumers |
| SDK combined I2C operation | Add a documented synchronous write/read operation in `nsx-ambiq-sdk`, preserving existing helpers until consumers migrate; define size limits, bus ownership, failure cleanup and completion bounds | HAL descriptor tests for repeated START and final STOP; error-path recovery; supported-SoC compile checks; waveform confirmation remains a hardware task |
| TDK instance feasibility | Introduce a narrowly scoped context-aware transport patch or qualified upstream replacement; preserve `inv_imu_device_t` transport layout assumptions; route read/write/delay through each instance | Two devices route all callbacks independently, including indirect register access and delays; vendor tests and provenance/license review; no global current-device switch |
| Shared NSX port | Add `ports/nsx/` and `include/nsx/sensors/ports/nsx.h`; replace BME688-specific adapters; provide endpoint validation, scratch limits/alignment, native error retention and bus serialization policy | Depends on combined I2C support; real adapter tests assert transaction semantics, bounds, failure mapping and fixed SPI settings; NSX example links against explicit SDK revision |
| Remaining driver migrations | Migrate INA228, MPU6050, MAX86150 and ICM-45605 in individually reviewable changes; update each application's migration example with its driver | Preserve INA228 precision/regressions; FIFO capacity and recovery tests; explicit calibration/timeouts; ICM depends on the instance feasibility change |
| Framework integration and release preparation | Add bare-metal/FreeRTOS examples, Zephyr BME688 wrapper, platform coverage matrix, resource measurements and final metadata updates | Pinned integration builds and representative links; all acceptance gates below; v1 release only after API review |

The first implementation slice should prove BME688 and LED Stick with fake
transports and plain CMake while the SDK and TDK prerequisites are developed.
It must not make a STOP-separated NSX helper appear to satisfy the new combined
transfer contract. The existing BME688 adapter remains a v0 implementation until
the shared NSX port can replace it with tested semantics.

Current source inspection supports the TDK patch approach: transport helpers
already receive an `inv_imu_transport_t *`, but read/write/delay callbacks discard
that instance information. Add an explicit user pointer and propagate it through
all callback sites; keep typed vendor storage per device. This is a proposed
implementation choice, not a completed feasibility result.

The build transition also changes tests: the current standalone configuration
intentionally fails without `nsx::core`. Replace that expectation with a portable
build-and-link test, and retain the missing-dependency check only when the NSX
port is explicitly enabled. Keep the nested-project guard, explicit source
lists, license installation and release-version coherence checks. Test both
portable-only and NSX integration configurations throughout migration.

Keep the existing v0 API available on its immutable releases. Transitional
implementation branches must state which drivers are migrated and which still
require NSX; do not label an aggregate portable until every selected driver is.
Do not automatically repin the NSX registry or downstream vendored applications.

V1 acceptance requires:

- Offline per-driver tests for protocol, conversions, errors, bounds, side
  effects, and independent instances; adapter tests execute actual port code.
- Host sanitizer runs; C/C++ public-header checks; no platform headers needed
  for any selected portable target.
- Build-and-link consumers using both `add_subdirectory` and installed targets,
  referencing driver functions so static archive omission cannot hide errors.
- NSX supported-target compile matrix and representative real firmware link;
  FreeRTOS example link with a declared HAL/toolchain; Zephyr native simulation
  or mocked-bus tests plus a cross-build of the wrapper/example on pinned versions.
- CI that can build portable subsets without network or private dependencies;
  separately provisioned SDK/framework integration jobs with immutable pins.
- License/provenance checks, documented resource/stack costs, migration examples,
  and no unresolved consequential review findings.

Publish a per-driver/per-platform matrix separating portable-core availability,
framework-wrapper availability, build/link evidence, and hardware qualification.
One Zephyr wrapper does not imply every device supports Zephyr's sensor API.
No physical sensor is currently available for this work; analog accuracy,
electrical timing, FIFO/interrupt behavior, power, and heater performance remain
explicit hardware qualification tasks. API stability is a different claim.

## Follow-up work and open decisions

Command queues belong in a separate proposal after two or more drivers use the
synchronous contract. An asynchronous submission API needs descriptors with
device/transaction boundaries, completion results, buffer lifetime/alignment,
cancellation, bus ownership, and suspend/resume rules. Device-specific planning
must account for page changes, conversion waits, and data-dependent operations;
a list of register addresses alone is insufficient. Never return early from a
synchronous callback and populate its output later.

Decisions to settle before implementation acceptance:

- Final header/target naming and driver selection defaults.
- Required transfer sizes and whether an explicit capability is needed for
  STOP-separated reads or full-duplex SPI; keep unsupported cases explicit.
- NSX combined-I2C support and controller completion bounds.
- Public context storage representation, especially for retained TDK state.
- Exact supported Zephyr/FreeRTOS integration versions and v1 wrapper scope.
- Whether any real consumer needs an opt-in v0 compatibility package.

Alternatives considered: keep all code NSX-bound (simpler now, duplicates future
drivers); adopt Zephyr types in the core (makes other environments depend on its
framework); introduce a universal sensor/channel object (premature for this small
mixed collection); rewrite every vendor implementation (unnecessary maintenance
for complex vendor code). The proposal instead shares protocol implementations
and transport contracts while allowing framework-specific presentation.
