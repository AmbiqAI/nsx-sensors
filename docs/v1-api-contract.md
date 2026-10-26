# v1 source API contract

Status: acceptance and source-freeze decision submitted in PR #32. It takes
effect when that PR is approved and merged; this draft does not assert an
already completed freeze. [RFC 0001](rfcs/0001-portable-sensor-driver-architecture.md)
records the architectural decision. Release acceptance, version metadata and
hardware qualification remain separate.

## Scope and compatibility

The authoritative declarations are the public headers under
[`include/nsx/sensors/`](../include/nsx/sensors/); this document specifies their
shared obligations and deliberate exclusions. The freeze covers the documented
functions, types, enum names/values, configuration/sample fields and behavior in
those headers at the approved PR revision. Device-specific comments and guides
supply the detailed preconditions, units and recovery rules; they are part of
the contract, not optional implementation advice.

V1 promises source compatibility for documented use after recompilation. Breaking
that contract requires an explicit compatibility/version decision. Additive APIs
may extend it. Use zero initialization for contexts and, where provided, the device's
`*_default_config()` followed by named field assignments for configurations;
validate before initialization. A default is not necessarily ready to operate:
INA228 needs application shunt/current values and MAX86150 defaults LED amplitudes
to zero. Do not depend on private fields, vendor internals or raw struct bytes.

There is **no stable binary ABI**, context size/alignment/layout, enum width,
serialized struct format, cross-toolchain archive compatibility or cross-version
object compatibility promise. Recompile callers, cores and adapters together
with matching CPU/FPU/ABI settings. Public typed context storage exists to permit
allocation without a heap; implementation fields are private by convention.
ICM-45605 includes typed `inv_imu_device_t` storage and shipped TDK headers for
allocation, not a promise that TDK internals are application APIs.

The portable C11 API has C++ linkage guards. It does not require NSX, an RTOS,
a universal sensor/channel object, dynamic allocation or a global active device.
The old NSX-bound v0 interface remains on immutable v0 releases; v1 supplies no
legacy aggregate or compatibility aliases. See the [API cleanup
rules](migration/api-cleanup.md) and per-device guides below.

## Headers and build selection

| Public device header (`nsx/sensors/` prefix) | CMake target | Source option / installed component |
| --- | --- | --- |
| `bme688.h` | `nsx::sensor_bme688` | `NSX_SENSOR_BME688` / `bme688` |
| `ina228.h` | `nsx::sensor_ina228` | `NSX_SENSOR_INA228` / `ina228` |
| `mpu6050.h` | `nsx::sensor_mpu6050` | `NSX_SENSOR_MPU6050` / `mpu6050` |
| `max86150.h` | `nsx::sensor_max86150` | `NSX_SENSOR_MAX86150` / `max86150` |
| `icm45605.h` | `nsx::sensor_icm45605` | `NSX_SENSOR_ICM45605` / `icm45605` |
| `ledstick.h` | `nsx::sensor_ledstick` | `NSX_SENSOR_LEDSTICK` / `ledstick` |
| `tsl254x.h` | `nsx::sensor_tsl254x` | `NSX_SENSOR_TSL254X` / `tsl254x` |
| `t5838.h` | `nsx::sensor_t5838` | `NSX_SENSOR_T5838` / `t5838` |

`status.h`, `bus.h` and `runtime.h` define shared contracts;
`nsx::sensor_common` provides the bus helpers. All eight source options default
ON in ordinary CMake builds. Disabled drivers are excluded from compilation.
`nsx::sensors` is an interface aggregate of selected portable cores only.

The actual NSX header is `<nsx/sensors/port_nsx.h>`. Link
`nsx::sensor_port_nsx` explicitly; it is not included by the aggregate.
`NSX_SENSOR_PORT_NSX` defaults ON only when enclosing `nsx::core`, `nsx::i2c`
and `nsx::spi` targets all exist, otherwise OFF. Explicitly enabling it requires
its SDK dependencies. There is no implicit sibling discovery or network fetch.
`NSX_SENSOR_INSTALL_NSX_PORT` defaults OFF; installed `port_nsx` requires a
producer that enabled that export and consumer-loaded SDK bus targets.

