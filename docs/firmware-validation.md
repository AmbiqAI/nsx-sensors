# Firmware link validation

## Reproducible NSX integration fixture

The explicit offline command builds real bare-metal and FreeRTOS final firmware
for `apollo4p_evb` using the root sensor CMake targets:

```sh
python3 tests/nsx_firmware_integration.py /absolute/path/to/nsx-ambiq-sdk \
    --output build/firmware-integration
```

Provide a clean SDK checkout at exactly
`40b893d859672caf0b17b0e51db351a77b423e56`; the command checks the revision and
rejects local changes or untracked files. The SDK contains AmbiqSuite/HAL/BSP
and FreeRTOS-Kernel V11.1.0 (MIT), including the GCC ARM_CM4F port and heap_4.
There is no download, sibling-repository fallback, stub substitution, or flash.
The output directory must be new or empty. Python 3.9+, Git, CMake 3.20+, a build
backend, Arm GNU GCC with newlib, `arm-none-eabi-nm`, and
`arm-none-eabi-objcopy` must be on PATH. Each configure/build has a ten-minute
limit; other subprocesses have a two-minute limit. Missing prerequisites or
artifacts fail the command.

The fixture follows the SDK hardware-test firmware composition, using its real
board descriptor, startup/vector table, linker script, HAL/BSP, core runtime,
I2C and SPI targets. It does not include the SDK's Unity reporter. The SDK's
`nsx::board_flags` compilation context flows through the root sensor targets;
no driver source list or CPU/FPU flags are duplicated. Compile-command evidence
checks that every sensor/core/vendor/adapter/application translation unit and
the FreeRTOS sources match the SDK transport's CPU/FPU/ABI profile. The
application calls the SDK CMSIS `SystemCoreClockUpdate()` before starting the
scheduler, retaining the SDK-owned clock data rather than defining a replacement.

Both modes validate configurations for all eight portable cores and call both
NSX bindings. The fixture deliberately leaves controller configurations
uninitialized: these binding attempts fail without sensor I/O. This is a final
link fixture, not a deployable board/sensor example. Adapter function pointers
retain the real synchronous I2C/SPI transfer implementations and their HAL paths.
The existing ELF/map verifier checks all ten sensor entry points against their
defining archive members. Additional checks verify startup, clock, BSP, transport,
HAL and busy-wait provenance. FreeRTOS also requires the real task/scheduler,
mutex operations, port entry points, and strong SVC/PendSV/SysTick shims; actual
vector bytes must route to those handlers. Bare-metal rejects kernel presence.
Negative checks weaken SVC, corrupt its vector, and change application ABI
metadata in temporary copies; each must fail its intended gate.

The FreeRTOS fixture supplies its own `FreeRTOSConfig.h`, copied from the pinned
SDK template with heap, tick, and hook policy explicit. One application task owns
the contexts; the adapter lock uses a mutex covering each entire synchronous bus
transaction. Other users of that IOM would need the same lock. Delays use
`nsx_delay_us`/the HAL busy wait in both modes, preserving sub-tick waits rather
than rounding to scheduler ticks. Scheduler ticks are used only for task idle
periods. Device-level serialization remains the application's responsibility.

`evidence.json` records SDK/sensor revisions, dirty-tree status and diff hash,
fixture hashes, compiler version/path, CMake version, compile profile, input and
artifact hashes, symbol provenance, and negative-check results. Per-mode folders
contain commands, configure/build logs, `compile_commands.json`, input paths,
unstripped ELF, binary and map. A dirty source tree is reported, not represented
as a clean revision. For review evidence, commit the fixture before running.

The local acceptance run used Arm GNU 15.2.Rel1 (`15.2.1 20251203`) and CMake
4.3.2. Hosted CI installs Ubuntu's Arm GNU/newlib packages and records the actual
version; that compiler package is **not pinned** and is not claimed to reproduce
local artifact bytes. The SDK (including vendored HAL/kernel) is immutable.
CI preserves artifacts/logs even on failure. Existing SDK/header compiler
warnings remain visible; this integration build does not promote external
warnings to errors. Normal host test commands remain offline and independent of
this explicit SDK fixture.

