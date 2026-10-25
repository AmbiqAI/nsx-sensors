# Pre-v1 API cleanup

This unreleased source-breaking cleanup follows the portable migrations. Update
callers and recompile; no compatibility aliases retain the old global names.
Register encodings, default device settings and measurement units are unchanged.
It does not bump the module version or claim hardware qualification.

## MPU6050 names

Prefix all clock, DLPF and full-scale enum members with `MPU6050_`:

| Previous name | New name |
| --- | --- |
| `CLOCK_GX_PLL` | `MPU6050_CLOCK_GX_PLL` |
| `DLPF_044HZ` | `MPU6050_DLPF_044HZ` |
| `GYRO_FS_250DPS` | `MPU6050_GYRO_FS_250DPS` |
| `ACCEL_FS_2G` | `MPU6050_ACCEL_FS_2G` |
| `MPU_I2CADDRESS_AD0_LOW` | `MPU6050_I2C_ADDRESS_AD0_LOW` |
| `MPU_I2CADDRESS_AD0_HIGH` | `MPU6050_I2C_ADDRESS_AD0_HIGH` |

The same prefix rule covers the remaining values in these enums. Existing
`mpu6050_*_t` type names are unchanged.

## MAX86150 typed selections

Use `max86150_slot_t` and `MAX86150_SLOT_*` instead of `max86150_slot_type`
and `Max86150Slot*`. Slot codes and ordering requirements are unchanged.
Configuration fields and direct setters now use named enums for ADC range,
PPG/ECG rates, pulse width, averaging, LED current range and ECG gains. C++
callers should use the named constants rather than assigning integer literals.
Raw amplitude bytes, thresholds, channel indices and interrupt masks remain
integers because they represent numeric values or bit sets.

```c
max86150_config_t config = max86150_default_config();
config.slots[0] = MAX86150_SLOT_PPG_LED1;
config.slots[1] = MAX86150_SLOT_PPG_LED2;
config.slots[2] = MAX86150_SLOT_ECG;
config.ppg_sample_rate = MAX86150_PPG_SAMPLE_RATE_400_HZ;
config.ppg_pulse_width = MAX86150_PPG_PULSE_WIDTH_100_US;
config.ecg_sample_rate = MAX86150_ECG_SAMPLE_RATE_400_HZ_CLK0;
config.ecg_pga_gain = MAX86150_ECG_PGA_GAIN_2;
```

The mapping follows the [manufacturer register tables, Rev 2 pp. 29–35](https://www.analog.com/media/en/technical-documentation/data-sheets/MAX86150.pdf).
Names ending in `2_PULSES` distinguish the two-pulse PPG selections. ECG `CLK0`
and `CLK1` retain distinct ADC clock encodings even where rates match. Averaging
encodings 6/7 remain accepted as 32-sample equivalents; use the named `_32`
selection (encoding 5) for new configuration. LED current range names are nominal;
maximum amplitude gives 51/102 mA for the 50/100 mA ranges.

Rates describe requested settings. Hardware may adjust PPG rate for pulse width,
slot timing and synchronization, and averaging changes output throughput.
`max86150_get_ppg_config` remains raw hardware readback, not a cached settings
getter. No helper claims a guaranteed effective FIFO rate.

## INA228 calibration scale

`ina228_get_current_lsb(ctx, &scale)` provides the cached amperes-per-code scale
from the last verified calibration without I/O. It returns `E_STATE` until
calibrated, or after reset or failed calibration programming/readback. A rejected
preflight request preserves any prior valid calibration. Output is unchanged on error.
This avoids accessing `_current_lsb` when converting exact accumulator deltas:

```c
float scale;
nsx_sensor_status_t status = ina228_get_current_lsb(&monitor, &scale);
if (status == NSX_SENSOR_OK) {
    /* delta_energy is an application-computed integer delta with wrap handled. */
    double joules = (double)delta_energy * 16.0 * 3.2 * (double)scale;
    /* delta_charge is a signed charge-code delta. */
    double coulombs = (double)delta_charge * (double)scale;
    /* Consume joules/coulombs here. */
}
```

Keep range and calibration fixed during an accumulation window. The getter
returns the same requested scale used by scaled reads; it is not hardware
readback or a correction for integer SHUNT_CAL quantization. External resets or
register writes require application recovery as before.

## Lifecycle and recovery comparison

Every bind is pure and caller storage must outlive its use. Serialize complete
operations per physical device; endpoints serialize individual bus transactions
and release the bus before conversion delays. Timing hooks must wait at least
the requested duration, including delays shorter than an RTOS tick.

| Driver | Successful init / explicit acquisition | Recovery highlights |
| --- | --- | --- |
| BME688 | Sleep, heater off; `start`/`collect` or bounded `measure` | A failed consuming collect may lose freshness while leaving the request pending. Use `sleep` to cancel, then start again; reset/init failures require init. |
| LED Stick | Configured accessory; issue commands | Failed length change requires init; address changes invalidate binding even on success, so resolve address then rebind. No identity probe is invented. |
| INA228 | ADC shutdown; `set_mode` starts conversions | Bind-only raw register access is intentional. Scaled current/power/energy/charge require calibration; reset or failed calibration invalidates it. |
| MPU6050 | Asleep, FIFO/interrupts off; wake with `set_sleep(ctx, 0)` | Uncertain configuration writes require init. Ambiguous FIFO consumption requires reset_fifo, then explicit enable. Calibration is explicit. |
| MAX86150 | Shutdown, FIFO/interrupts off; `start` flushes and acquires | FIFO-data ambiguity allows clear/start; failed consuming interrupt reads or lost configuration require init. |
| ICM-45605 | Idle; explicit `start`/`stop` | Failed mutation or consuming reset-status read requires init. Direct data reads are latest snapshots, not a freshness guarantee. Calibration is explicit. |

These device-specific lifecycles share bus/runtime/status contracts without a
universal sensor object. BME688/MPU6050/MAX86150/ICM timeouts without `now_us`
bound requested delays, not transfer wall time; endpoints need their own finite
completion bound. ICM typed TDK storage remains address-stable after bind.
