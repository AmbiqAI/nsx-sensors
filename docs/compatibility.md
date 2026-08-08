# `v0.1.0` compatibility

## What the declarations mean

`nsx-sensors` contains no SoC-conditional code. Every SoC and toolchain it can
reach is inherited from its direct dependencies: `nsx-core`, `nsx-i2c`, and
`nsx-spi`. The `compatibility` block in `nsx-module.yaml` therefore declares the
intersection of what those dependencies declare, and nothing beyond it.

Three distinct claims are kept separate throughout this document:

| Level | Meaning |
| --- | --- |
| Declared | The dependency graph advertises the SoC/toolchain, so the module is expected to configure and build. |
| Build-verified | A configure and build of a real NSX consumer succeeded for that SoC and toolchain. |
| Runtime validated | The driver was exercised against the real sensor part on real hardware. |

## SoC declarations

Evidence source: `AmbiqAI/nsx-ambiq-sdk` at
`a9f4ec25a162f6f3700623feb691423bb5a51132` (`v5.2.24`), `compatibility.socs` of
`modules/nsx-core`, `modules/nsx-i2c`, and `modules/nsx-spi`.

| SoC | `nsx-core` | `nsx-i2c` | `nsx-spi` | `nsx-sensors` declares |
| --- | --- | --- | --- | --- |
| `apollo3` | yes | yes | yes | yes |
| `apollo3p` | yes | yes | yes | yes |
| `apollo4l` | yes | yes | yes | yes |
| `apollo4p` | yes | yes | yes | yes |
| `apollo330P` | yes | yes | yes | yes |
| `apollo510` | yes | yes | yes | yes |
| `apollo510b` | yes | yes | yes | yes |
| `apollo510L` | yes | yes | yes | yes |
| `apollo2` | yes | no | no | no |
| `atomiq110` | yes | no | no | no |
| `apollo5b` | no | no | no | **no** |

`apollo2` and `atomiq110` are declared by `nsx-core` but not by the I2C or SPI
transports this module requires, so they are correctly absent.

## `apollo5b` is deliberately not declared

`apollo5b` was evaluated for `v0.1.0` and rejected on evidence.

Evidence that exists:

- `nsx-ambiq-sdk` carries a SoC descriptor and a side-effect-free facts file
  (`cmake/socs/apollo5b.cmake`, `cmake/socs/facts/apollo5b.cmake`).
- `nsx-ambiq-sdk` carries `modules/nsx-core/src/apollo5b/{gcc,armclang}` startup
  and linker artifacts and an `apollo5b_evb` board directory.
- `neuralspotx` at `6dafd6880331eee8fc6caa99de1ccd8321f18495` lists
  `apollo5b_evb` as a registered board with GCC, Armclang, and ATfE.
- `NSX_SOC_FAMILIES_APOLLO5` in `cmake/nsx_toolchain_flags.cmake` includes
  `apollo5b`.

Evidence that does not exist, and that blocks the claim:

- `nsx-ambiq-sdk` `docs/platform-coverage.md` at the audited pin classifies
  `apollo5b` as **descriptor-only**: "Descriptor + facts exist, but no
  `apollo5b` HAL/BSP artifacts are present." It classifies `apollo5b_evb` as
  "Not configure-ready until `apollo5b` artifacts arrive," and the SDK's own
  toolchain smoke runner skips the board for that reason.
- `nsx-core`, `nsx-i2c`, `nsx-spi`, and `nsx-soc-hal` all omit `apollo5b` from
  `compatibility.socs`.
- No configure or build of `nsx-sensors` has ever been attempted for `apollo5b`.

A SoC descriptor is a platform fact. It is not a statement that the I2C and SPI
transports this module links against support the part. Declaring `apollo5b`
today would be a claim, not evidence. The gate to add it later is explicit: the
`apollo5b` HAL/BSP artifacts land in `nsx-ambiq-sdk`, `nsx-core`, `nsx-i2c`, and
`nsx-spi` declare `apollo5b`, and `tests/configure_target_smoke.sh` passes for
`apollo5b_evb` on at least one toolchain.

## Toolchain declarations

`nsx-core`, `nsx-i2c`, and `nsx-spi` each declare `arm-none-eabi-gcc`,
`armclang`, and `atfe`, so `nsx-sensors` declares the same three.

The module uses no toolchain-specific extension. It requires `c_std_11`, and the
vendored TDK subset compiles cleanly as C11 with `-Wall -Wextra -Werror`.

## Qualification status for `v0.1.0`

