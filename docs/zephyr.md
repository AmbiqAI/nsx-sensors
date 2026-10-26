# Zephyr integration

The external module reuses the root CMake portable targets and their source lists.
It provides reusable synchronous I2C/SPI endpoints plus one Zephyr sensor wrapper:
BME688 temperature, pressure and humidity. Other selected cores are available
through their typed C APIs; they do not acquire Zephyr sensor wrappers implicitly.
See [validation](zephyr-validation.md) for the exact framework pin and evidence.

## Module and device selection

Add this checkout to `ZEPHYR_EXTRA_MODULES`, or include it in the application's
West manifest. Module discovery uses `zephyr/module.yml`; no NSX SDK is required.
Enable `CONFIG_NSX_SENSORS` and the desired `CONFIG_NSX_SENSOR_<DEVICE>` choices.
Unlike plain CMake's all-on defaults, Zephyr cores are opt-in. A configured BME688
wrapper selects its core. T5838 core selection needs no I2C/SPI controller.
BME688, INA228 and ICM-45605 need full math support: choose a supported full libc
(such as newlib or picolibc) for embedded builds. The module rejects minimal libc
for those cores with an actionable configuration error.

For the wrapper, enable `CONFIG_SENSOR`, the appropriate bus, and
`CONFIG_NSX_SENSOR_BME688_ZEPHYR`. The integration-specific compatible
`ambiq,nsx-bme688` selects this implementation without taking over upstream Bosch
bindings. Assign one driver to each physical device; never describe the same
address/chip select twice to run both upstream and NSX implementations.

For example, beneath an application-configured I2C controller:

```dts
bme688: sensor@76 {
    compatible = "ambiq,nsx-bme688";
    reg = <0x76>;
    status = "okay";
};
```

The SPI binding additionally uses the standard `spi-max-frequency` and chip-select
properties. Board pinmux, bus speed, power, reset wiring and controller readiness
remain application/framework responsibilities.

## BME688 fetch/get contract

Initialization uses the portable core defaults with the gas heater disabled.
`sensor_sample_fetch()` performs one bounded forced conversion, serializing the
whole device operation. Supported fetch channels are ALL, AMBIENT_TEMP, PRESS,
and HUMIDITY; any supported fetch refreshes all three measurements together.
`sensor_channel_get()` returns one cached channel without I/O: Celsius, kPa, or
percent relative humidity, respectively. Unsupported channels return `-ENOTSUP`.

Before a successful fetch, and after a failed supported fetch, channel reads
return `-ENODATA`. Failed reads preserve the caller's output. A failed fetch
invalidates the cached sample; the next fetch reinitializes the core before
acquiring again, resolving pending conversions and uncertain consuming reads.
No partial new sample is published. A mutex protects each instance's core and
cache; it does not make a caller's separate fetch/get sequence atomic against
another caller. Applications needing that sequence must serialize it themselves.

The wrapper exposes no attributes, triggers, gas resistance, IAQ, VOC or BSEC.
Applications requiring explicit oversampling/heater control can use the typed
core through a Zephyr endpoint instead, without registering a wrapper for that
same physical sensor. Wrapper initialization failure follows Zephyr device
readiness semantics; it is not a hot-plug recovery facility.

## Direct portable cores and endpoints

Include the device's `<nsx/sensors/<device>.h>` and
`<nsx/sensors/port_zephyr.h>`. Allocate a zero-initialized, address-stable
`nsx_sensor_zephyr_context_t`, bind it to an I2C/SPI DT spec, and pass its `.bus`
and `nsx_sensor_zephyr_runtime()` to the portable driver. I2C bind copies the
spec; SPI bind borrows the caller's `const struct spi_dt_spec`. Neither performs
I/O; referenced framework devices must remain alive and ready.

Zephyr SPI controllers may cache the configuration pointer and skip applying
settings when its address is unchanged. Keep each SPI spec/config immutable at
a stable address for as long as the controller can retain that pointer, even
after endpoint rebind or destruction; `static const` storage is recommended.
To change settings on a quiescent endpoint, bind a distinct persistent spec.
Do not overwrite or reuse a previously cached configuration's address, and do
not assume `spi_release()` clears the controller's configuration cache.
Check every status. Do not move or share an endpoint without serialization.

Each direction supports up to 256 bytes. Combined I2C uses repeated START with a
final STOP; SPI uses one synchronous full-duplex transaction, discards RX during
the command prefix and sends zeroes while receiving. The adapter stages buffers
so RX is not published on failure. Its two 512-byte arrays are caller-owned;
there is no dynamic allocation. Register format, page selection and read bits
remain in each core.

SPI supports master, Motorola framing, 8-bit, MSB-first, single-line operation.
Loopback, half duplex, held CS, bus-lock retention and unsupported word/line modes are
rejected before I/O. Invalid requests do not overwrite `last_native_status`;
it records the last invoked native transfer result. Status helpers translate
between the portable status enum and Zephyr errno values; unknown native errors
map to I/O failure.

Use these APIs from thread context. Serialize full operations on a device and
all use of its endpoint scratch. Zephyr controllers serialize bus transactions;
the selected controller must provide finite completion. There is no portable
Zephyr timeout override, and a core deadline cannot preempt a synchronous
transfer. Runtime delays enforce at least the requested duration, including
sub-tick requests, by waiting against the monotonic kernel tick clock. They may
overshoot by scheduling/tick granularity; they are not a THSEL waveform generator.