These links do not establish scheduler execution, sensor traffic, waveforms,
resource qualification, or T5838 timed THSEL/audio integration. No hardware is
flashed. Independent review and user approval remain separate merge gates.

## Existing application workspace

Use the opt-in smoke script with a real NSX consumer workspace:

```sh
tests/configure_target_smoke.sh /path/to/app arm-none-eabi-gcc apollo510_evb \
    app.axf app.map bme688 port_nsx_i2c
```

The fourth and fifth arguments are the unstripped executable and its matching
linker map, relative to the script's fresh build directory. Both are required.
The remaining arguments select the implementations that
must be linked: `bme688`, `ledstick`, `ina228`, `mpu6050`, `max86150`, `icm45605`,
`tsl254x`, `t5838`, `port_nsx_i2c`, and/or `port_nsx_spi`. Select the relevant adapter to check its
binding function as well as the portable driver. The consumer must honor
`NSX_SENSORS_ROOT`, `NSX_TOOLCHAIN`, and `NSX_BOARD` passed by the script.

In addition to the selected cross compiler and CMake, inspection requires
Python 3.9+ and either `arm-none-eabi-nm` or `llvm-nm`. These inspect ELF artifacts
from all supported toolchains; selecting Armclang/ATfE does not change the symbol
inspection requirement.

The script checks the explicit artifact is a linked Arm ELF32 executable, then
requires a defined code symbol for each selected API in
`tests/verify_firmware_link.py`. Driver checks use `*_validate_config`; the NSX
adapter check uses `nsx_sensor_nsx_bind_i2c` or `nsx_sensor_nsx_bind_spi`.
The map must place the same symbol at the ELF address inside an allocated code
section from the expected archive and member, such as
`libnsx_sensor_bme688.a(bme688.c.o)` or `libnsx_sensor_port_nsx.a(nsx.c.o)`.
A same-named function in application code, a mere archive LOAD entry, or a
section discarded by the linker does not satisfy this check. The consumer must
reference the functions so archive extraction and section garbage collection
retain them.
Keep these definitions identifiable: stripping or LTO that removes/renames them
does not satisfy this check. Use a qualification build preserving those symbols.

An archive merely being produced is insufficient: `nsx::sensors` is an interface
aggregate and has no archive, and even concrete driver archives can be wholly
discarded during linking. Missing artifacts, relocatable objects, host binaries,
data/weak symbols, and missing selected functions fail the check.

Generate a map during the same link as the executable. GNU ld and LLVM lld use
`-Wl,-Map=app.map` through the compiler driver (for example,
`target_link_options(app PRIVATE "-Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/app.map")`).
Arm's linker requires `--map --symbols --list=app.map`. Unknown map formats fail
closed. GNU/lld code must use normal `.text`/`.text.*`/`.gnu.linkonce.t.*` input
sections; custom code-section layouts need explicit parser support before use.
The verifier normalizes the Thumb address bit and checks the defining input
section, excluding debug sections and output-section summaries.

For an existing artifact, run the inspector directly:

```sh
python3 tests/verify_firmware_link.py build/app.axf --map build/app.map bme688 port_nsx_i2c
```

This checks linked inclusion and the defining archive/object recorded by the
linker. It does not authenticate source contents or detect a deliberately forged
map/archive. Supply the matching unmodified map and ELF from the same build;
address checks catch mismatched maps but are not a cryptographic binding.
It does not establish successful device communication, all application paths,
or board configuration. Record the actual consumer, source
revision, SDK, toolchain, board and scheduler mode alongside qualification results.

CI's `tests/firmware_link_contract.sh` exercises the checker with actual Arm ELF
files built from the production CMake targets. Its positive fixture references
all eight portable cores; negative fixtures exercise same-named replacement
functions, discarded code, mismatched/missing maps, and invalid artifacts. It uses a synthetic entry point and `nosys` stubs: it is a test of the
validation mechanism, not real NSX bare-metal/FreeRTOS firmware qualification.