| Driver | Bus | Declared | Build-verified | Runtime validated |
| --- | --- | --- | --- | --- |
| MAX86150 | I2C | all declared SoCs | yes, all 8 x 3 | **no** |
| MPU6050 | I2C | all declared SoCs | yes, all 8 x 3 | **no** |
| INA228 | I2C | all declared SoCs | yes, all 8 x 3 | **no** |
| Qwiic LED Stick | I2C | all declared SoCs | yes, all 8 x 3 | **no** |
| ICM-45605 | SPI | all declared SoCs | yes, all 8 x 3 | **no** |

"Build-verified" above means `tests/module_compile_smoke.sh` compiled all seven
translation units, including the vendored TDK subset, against the real
`nsx-core`, `nsx-i2c`, `nsx-spi`, and AmbiqSuite headers from `nsx-ambiq-sdk`
at `a9f4ec25a162f6f3700623feb691423bb5a51132`. Every declared SoC was verified
against every declared toolchain:

| SoC | Core | `arm-none-eabi-gcc` | `armclang` | ATfE |
| --- | --- | --- | --- | --- |
| `apollo3` | cortex-m4 | pass | pass | pass |
| `apollo3p` | cortex-m4 | pass | pass | pass |
| `apollo4l` | cortex-m4 | pass | pass | pass |
| `apollo4p` | cortex-m4 | pass | pass | pass |
| `apollo330P` | cortex-m55 | pass | pass | pass |
| `apollo510` | cortex-m55 | pass | pass | pass |
| `apollo510b` | cortex-m55 | pass | pass | pass |
| `apollo510L` | cortex-m55 | pass | pass | pass |

Toolchain versions used: Arm GNU `15.2.rel1`, Arm Compiler `6.24Rel19`, and
Arm Toolchain for Embedded `22.1.0`. The script derives each SoC's compile
definitions, core, and MCU payload directory from the SDK's own descriptors, so
it cannot drift from the SDK's single source of truth.

Running the same script for `apollo5b` fails with "SoC 'apollo5b' maps to
mcu/apollo5b, which is not staged", which is the direct, reproducible evidence
behind the exclusion above.

**No driver in this release is hardware-in-the-loop qualified.** No board was
flashed and no sensor part was exercised while preparing `v0.1.0`. Sensor
functionality is explicitly unqualified for this release. Do not read a declared
SoC, a green CI run, or a successful consumer build as evidence that a driver
talks correctly to real silicon.

Three smoke levels back these claims, in increasing fidelity:

| Script | Scope | Needs |
| --- | --- | --- |
| `tests/vendor_compile_smoke.sh` | vendored TDK subset only, no NSX headers | any C compiler |
| `tests/module_compile_smoke.sh` | all module sources against real NSX and AmbiqSuite headers | an `nsx-ambiq-sdk` checkout plus a cross toolchain |
| `tests/configure_target_smoke.sh` | full NSX consumer configure, build, and link | an NSX consumer workspace plus a cross toolchain |

Hosted CI checks out the exact qualified `nsx-ambiq-sdk v5.2.24` commit and
compiles all 8 declared SoCs with Arm GNU using `-Wall -Wextra -Werror`. The
immutable-release workflow inspects the exact main-commit CI run and refuses
publication unless this job and the three host jobs all completed successfully.
The Armclang and ATfE columns in the table above are exact local qualification
evidence from the same commit and SDK payload; they are not represented as
hosted CI results.

The full consumer smoke remains a maintainer-run escalation because it needs a
prepared application workspace. Neither smoke passes silently: a missing
workspace, `cmake`, cross compiler, SDK payload, or ATfE `clang` is a hard
failure. An ATfE request is additionally checked against the compiler's version
banner and target list, so a host `clang` cannot be mistaken for ATfE.

None of the three links a firmware image against real hardware, and none of them
exercises a sensor.

`tests/vendor_compile_smoke.sh` runs unconditionally in CI and compiles the
vendored TDK subset standalone as C11 with `-Wall -Wextra -Werror`. Warnings are
errors, so a future vendor intake that introduces one fails instead of passing
green. That proves the vendored subset is self-contained and warning-clean. It
proves nothing about sensor behavior.

## Application responsibilities

Applications own the board bring-up that this module deliberately does not
perform:

- initialize the `nsx_i2c_config_t` / `nsx_spi_config_t` bus before calling any
  `*_init()`; the drivers take a pre-configured, caller-owned bus;
- own the chip-select pin, interrupt GPIO wiring, and NVIC configuration for
  ICM-45605;
- own power, clock, and cache policy;
- own sample storage and framing. No driver here buffers samples.
