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
combined I2C operation from SDK PR #130. TDK callbacks and ICM-45605 have now
been migrated to per-instance state; all six cores are portable. Existing BME688
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

Portable default targets are `nsx::sensor_common`, `nsx::sensor_bme688`,
`nsx::sensor_ledstick`, `nsx::sensor_ina228`, `nsx::sensor_mpu6050`, and `nsx::sensor_max86150`. Disable individual drivers with `NSX_SENSOR_*` options.
Only enabled driver headers/libraries are installed. Installed and nested
consumers use the same target names; `find_package` components are `bme688`,
`ledstick`, `ina228`, `mpu6050`, `max86150`, `icm45605`, and `common`. Package exports contain no SDK paths or implicit
repository discovery. Until v1, package version compatibility is exact.

All six cores are now portable. The transitional `NSX_SENSORS_LEGACY_NSX`
option is removed. `nsx::sensors` is an interface aggregate of selected driver
targets and ships in the same portable package export. It exists with or without
NSX. Link `nsx::sensor_port_nsx` separately for NSX endpoints; only that adapter
requires SDK targets. No filesystem discovery occurs, and registry metadata
remains pinned. See [ICM-45605 migration](icm45605.md) for the final legacy port.

Run `tests/portable_build.sh`: synthetic protocol/lifecycle tests, sanitizer
runs, C/C++ linkage, relocated installed package, nested consumer, and driver
selection configurations. `tests/module_compile_smoke.sh` compiles both new and
remaining sources against SDK headers for each declared SoC. These checks do
not prove electrical timing, physical sensor accuracy, or hardware operation.

## INA228 follow-up

INA228 now uses `nsx::sensor_ina228` and `<nsx/sensors/ina228.h>` with the shared
NSX port. Its binding-only legacy init is replaced by bind/probe/validate/init;
see [INA228 migration](ina228.md) for calibration, units, and recovery semantics.
ICM-45605 is now portable as well; no NSX-only driver remains.

## MPU6050 follow-up

MPU6050 now uses `nsx::sensor_mpu6050` and `<nsx/sensors/mpu6050.h>` with shared
bus/runtime descriptors. Initialization ends asleep; wake and calibration are
explicit. See [MPU6050 migration](mpu6050.md) for sample units, bounded operations,
FIFO format/overflow/partial-read recovery, and the NSX example.

MAX86150 now has an independently selectable portable target and shared NSX
endpoint support. Its migration includes a native-driver audit; see the
[MAX86150 migration guide](max86150.md) for corrected FIFO/error/decoding behavior,
continuous versus snapshot acquisition, and remaining hardware checks.