Both `add_subdirectory()` and `find_package(nsx_sensors CONFIG REQUIRED
COMPONENTS ...)` are supported. Installed components only exist if built.
Use the [compilation-context contract](cmake-integration.md) for
`NSX_SENSOR_COMPILE_CONTEXT`, automatic `nsx::board_flags` selection and installed
profile prerequisites. A matching profile target name alone does not verify ABI.
Zephyr's module/Kconfig entry point selects these same portable targets; its
framework presentation does not change the ordinary CMake defaults above.

## Ownership and synchronization

Applications own context storage, output buffers, board power, controller
initialization, pinmux, bus settings, interrupt wiring and scheduling. Zero
initialize contexts before first use. Bind is software-only and copies lightweight
descriptors; their ops tables, user bindings and referenced platform storage must
outlive every operation. Keep bound contexts and endpoints at stable addresses;
ICM callbacks and platform bindings may retain self-pointers. Do not copy/move
an active context or rebind a pending operation. Configuration inputs are copied
unless a specific API explicitly documents otherwise.

Serialize **whole driver operations per physical device**, not just individual
register transfers. Two independent contexts must not control one sensor.
Endpoint locks serialize each transaction against all users of the physical
controller, including DMA users. Drivers release the bus before conversion or
calibration delays. No generic synchronous operation is promised ISR-safe; defer
interrupt processing to a worker. T5838 additionally requires serialization of
the entire shared-clock microphone group, including audio/clock transitions.

## Synchronous bus and status contract

`nsx_sensor_bus_t` names a device-bound I2C or SPI endpoint through `ops`/`user`
and inclusive `max_tx_size`/`max_rx_size` limits. A zero limit means that direction
is unsupported. `nsx_sensor_bus_transfer()` validates arguments and limits before
calling the endpoint. Nonempty phases require buffers; zero-length phases ignore
their pointers, including populated pointers. At least one phase is nonempty.

Two nonempty phases mean write-then-read: **I2C repeated START with no intervening
STOP**, or **SPI one CS assertion across both phases**. SPI discards receive bytes
during transmit; the endpoint documents receive MOSI fill or hardware behavior.
This is not simultaneous full duplex. The core supplies protocol bytes, register
widths, page/bank handling and endianness. The endpoint owns address/CS, locking,
alignment/scratch and native error translation. It must not split a transaction,
truncate it, substitute STOP-separated reads or infer a protocol by device name.
Unsupported shapes fail before I/O. A core may deliberately chunk only where its
device protocol permits it.

Callbacks return only after completion and borrow buffers only until return.
Failed transfers may alter hardware and partially fill receive buffers; no
byte-level progress is reported. Async/DMA queue submission that returns before
completion cannot implement this callback contract. An endpoint must supply its
own finite completion bound; portable timeouts cannot preempt a blocked HAL.

`nsx_sensor_status_t` defines `NSX_SENSOR_OK`, `NSX_SENSOR_AGAIN` and
`NSX_SENSOR_E_ARGUMENT`, `_E_IO`, `_E_DEVICE`, `_E_STATE`, `_E_BUSY`, `_E_TIMEOUT`,
`_E_UNSUPPORTED`, `_E_CALIBRATION`, `_E_OVERFLOW`. `AGAIN` means the particular
operation has no available completed result; it is not a successful sample.
Platform native errors belong to the endpoint, not a global errno or a
sensor-specific reinterpretation in the adapter.

The NSX endpoint is `nsx_sensor_nsx_context_t`, created with
`nsx_sensor_nsx_bind_i2c()` or `nsx_sensor_nsx_bind_spi()`; drivers receive its
`bus` member. It supports 256 bytes per direction, with combined TX additionally
limited by `AM_HAL_IOM_MAX_OFFSETSIZE`. SPI nCE is a hardware selector 0..3, not a
GPIO number; I2C addresses are unshifted 0x08..0x77. SPI receive MOSI is
hardware-defined. Lock hooks are paired; absent hooks mean application-guaranteed
exclusive controller access. Shared SPI users must retain compatible fixed mode
and clock settings. `last_sdk_status` records the last invoked SDK helper result;
validation/lock failures leave it unchanged. See [NSX port details](nsx-port.md).

## Timing, outputs and recovery

