# Resource qualification: eight cores and transport adapters

Measured 2026-10-27 using Arm GNU Toolchain 15.2.Rel1, GCC 15.2.1
20251203. This is static build evidence, not a hardware measurement or a runtime
stack high-water guarantee. Production sources match `f3b313a`; the report records
the observed checkout HEAD, uncommitted-file status and SHA-256 input manifest.
The run used clean source revision `12609fe15d97c8fdccecbe19df3348282e0d982d`.
The resulting evidence is committed separately; it does not qualify a future
release SHA automatically.

[Machine-readable results](resources/2026-10-27-arm-gcc15.json) and
[raw evidence archive](resources/2026-10-27-arm-gcc15-raw.tar.gz) preserve commands,
compile databases, linker maps, symbol sizes and GCC `.su` stack reports. The JSON
contains the archive checksum. Full ELF files remain in the fresh build directory;
the JSON records the portable ELF hashes. No original integration evidence or
provisioned checkout was overwritten.

## Reproduce without downloads or hardware

Use already provisioned dependencies matching the recorded revisions:
NSX SDK `40b893d859672caf0b17b0e51db351a77b423e56` and Zephyr
`684c9e8f32e4373a21098559f748f06915f950c9` (with the CMSIS modules and Python
venv from `tests/zephyr_integration.py`). The runner reuses the integration
fixtures' pins, checks all four repository roots and rejects staged, unstaged or
untracked changes before building and before accepting results. Revisions, paths
and clean status are recorded for SDK, Zephyr, CMSIS and CMSIS 6. Ignored build
artifacts and the pre-provisioned Python environment are not content-attested.
Do not mutate dependencies during measurement; endpoint checks cannot detect
edits reverted between checks. From the repository root:

```sh
python3 tests/resource_qualification.py \
  --output /tmp/nsx-resource-new-run \
  --arm-prefix /usr/local/arm-gnu-toolchain/bin/arm-none-eabi- \
  --sdk-root /home/adamp/Ambiq/neuralspotx/nsx-sdk-i2c-combined \
  --zephyr-workspace /home/adamp/Ambiq/neuralspotx/tmp/zephyr-v1
python3 -m unittest discover -s tests -p 'test_resource_qualification.py'
```

The output directory must not exist. All builds go there. The runner performs no
installation, flashing or target execution. Adjust paths for local provisioning;
`commands.json` records exact arguments and framework environment overrides.
Each subprocess has a ten-minute timeout. `tests/resources/CMakeLists.txt` consumes
the production root targets; framework builds consume the existing real NSX and
Zephyr fixtures. Production source lists are not copied into the harness.

## Context storage and representative linked footprint

Portable profile: Cortex-M4, Thumb, hard float, `fpv4-sp-d16`, short enums,
`-Os`, function/data sections, no LTO. All selected core and TDK vendor archive
members are retained with `--whole-archive`; section garbage collection is disabled.
The existing ELF/map verifier checks each core's linked implementation provenance.
The minimal `main` baseline is 4 bytes. These synthetic ELFs have no startup,
board linker script, sensor contexts, adapters, heap reservation or stack allocation.
They are never executed. They provide reproducible complete-API retention costs,
not the footprint of a typical application's selected call graph.

| Core | Context bytes | Linked flash bytes | Increment above baseline | Largest compiler frame (bytes) |
| --- | ---: | ---: | ---: | --- |
| BME688 | 88 | 3830 | 3826 | `bme688_init` 72 |
| LED Stick | 24 | 808 | 804 | `ledstick_bind` 48 |
| INA228 | 36 | 6468 | 6464 | `ina228_init` 56 |
| MPU6050 | 48 | 6941 | 6937 | `mpu6050_calibrate` 128 |
| MAX86150 | 64 | 4396 | 4392 | `read_fifo` 176 |
| ICM45605 | 144 | 8152 | 8148 | `icm45605_calibrate` 192 |
| TSL254x | 44 | 1500 | 1496 | `tsl254x_init` 64 |
| T5838 | 28 | 1325 | 1321 | `t5838_encode_write` 88 |
| All eight | 476 (one of each) | 29286 | 29282 | No additive stack total |

