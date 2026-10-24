# Shared NSX sensor transport

Link `nsx::sensor_port_nsx` alongside the portable driver targets. Include
`<nsx/sensors/port_nsx.h>` and the device header, such as
`<nsx/sensors/bme688.h>`. The same endpoint type supports BME688, LED Stick, INA228, MPU6050,
and future drivers using the common synchronous bus API.

This unreleased adapter requires the combined-read addition in
[SDK PR #130](https://github.com/AmbiqAI/nsx-ambiq-sdk/pull/130).
CI pins SDK commit `1f18f01555897ed8e06437ff9727f2bb61771e70`.
SDK v5.2.24 does not provide that API. The SDK prerequisite has merged. Repin to its released revision during the
coordinated v1 integration.
No fallback to the old STOP-separated `nsx_i2c_write_read` is provided.

## Binding and lifetime

The application initializes the SDK controller, selects compatible fixed bus
settings, and supplies any runtime timing hooks. Bind performs no I/O:

```c
#include <nsx/sensors/bme688.h>
#include <nsx/sensors/port_nsx.h>

/* Application-owned storage; keep all objects alive while the driver is used. */
static nsx_sensor_nsx_context_t endpoint;
static bme688_context_t sensor;

nsx_sensor_status_t start_sensor(nsx_i2c_config_t *initialized_i2c,
                                const nsx_sensor_runtime_t *runtime)
{
    /* NULL locks: this application guarantees exclusive access to the IOM. */
    nsx_sensor_status_t status = nsx_sensor_nsx_bind_i2c(
        &endpoint, initialized_i2c, 0x76, NULL);
    if (status == NSX_SENSOR_OK)
        status = bme688_bind(&sensor, &endpoint.bus, runtime);
    bme688_config_t config = bme688_default_config();
    if (status == NSX_SENSOR_OK) status = bme688_init(&sensor, &config);
    return status;
}
```

For SPI, use `nsx_sensor_nsx_bind_spi(&endpoint, initialized_spi, nce, locks)`.
`nce` is the IOM hardware chip-select selector 0..3, not the GPIO pin number.
Board pinmux must route that selector to the sensor. The adapter does not change
SPI mode, speed, or pinmux. Do not change the controller settings behind it.
For LED Stick, bind an I2C endpoint, then call `ledstick_bind` and
`ledstick_init` as described in the [migration guide](migration/portable-foundation.md).

A context contains its bus descriptor, SDK configuration pointer, endpoint
address/selector, optional lock hooks, last SDK status, and 256 bytes of aligned
scratch storage. It owns no heap memory. Do not move or copy an endpoint after
a driver has bound to it. Rebinding requires exclusive access and rebinding the
driver too; it is not an in-flight address-change operation.

## Transactions and concurrency

- TX-only and RX-only operations use the corresponding SDK helper.
- I2C command/read operations invoke `nsx_i2c_write_read_combined` once, with
  repeated START between phases and a final STOP.
- SPI command/read operations pack the command MSB-first into the instruction
  field of one `nsx_spi_read`. CS remains asserted through the read.
- Each direction accepts at most 256 bytes. Combined command prefixes are
  additionally limited to `AM_HAL_IOM_MAX_OFFSETSIZE`: three bytes on Apollo3/3p,
  five on the other supported SoCs. Longer requests fail before I/O; there is
  no splitting, automatic retry, full-duplex exchange, or queue submission.
- SPI RX uses the IOM read phase. MOSI is hardware-defined rather than a chosen
  fill byte; Apollo3's datasheet section 8.15.4 describes MOSI as no longer
  driven after the address. Devices requiring a specific dummy byte need a
  different adapter. The BME688 read protocol does not require such a byte.

Optional acquire/release hooks must be supplied together and protect **all
users of the physical IOM**, including other endpoints and DMA/queue users.
A failed acquire returns its status without calling the SDK or release hook.
Without hooks, the application guarantees exclusive IOM access. Calls are not
ISR-safe. Separately serialize complete driver operations per physical sensor;
a bus lock cannot protect a multi-call register read/modify/write by itself.
The adapter releases the bus after each transaction, before conversion delays.

Scratch storage supplies word alignment and TX tail padding. RX is copied to
the caller only after success. A failed transaction may still consume hardware
data or change device state. The adapter maps any SDK failure to `E_IO` and
retains the helper's result in `last_sdk_status`; legacy helpers collapse some
native errors to 1, so this field is **not always a native HAL error code**.
Validation and acquire failures preserve the last SDK result. Read diagnostics
under application serialization. HAL blocking waits/recovery determine timeout
behavior; there is no caller deadline or cancellation API here.

## Build and verification

`NSX_SENSOR_PORT_NSX` defaults on when the enclosing NSX build already supplies
`nsx::core`, `nsx::i2c`, and `nsx::spi`. It defaults off standalone and can be
explicitly enabled with I2C/SPI dependency targets supplied. No sibling checkout
is discovered. Link the port and each desired portable driver explicitly;
`nsx::sensors` still contains the two unmigrated legacy drivers.

Firmware builds do not emit an install export for the port. Package producers
explicitly enable `NSX_SENSOR_INSTALL_NSX_PORT=ON` and must supply the SDK's
install export closure (or preinstalled imported SDK targets). This avoids
requiring SDK package exports in ordinary source-based NSX applications.
When enabled, the port has a separate install export so portable consumers do
not acquire SDK dependencies. Installed consumers load their SDK targets first,
then use:

```cmake
find_package(nsx_sensors CONFIG REQUIRED COMPONENTS bme688 port_nsx)
target_link_libraries(app PRIVATE nsx::sensor_bme688 nsx::sensor_port_nsx)
```

`tests/nsx_port_host.sh` exercises real adapter code with mocked SDK helpers,
both prefix limits, error/lock paths, alignment/padding, unchanged failed RX,
portable BME688/LED Stick calls, and relocated C/C++ package consumers.
`tests/module_compile_smoke.sh` compiles the adapter against real SDK headers
for all eight declared SoCs. These tests do not establish wire-level timing or
fault recovery on hardware. Repeated START, CS continuity, sensor sampling,
and bus fault recovery remain hardware qualification tasks.