`nsx_sensor_runtime_t.now_us`, when supplied, is monotonic and must not move
backward during a call. `delay_us` waits **at least** the requested elapsed time,
including sub-tick requests and calls near a scheduler tick. Longer waits are
permitted. Rounding to a positive RTOS tick count alone is insufficient. Hooks
are optional only for operations whose documented preconditions permit it.
Without `now_us`, applicable timeouts bound requested waits, not transfer wall
time. Even with a clock, they do not preempt synchronous I/O; read each operation's
deadline origin and final-attempt rules (notably BME688 `measure`).

Scalar/sample outputs remain unchanged on failure, including `AGAIN`. Batch/FIFO
APIs explicitly report `completed`: it counts successfully published complete
frames (or bytes for the byte API), including work before a later burst fails.
A failed burst publishes none of that burst. This does **not** measure physical
FIFO consumption; a failed transfer may consume unknown extra bytes. Do not
blindly retry ambiguous consuming operations. Follow the device's reset/init
recovery rules. Hardware and context state need not remain unchanged just because
an output does. Status/accumulator reads that clear flags are consuming operations.
Cached requested settings are distinct from hardware readback and neither implies
freshness, atomic multi-register snapshots or effective acquisition rate.

| Core | Init/acquisition and output scope | Recovery / detailed contract |
| --- | --- | --- |
| BME688 | Init sleeps with heater off; explicit forced `start`/`collect` or `measure`; °C, Pa, %RH and raw compensated gas ohms with validity flags | Failed consuming collect may lose freshness while pending; `sleep` cancels before restart. Init/reset failures require init. [Guide](bme688.md) |
| LED Stick | Commands, no invented probe/sample interface | Length-write failure requires init; address change invalidates binding even on success, resolve address then rebind. [Migration](migration/portable-foundation.md) |
| INA228 | Init ADC shutdown; `set_mode` starts; mA, mV, mW, °C, J, C and exact integer 40-bit accumulators | Reset or failed calibration programming/readback invalidates scaling; rejected preflight calibration preserves it. Cached `get_current_lsb` is A/code. [Migration](migration/ina228.md) |
| MPU6050 | Init asleep/FIFO off; explicit wake; raw and g/°/s/°C samples; bounded explicit stationary calibration | Ambiguous FIFO reads require `reset_fifo` then explicit enable; uncertain config writes require init. Calibration failure may leave offsets changed. [Migration](migration/mpu6050.md) |
| MAX86150 | Init shutdown/FIFO off; `start` flushes; raw ECG/PPG frames, no clinical units | Ambiguous FIFO data allows clear/start; failed consuming interrupt reads require init. Continuous capture cannot guarantee loss detection; snapshots omit capture while draining. [Migration](migration/max86150.md) |
| ICM-45605 | Init idle; explicit start/stop; SPI direct raw or g/°/s/°C latest snapshots; explicit calibration | Failed mutation or consuming reset-status read requires init; no FIFO/freshness guarantee. [Migration](migration/icm45605.md) |
| TSL254x | Explicit 2540/2541 variant; init ALS off, start explicitly; visible/IR raw counts, saturation and cached 2541 scale, no lux | Failed mutation or external reset requires init; latest snapshot is not proof of a new conversion. [Guide](tsl254x.md) |
| T5838 | Dedicated synchronous `t5838_control_t` timed-waveform endpoint; init leaves AAD disabled; no I2C/SPI or identity/readback fiction | Recover whole shared-clock group after uncertain writes/clock loss; invalidate caches and fully init. Delivery success does not prove presence. [Guide](t5838.md) |

T5838 waveform durations are CLK cycles, with >50 cycles before/after each frame
(the encoder supplies 60). Ordinary minimum-delay hooks cannot satisfy bounded
pulse timing. Timer/DMA or another qualified endpoint and board clock handoff
remain required; PDM/PCM capture is owned by the audio layer.

## Zephyr endpoint and BME688 wrapper