Flash is GNU `size` text + data, static linked RAM is data + bss. Every portable
link above has **zero static linked RAM**: caller-owned contexts are deliberately
measured separately with target `sizeof` probes. Allocating them makes them part
of the caller's RAM/stack, and requires additional output buffers, configuration
objects and transport endpoints. The bus descriptor is 20 bytes and runtime
callback descriptor 12 bytes; these are already embedded in contexts where
applicable and must not be added twice.

The individual increments sum to 33388 bytes, versus 29282 for the combined
increment. They are **not additive**: common bus code, libc memcpy/memset and
compiler arithmetic helpers are shared. Each link pulls only required runtime
library members, outside `--whole-archive`; maps expose the exact libgcc/libc
members. BME688 compensation and TDK driver/transport sources are included.
`libm` is offered by production targets where required; naming `-lm` does not
mean its entire archive is retained. Link deltas are standalone increments over
the empty fixture, not marginal additions to an existing application.

## Adapters and framework context

Layout probes reuse actual production consumer compile commands, without running
target code. Cortex-M4 NSX uses the Apollo4p EVB SDK ABI with `-Os`; Zephyr uses
`qemu_cortex_m3`, Thumb/AAPCS soft float, `-Os` and its configured ABI. All eight
core layouts happen to match in these two profiles; this is not an ABI guarantee
for other compilers or options.

| Adapter | Context bytes | Scratch included in context | Object text / data / bss | Transfer frame |
| --- | ---: | --- | --- | ---: |
| NSX | 308 | 256 bytes (`_scratch[64]` words) | 564 / 0 / 0 | 40 |
| Zephyr | 1060 | 512 TX + 512 RX bytes | 750 / 0 / 0 | 72 |

Adapter object sizes exclude external SDK/kernel/library dependencies, relocations
and final linker padding. They are not linked marginal costs. Scratch persists in
caller-owned endpoint storage; it is not a per-call stack buffer. SDK I2C/SPI
configuration objects, Zephyr SPI DT specs, locks and controller/kernel storage
are additional external lifetime requirements.

The fresh NSX bare-metal integration fixture links to text/data/bss
**53112 / 752 / 392464** bytes; the Zephyr test fixture links to
**43908 / 180 / 30142** bytes. These are whole-fixture GNU `size` values, not
sensor attribution or directly comparable board-memory budgets. In particular,
NSX's board linker reservations dominate bss; Zephyr includes its kernel, test
runner and configured stacks. They must not be subtracted from one another to
infer adapter cost. Matching maps and the Zephyr `.config` are in the raw archive.

## Stack evidence and limits

The table reports the largest **individual compiler frame**, not a maximum call
chain. Every compiled source's `.su` record is preserved, including TDK vendor
code, NSX SDK source and Zephyr source. Vendor maxima in the portable profile are
48 bytes (`inv_imu_soft_reset`) and 32 bytes (`write_mreg`, transport). Missing
prebuilt library frames are not treated as zero.

A concrete source call path needing a combined budget is
`icm45605_calibrate` → `icm45605_read_raw` →
`inv_imu_get_register_data` → `inv_imu_read_reg` → the vendor read callback →
ICM `read_reg` → `nsx_sensor_bus_transfer` → endpoint `transfer`.
The portable frames at these named functions are respectively 192, 48, 24, 16,
48 and 8 bytes before the selected endpoint. The direct-register vendor helper
can be inlined/tail-called; memory-register paths introduce other helpers.
NSX `transfer` may call `nsx_spi_read` (72-byte compiler frame) or
`nsx_i2c_write_read_combined` (80), then prebuilt Ambiq HAL routines; acquire and
release hooks add caller-defined paths. Zephyr transfer reaches controller driver
callbacks and kernel synchronization. T5838 also invokes caller-supplied control
and delay hooks. Runtime delay/time callbacks and libc/compiler helpers need
separate accounting.

These source-level paths identify coverage boundaries, not a verified machine-code
call graph. Do not sum all frames, combine Cortex-M3 and Cortex-M4 frames, or claim
that the largest function frame is the required task stack. Inlining, tail calls,
indirect callbacks, prebuilt HAL/newlib/libgcc code, interrupt nesting and exception
stacking (including floating-point context) prevent a complete bound from these
reports. Runtime high-water, scheduler/ISR scenarios and actual controller paths
remain unmeasured and require the intended firmware and hardware. No worst-case
stack or electrical qualification is asserted.
