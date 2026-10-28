# Provisioned Zephyr validation

`tests/zephyr_integration.py` is the separate Linux framework integration gate.
Normal portable/host tests remain offline and do not invoke it. This is software
integration evidence with simulated controllers, not sensor hardware validation,
resource qualification, or a release gate completion claim.

Use Python 3.12+, host GCC, Git, and a GNU Arm `arm-none-eabi` toolchain with
newlib built with retargetable locking. The Ubuntu 24.04 Arm/newlib packages
used in the initial CI run failed Zephyr 4.4.0's locking prerequisite. Setup
does not require a Zephyr SDK or modify an existing Zephyr/Nordic checkout. Choose a dedicated writable workspace outside this repository:

```sh
python3 tests/zephyr_integration.py --provision --provision-arm-toolchain --workspace /tmp/nsx-zephyr
```

`--provision` explicitly permits dependency downloads: an isolated Python venv,
west 1.5.0, CMake 3.31.6, Ninja 1.11.1.4, Zephyr's Python build requirements,
and these exact source revisions:

| Source | Revision |
| --- | --- |
| Upstream Zephyr v4.4.0 | `684c9e8f32e4373a21098559f748f06915f950c9` |
| cmsis | `512cc7e895e8491696b61f7ba8066b4a182569b8` |
| cmsis_6 | `30a859f44ef8ab4dc8f84b03ed586fd16ccf9d74` |

The two CMSIS revisions are verified against Zephyr's pinned `west.yml`.
Only these manifest dependencies and this repository enter `ZEPHYR_MODULES`;
unrelated installed modules are not discovered. Existing source checkouts with
a different HEAD or tracked edits are rejected. Python dependency resolutions
and compiler/tool versions are recorded, rather than claimed to be immutable.
On Linux x86_64, `--provision-arm-toolchain` downloads and extracts official
Arm GNU 15.2.Rel1 into the workspace, without system installation. The archive
is checked against the pinned SHA-256
`597893282ac8c6ab1a4073977f2362990184599643b4c5ee34870a8215783a16`,
from [Arm's published checksum](https://developer.arm.com/-/media/Files/downloads/gnu/15.2.rel1/binrel/arm-gnu-toolchain-15.2.rel1-x86_64-arm-none-eabi.tar.xz.sha256asc).
The [official archive](https://developer.arm.com/-/media/Files/downloads/gnu/15.2.rel1/binrel/arm-gnu-toolchain-15.2.rel1-x86_64-arm-none-eabi.tar.xz)
URL, release and verified checksum are saved in `evidence/arm-toolchain.json`.
Curl has connection and transfer timeouts, bounded retries, and the runner's
process-group timeout; extraction is bounded too. Every run compiles a small
newlib prerequisite probe before building Zephyr, including custom toolchains.
The probe checks the actual library configuration and does not override it.

The Arm compiler can alternatively be selected explicitly with
`--arm-prefix /opt/arm/bin/arm-none-eabi-`.

After provisioning, omit both provisioning flags and select the installed
compiler with `--arm-prefix <workspace>/toolchains/arm-gnu-toolchain-15.2.rel1-x86_64-arm-none-eabi/bin/arm-none-eabi-`
to run without dependency downloads.
Every subprocess has a wall-clock timeout (default 900 seconds, overridable
with `--timeout`); the native executable has a 60-second limit. On timeout the
whole command process group is killed. Commands, return codes, durations and
logs are written beneath `<workspace>/evidence`, along with versions and the
final result. Builds use fresh CMake configuration on each run.

The native `native_sim/native/64` application compiles all nine selectable
cores and their public headers through the external module's Kconfig/CMake
integration, including ICM-45605's transitive vendor header dependency. It runs
four ztests against actual Zephyr kernel/device/I2C/SPI/sensor APIs, the real
NSX adapter and BME688 core/wrapper. Only controller hardware is replaced:

- I2C repeated START/final STOP, reads, invalid requests rejected before I/O,
  size bounds, native status translation and unchanged RX after partial failed I/O.
- A single SPI transceive for both phases, zero MOSI during RX, phase extraction,
  endpoint independence, native errors, maximum combined buffer size, and
  controller configuration identity when an endpoint is rebound.
- Two I2C and one SPI devicetree BME688 instances, initial no-data state, temperature/pressure/
  humidity golden values (including Pa to kPa), unsupported channels, failed
  fetch cache invalidation, reinitialization/recovery, stale-data timeout, and
  two concurrent fetch threads verifying whole-operation serialization.

Synthetic calibration/raw values reuse the portable BME688 fixture and its
Bosch-oracle expected values. The simulation finishes successful conversions
immediately but the real core still performs its Zephyr runtime wait.

The same application is cross-compiled and linked for `qemu_cortex_m3` with
newlib. The runner reuses the firmware link verifier to check ELF32/Arm executable
identity and BME688 code-symbol/archive provenance. It also checks I2C/SPI
binding and wrapper fetch code addresses against the allocated input sections
from their actual adapter/wrapper archive members. It does not run QEMU or claim Arm
execution. A separate T5838-only application builds with I2C and SPI disabled,
checking that control-only use does not acquire unrelated bus dependencies.

`.github/workflows/zephyr.yml` provisions and runs this gate separately from
host CI, then uploads command/version evidence and Arm ELF/map artifacts.
