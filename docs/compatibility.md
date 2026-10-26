# Compatibility

See [firmware link validation](firmware-validation.md) for the explicit executable
and retained-symbol checks required by the opt-in consumer smoke script.

## Unreleased portable foundation

All eight driver cores have portable CMake targets and offline host tests. The
seven I2C/SPI cores share the NSX adapter; T5838 uses a caller-supplied THSEL
waveform endpoint and the separate audio layer. [Zephyr integration](zephyr.md)
provides selectable portable cores, reusable I2C/SPI endpoints and a BME688
temperature/pressure/humidity wrapper. Other Zephyr wrappers remain pending.
`nsx::sensors` aggregates the selected portable cores without SDK dependencies. Metadata dependencies below
describe NSX integration, not portable-core requirements. No framework or hardware
qualification is implied by portable compilation.

| Drivers | Portable core | NSX integration | Zephyr wrapper | Hardware |
| --- | --- | --- | --- | --- |
| BME688 | Implemented; host tested and linked | Shared NSX adapter | T/P/H fetch/get; pinned native tests and Arm link | Unqualified |
| LED Stick, INA228, MPU6050, MAX86150 | Implemented; host tested and linked | Shared NSX adapter | Direct cores available; wrappers pending | Unqualified |
| ICM-45605 basic SPI | Implemented; per-instance TDK state, direct reads and explicit calibration | Shared NSX adapter | Direct core available; wrapper pending | Unqualified |
| TSL2540/TSL2541 | Implemented shared core with explicit variant; host tested | Shared NSX I2C adapter | Direct core available; wrapper pending | Unqualified |
| T5838 AAD control | Implemented Analog/D1/D2 control and waveform encoder; host tested | Board waveform endpoint and audio clock handoff pending | Pending | Unqualified |

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

The existing smoke levels are complemented by the explicit real SDK fixture:

| Script | Scope | Needs |
| --- | --- | --- |
| `tests/vendor_compile_smoke.sh` | vendored TDK subset only, no NSX headers | any C compiler |
| `tests/module_compile_smoke.sh` | all module sources against real NSX and AmbiqSuite headers | an `nsx-ambiq-sdk` checkout plus a cross toolchain |
| `tests/configure_target_smoke.sh` | full NSX consumer configure, build, and link | an NSX consumer workspace plus a cross toolchain |
| `tests/nsx_firmware_integration.py` | real apollo4p_evb bare-metal and FreeRTOS final links; all eight cores and both NSX bindings | exact pinned SDK plus Arm GNU/newlib |

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

The explicit integration fixture links real board/startup/HAL/transport and, in
FreeRTOS mode, the kernel/port. None of these commands executes a sensor or
flashes hardware; see [recorded scope and prerequisites](firmware-validation.md).

`tests/vendor_compile_smoke.sh` runs unconditionally in CI and compiles the
vendored TDK subset standalone as C11 with `-Wall -Wextra -Werror`. Warnings are
errors, so a future vendor intake that introduces one fails instead of passing
green. That proves the vendored subset is self-contained and warning-clean. It
proves nothing about sensor behavior.

## BME688 forced-mode addition (unreleased)

The original BME688 driver was added after the historical release qualification above.
The following table records PR #16 before portable migration; its NSX adapter
and Armclang/ATfE results are not claims about the new portable foundation.
Current foundation evidence is listed separately below.
It supports T/P/H and optional raw gas resistance in forced mode, over I2C and
4-wire SPI. Parallel heater-profile scanning remains follow-up work for issue
#14; BSEC, IAQ, and derived VOC outputs are excluded.

| Evidence | Result |
| --- | --- |
| Synthetic host tests, native core and actual NSX adapters | pass, including fixed Bosch v4.4.8 golden vectors and register/failure paths |
| AddressSanitizer + UndefinedBehaviorSanitizer | pass |
| Module compile, all 8 declared SoCs, Arm GNU 15.2.Rel1 | pass, all 10 translation units, `-Wall -Wextra -Werror` |
| Module compile, all 8 declared SoCs, Arm Compiler 6.23 (HSK early access) | pass, same sources/warnings |
| Module compile, all 8 declared SoCs, ATfE 22.1.0 | pass, same sources/warnings |
| Nested CMake/export contract | configure passed |
| Real NSX firmware links with BME688 and all other core validation calls | pass, apollo4p_evb bare-metal + FreeRTOS; see firmware validation |
| BME688 hardware runtime | **not performed; unqualified** |

These new compile checks used `nsx-ambiq-sdk` commit
`788b6fa3955a3aae3ac9e5d26783c36173207a34` with its locally staged AmbiqSuite
payload. This records the evidence for the new driver; it does not replace the
historical SDK pin above. The existing CI matrix will also compile the new
sources against the pinned SDK on each PR.

The portable foundation now passes synthetic BME688/LED Stick protocol and
lifecycle tests, host ASan/UBSan, C/C++ consumer linkage, relocated package
installation, selected-driver configurations and all eight GCC SoC compile
checks. The foundation has 10 translation units (including the shared bus and
retained TDK subset). The shared NSX port and real bare-metal/FreeRTOS links now have
contract/link evidence; hardware tests remain pending; Armclang and ATfE have not been rerun for this migration.

Hardware qualification still needs a recorded board, sensor/module revision,
SDK revision, toolchain, bus speed/mode, and results for:

- chip/variant IDs, reset, and calibration readback on each claimed bus;
- I2C address/register traffic and SPI page/CS/read-command behavior on a logic
  analyzer, including after reset and injected transfer failures;
- T/P/H comparison with an independent reference and Bosch compensation of the
  same part's raw ADC/calibration data;
- heater target/duration programming, gas-valid versus heater-stable behavior,
  repeated forced readings, and waiting/sleeping through conversions;
- bounded mode transition/timeout recovery, reset during use, and more than
  256 samples to cover measurement-index wraparound;
- two devices and other users on a shared bus with application synchronization.

Host fixtures are synthetic. Neither their golden-vector agreement nor a
successful cross compile establishes a physical gas calibration or sensor
accuracy. See [the BME688 guide](bme688.md) for API and timing contracts.

## Application responsibilities

Applications own the board bring-up that this module deliberately does not
perform:

- initialize the `nsx_i2c_config_t` / `nsx_spi_config_t` bus before calling any
  `*_init()`; the drivers take a pre-configured, caller-owned bus;
- own the chip-select pin, interrupt GPIO wiring, and NVIC configuration for
  ICM-45605;
- own power, clock, and cache policy;
- own sample storage and framing. No driver here buffers samples.

## Shared NSX port (unreleased)

The portable BME688 and LED Stick drivers now have a shared NSX adapter.
It requires SDK PR #130; see [the adapter guide](nsx-port.md) for the exact
CI revision, transfer limits, locking, and remaining hardware checks.

INA228 migration and its failure/precision tests are documented in
[the INA228 guide](migration/ina228.md).

MPU6050's portable migration has host coverage for lifecycle, all native FIFO
formats, fault recovery, signed scaling, preserved compensation bits, and bounded
calibration. See [the MPU6050 guide](migration/mpu6050.md). Physical FIFO timing,
interrupt behavior and calibration convergence still require hardware testing.

MAX86150 now has an independently selectable portable target and shared NSX
endpoint support. Its migration includes a native-driver audit; see the
[MAX86150 migration guide](migration/max86150.md) for corrected FIFO/error/decoding behavior,
continuous versus snapshot acquisition, and remaining hardware checks.
