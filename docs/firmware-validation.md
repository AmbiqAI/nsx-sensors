# Firmware link validation

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
python3 tests/verify_firmware_link.py build/app.axf app.map bme688 port_nsx_i2c
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
