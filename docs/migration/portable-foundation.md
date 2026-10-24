# Portable foundation: first RFC implementation slice

This draft implements the first stage of [RFC 0001](https://github.com/AmbiqAI/nsx-sensors/pull/17),
based on the fixed BME688 implementation in PR #16. It intentionally changes
BME688 and LED Stick APIs. It is not a v1 release. Version metadata stays at
v0.3.0 until the coordinated release; do not publish this branch under that tag.

## Current scope and dependency order

BME688 and LED Stick use the same portable bus/status types. BME688 additionally
uses runtime timing hooks. Cores compile without NSX, an RTOS, or a vendor HAL.
No generic sensor object, heap allocation, queues, or framework wrapper is added.

The shared [NSX adapter](../nsx-port.md) supplies endpoints using the genuine
combined I2C operation from SDK PR #130. The TDK per-instance callback work
and migration of the remaining four drivers are still pending. Existing BME688
and LED Stick NSX call sites need the endpoint and lifecycle changes below.

## Shared contracts

Include `<nsx/sensors/bus.h>` for endpoints, `<nsx/sensors/runtime.h>` for timing,
and each device's header. Status values come from `<nsx/sensors/status.h>`.
Contexts copy descriptors, not endpoint storage; ops tables and user bindings
must outlive them. The caller serializes whole operations per physical sensor;
the endpoint serializes physical transactions across devices. Bus locks must
not remain held while waiting for a conversion.

A transfer sends `tx_size` bytes then receives `rx_size` bytes synchronously.
At least one length is nonzero. I2C combined transfers use repeated START;
SPI holds CS across both phases and discards receive data during transmit.
The SPI endpoint documents receive-phase MOSI behavior. Unsupported operations return
`NSX_SENSOR_E_UNSUPPORTED` before I/O. Do not split transactions silently.

Limits are explicit inclusive byte counts; zero means the direction is
unsupported, never unlimited. BME688 requires TX >= 2 and RX >= 23; LED Stick
requires TX >= 5 and does not need RX. Common validation rejects oversized or
invalid requests before invoking a callback. The endpoint retains available transport
diagnostics and returns a shared status. Failed transfers may have hardware effects.

Adapters own alignment, scratch storage and bounded completion. They must use
compatible fixed SPI settings or restore endpoint settings while holding the
bus lock. The current NSX `write_read` helper inserts STOP and does not satisfy
this contract. There is deliberately no fallback that weakens it.

`delay_us` waits at least the requested elapsed interval. `now_us` is optional
and monotonic. BME688 uses elapsed time when supplied, otherwise bounds requested
delays. Neither approach preempts synchronous HAL calls. Oversleep is counted
by elapsed-time budgets; no additional wait is issued after expiry.

## BME688 migration

| Previous API | Portable API |
| --- | --- |
| `#include "nsx_bme688.h"` | `#include <nsx/sensors/bme688.h>` |
| `bme688_transport_t` register callbacks | `nsx_sensor_bus_t` complete transactions plus `nsx_sensor_runtime_t` |
| `bme688_init(ctx, transport)` | `bme688_bind(ctx, bus, runtime)` then `bme688_init(ctx, config)` |
| `bme688_init_i2c` / `bme688_init_spi` | `nsx_sensor_nsx_bind_i2c` / `nsx_sensor_nsx_bind_spi`, then portable bind/init |
| `BME688_OK`, device status enum | `NSX_SENSOR_OK`, `nsx_sensor_status_t` |
| Reset restores default configuration | Reset invalidates state; initialize explicitly before sampling |
| `context.transport_status` | Native diagnostics retained by the endpoint |

Before (PR #16):

```c
bme688_context_t sensor;
bme688_status_t status = bme688_init(&sensor, &transport);
if (status == BME688_OK) status = bme688_measure(&sensor, &sample, 250000);
```

After (bus and runtime supplied by the application's HAL):

```c
#include <nsx/sensors/bme688.h>
bme688_context_t sensor = {0};
bme688_config_t config = bme688_default_config();
nsx_sensor_status_t status = bme688_bind(&sensor, &bus, &runtime);
if (status == NSX_SENSOR_OK) status = bme688_init(&sensor, &config);
if (status == NSX_SENSOR_OK) status = bme688_measure(&sensor, &sample, 250000);
```

Probe reads chip/variant identity and may select a SPI page; it does not reset.
Invalid configuration fails before I/O. Init applies explicit oversampling and
filter settings and leaves the heater disabled. Start/collect and compensation
retain the forced-mode scope. Zero/repeated measurement indices remain valid.

## LED Stick migration

Previously `ledstick_init(ctx, i2c, address)` only stored a binding. Now bind
performs no I/O; init explicitly programs the LED count and clears all colors:

```c
#include <nsx/sensors/ledstick.h>
ledstick_context_t lights = {0};
ledstick_config_t config = { .length = 10 };
nsx_sensor_status_t status = ledstick_bind(&lights, &led_bus);
if (status == NSX_SENSOR_OK) status = ledstick_init(&lights, &config);
if (status == NSX_SENSOR_OK) status = ledstick_set_color(&lights, 1, 32, 0, 0);
```

LED numbers are one-based and bounded by the successfully programmed length.
Brightness outside 0..31 is rejected instead of masked. The length is a nonzero
protocol byte; the application must select a length supported by its hardware.
There is no identity probe because the device has no suitable identity register.

An attempted address-change command invalidates the binding even on success.
The application must establish the resulting address, construct/rebind its
endpoint, and initialize again. On error either address may be active; do not
blindly retry at the old address. Failed length writes also invalidate cached
configuration; set the length successfully or initialize before other commands.

## Build and migration status

Portable default targets are `nsx::sensor_common`, `nsx::sensor_bme688` and
`nsx::sensor_ledstick`. Disable individual drivers with `NSX_SENSOR_*` options.
Only enabled driver headers/libraries are installed. Installed and nested
consumers use the same target names; `find_package` components are `bme688`,
`ledstick`, and `common`. Package exports contain no SDK paths or implicit
repository discovery. Until v1, package version compatibility is exact.

The `NSX_SENSORS_LEGACY_NSX` option builds `nsx::sensors` with the
four remaining NSX-only drivers, exported through the existing `nsxTargets` set.
Portable targets have their own package export set. Both can coexist in one
build; link portable drivers explicitly. The aggregate defaults on when all three
NSX dependency targets already exist, so the normal metadata-driven integration
retains `nsx::sensors`. It defaults off for standalone builds; an explicit option
overrides either default. No filesystem discovery occurs, and board/registry
metadata remains pinned.

Run `tests/portable_build.sh`: synthetic protocol/lifecycle tests, sanitizer
runs, C/C++ linkage, relocated installed package, nested consumer, and driver
selection configurations. `tests/module_compile_smoke.sh` compiles both new and
remaining sources against SDK headers for each declared SoC. These checks do
not prove electrical timing, physical sensor accuracy, or hardware operation.
