# Bounded sensor simulation and regression evidence

These tests exercise real driver cores through fake bus/runtime callbacks. They
are partial behavioral models, not digital twins or hardware qualification.
Production drivers, headers and build source lists are unchanged by this work.

## How we avoid testing an implementation against itself

Expected register numbers, bit fields and raw vectors are literal independent
fixtures, not imported private driver constants. Keep three kinds of expectations
separate:

- **Device behavior:** named manufacturer sections or independently generated
  reference vectors establish what the fake should do.
- **NSX contracts:** public headers define output preservation, lifecycle and
  recovery after ambiguous transfers. These are software choices, not claims
  about the sensor.
- **Scenario assumptions:** selected sample schedules, synthetic data and injected
  bus failures are inputs to the test. They are not measured device behavior.

Direct model checks exercise temporal/status/FIFO effects before invoking the
MPU6050/MAX86150 driver. A small mutation campaign checks whether tests reject
specific wrong implementations. Neither technique proves the model complete.
Do not add a rule merely because a driver currently depends on it; resolve the
manufacturer evidence first, recording ambiguity rather than inventing behavior.

## Evidence map

Existing scenarios are retained where sufficient; only BME688, MPU6050 and
MAX86150 need model/fault additions in this bounded pass. Every core participates
in the mutation controls.

| Core | Device/reference basis | Behavior and regression checks | Deliberate defect rejected |
| --- | --- | --- | --- |
| BME688 | BST-BME688-DS000-03 rev1.3 register map; existing Bosch v4.4.8 oracle generator and frozen vectors | Calibration/compensation, SPI paging, repeated forced measurements; init failures now before **and after** transfer effects, followed by successful reinit on the same model | Wrong field-data register |
| MPU6050 | RM-MPU-6000A-00 rev4.2 §§4.2,4.4,4.14,4.16–4.19,4.30–4.31 | Existing FIFO formats/overflow, signed scaling and fresh averaging; direct virtual-time/latest-sample/status-clear controls; init failure/recovery on both sides of effects | Historical full-scale shift error (4 instead of 3) |
| MAX86150 | 19-8402 rev2 FIFO pointer/data/config descriptions, System Control, FIFO Description | Existing slot/sign decoding, reset, ambiguity and concurrent production; scheduled FIFO fill, loss, stopped producer and recovery; both init failure modes | Wrong FIFO data address |
| INA228 | SLYS021A SHUNT_CAL, DIAG_ALRT and CHARGE definitions | Existing register reset/latch behavior, calibration and signed counters; shared old/new executable vectors below | Reconstructed earlier calibration multiplier and ready-bit defects |
| ICM-45605 | Retained TDK basic transport/register subset; existing raw/scaled vector tests | Existing byte order, indirect registers, data-ready timing/races, bounded reset/calibration and before/after failure sweep | Missing SPI read bit |
| TSL2540/2541 | DS000564 / DS000570 v2-00 STATUS, channel-data and gain/calibration registers | Existing W1C status, both identities, channel byte order, gain and VIS_ADJ; exhaustive init failure/recovery sweep | Swapped visible-channel byte order |
| Qwiic LED Stick | SparkFun command reference linked in PROVENANCE.md | Existing literal command packets, capacity checks, configuration and address ambiguity recovery | Wrong all-colors command |
| T5838 | DS-000383 rev1.2 Figure22 and AAD programming examples | Existing independent waveform decoder, all payload bits, two devices and before/after write failures | Stop width 128 cycles instead of strictly greater than 128 |

The new MAX86150 schedule is a chosen 400-frame/s scenario, without averaging.
It advances on elapsed virtual time even when no driver call occurs. It does
**not** implement every rate adjustment, optical response, partial conversion or
startup settling rule. MPU6050's existing timed scenario assumes an awake 100 Hz
part; its synthetic ramp and injected bus delay check freshness, not filtering
or inertial physics. Other models retain their existing limited timing behavior.

Sources: [MPU register map, manufacturer PDF mirror](https://electronperdido.com/wp-content/uploads/2015/08/acelerometro-6-ejes-MPU-6000-Register-Map1.pdf),
[MAX86150 manufacturer PDF](https://www.analog.com/media/en/technical-documentation/data-sheets/MAX86150.pdf),
[INA228 manufacturer PDF](https://www.ti.com/lit/ds/symlink/ina228.pdf).
Other source revisions and links are retained in [PROVENANCE](../PROVENANCE.md),
[TSL254x](tsl254x.md) and [T5838](t5838.md). The evidence map describes the
existing test basis, not a new independent datasheet audit of every core.

## Old/new comparison, with intentional differences

`tests/sensor_legacy_compare.py` compiles the actual pre-migration INA228 at
`65e8f2141801eedac5052d4d533be51ef44cac5f` and the current source against the
**same** `tests/regression/ina228_vectors.c`. Both independently assert expected
SHUNT_CAL (1250/5000), all four MEMSTAT/CNVRF combinations and five signed 40-bit
counter boundaries, then compare their 22 printed results. No old sources are
rewritten or copied into the repository. The pinned local git object is required;
the script never fetches it, so this optional check is not in shallow-clone CI.

This old revision already contains the earlier INA228 math/ready-bit fixes. The
INA mutation cases reconstruct those documented earlier defects; they are not
executions of those buggy historical revisions. MPU's shift mutation reconstructs
`GCONFIG_FS_SEL_BIT=4` in `5454002987fac3f1c432cffd467d9f32c5489065`.

We deliberately do not build a compatibility layer for every former API. For the
remaining drivers the existing literal vectors and device effects are regression
references, **not** a claim of running pre-migration binaries side by side.
Migration guides record intended corrections: MPU range/FIFO/calibration changes,
MAX FIFO slot/sign/recovery corrections, explicit ICM lifecycle, and common error
contracts. Spark additions have no previous NSX core to compare. Old behavior is
not authoritative when it conflicts with the device reference or accepted API.

## Running the bounded checks

```sh
# Existing complete portable build matrix, including ASan/UBSan.
tests/portable_build.sh
# Nine compiling negative controls across eight cores; fresh output path required.
python3 tests/sensor_mutations.py --output /tmp/sensor-mutations-run
# Optional actual legacy comparison, with pinned history available locally.
python3 tests/sensor_legacy_compare.py --output /tmp/sensor-legacy-run
```

The mutation runner copies sources to a temporary directory, uses production
CMake targets, runs all eight clean controls, builds/runs each single mutant,
restores and rechecks its clean control, and finishes with all eight controls.
It leaves the checkout untouched. Missing/nonunique edit anchors, compile errors,
timeouts or failures without assertion/CHECK diagnostics fail the campaign; they
do not count as detected defects. JSON records source revision, working-tree
status and input hashes; logs retain the actual failing assertions. CI retains
these artifacts. Nine detected mutations mean sensitivity to those nine defects,
not a percentage of all possible driver faults.

No new public simulator API, OS dependency, physical sample generator or full MCU
emulator is introduced. Electrical I2C repeated-START/SPI framing, actual THSEL
pulse timing, real reset/settling durations, physical accuracy and platform
scheduling still require bench qualification. Hardware remains a later additive
check; software tests should stay useful when hardware becomes available.