`<nsx/sensors/port_zephyr.h>` exposes `nsx_sensor_zephyr_context_t`,
`nsx_sensor_zephyr_bind_i2c()`, `nsx_sensor_zephyr_bind_spi()`, status/errno
translation helpers and `nsx_sensor_zephyr_runtime()`. Bind copies the devicetree
spec without I/O or a readiness check; the caller checks controller readiness.
`CONFIG_NSX_SENSORS` enables the module; individual `CONFIG_NSX_SENSOR_*`
selections are opt-in. The devicetree-enabled BME688 wrapper defaults on when its
SENSOR/bus dependencies are met and selects the BME688 core. The Zephyr library
`nsx_sensor_port_zephyr` exists only with I2C or SPI enabled; portable-only cores
can be selected without either controller subsystem. BME688, INA228 and
ICM-45605 require a complete C library for their math functions: an application
using Zephyr minimal libc must explicitly select, for example,
`CONFIG_NEWLIB_LIBC=y` or `CONFIG_PICOLIBC=y`. Configuration rejects these math
cores with minimal libc; Kconfig does not select a replacement automatically.
T5838 control can build without a bus and with minimal libc.

Each direction supports up to `NSX_SENSOR_ZEPHYR_MAX_TRANSFER` (256) bytes.
The endpoint has 1024 bytes of staging scratch; failed transfers preserve caller
RX output. I2C accepts unshifted addresses 0x08..0x77. SPI accepts master,
8-bit, MSB-first, single-line full-duplex Motorola configuration, without
HOLD_ON_CS or LOCK_ON. Receive clocks send zero bytes; one transceive contains
both phases. Endpoint flags do not prove a particular sensor supports every mode:
select the device's required polarity, phase and clock. Thread-context calls and
whole-operation serialization protect scratch and `last_native_status`.
Zephyr controller drivers serialize bus access and must provide finite completion;
these synchronous framework APIs offer no portable per-call timeout override.
The runtime's minimum-delay guarantee is separate from controller completion.

Devicetree compatible `ambiq,nsx-bme688` supports I2C and SPI. Each instance owns
its core, endpoint, mutex and cached sample. `CONFIG_NSX_SENSOR_BME688_ZEPHYR`
provides temperature (°C), pressure (kPa) and humidity (%RH); no gas, attributes,
triggers or async API. It uses core defaults (temperature 2x, pressure 16x,
humidity 1x, filter coefficient 3), heater off and a 1,000,000 µs `measure`
budget. That budget excludes arbitrary mutex/controller blocking and recovery
initialization; it is not a hard end-to-end deadline.

Fetching ALL or any supported channel acquires all three channels and replaces
the cache only on complete success. A failed supported fetch invalidates the
cache and marks the next fetch for reset/reinitialization before acquisition.
A channel get before a successful fetch or after failed fetch returns `-ENODATA`
and preserves output. Unsupported channels (including ALL for channel get)
return `-ENOTSUP`; an unsupported fetch leaves the existing cache intact. Get
uses the cached sample without I/O; a successful get alone does not mean a new
measurement. Framework failures use negative errno values. For custom core
configuration, own a direct core instance instead of reaching into wrapper
storage or concurrently controlling its physical device.

## Platform and release boundary

Plain C/FreeRTOS applications use the same core APIs with application endpoints.
The shared NSX port covers the seven I2C/SPI cores, not T5838 waveform generation.
Zephyr integration in PR #32 provides reusable synchronous I2C/SPI endpoints and
a BME688 sensor wrapper only. Framework pressure is kPa while core pressure is
Pa; wrapper fetch/get semantics, serialization and recovery must preserve the
core's validity rules. Other portable cores are not thereby Zephyr sensor API
wrappers. The proposal pins Zephyr v4.4.0 at
`684c9e8f32e4373a21098559f748f06915f950c9` and manifest dependencies; completed
build/test evidence must be recorded separately from this API decision.

No source freeze proves resource/stack budgets, electrical timing, analog
accuracy, hardware operation or publication readiness. The [readiness audit tracked in separate PR #25](https://github.com/AmbiqAI/nsx-sensors/pull/25) and
[RFC release gates](rfcs/0001-portable-sensor-driver-architecture.md#release-acceptance-gates)
retain those requirements. Version remains 0.3.0 until a separate release change.
Async queues, advanced ICM features, expanded BME688 modes/BSEC/IAQ, other Zephyr
wrappers and T5838 board/audio integration remain separate work.
