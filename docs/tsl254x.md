# TSL2540 / TSL2541 ambient light

Include `<nsx/sensors/tsl254x.h>` and link `nsx::sensor_tsl254x` (CMake option
`NSX_SENSOR_TSL254X`, installed component `tsl254x`). This native driver uses the
shared I2C endpoint, including the existing NSX adapter. The endpoint owns the
7-bit address `0x39`; it must support two TX bytes and four RX bytes, with genuine
combined reads. There is no per-device NSX bus wrapper.

Select `TSL254X_TSL2540` or `TSL254X_TSL2541` from the actual BOM. Both use ID
`0xe4`; the auxiliary ID distinguishes TSL2540 (0/1) from TSL2541 (2). Probe
rejects a mismatch. Supporting both variants does not establish which Spark uses.
Confirm supply, I2C levels, address sharing and interrupt wiring from the final
schematic. Both sensors at the same address need separate buses or a mux.

Bind is pure. Init validates and programs gain, integration cycles, thresholds,
persistence and interrupt enables, then leaves ALS off. It disables wait,
sleep-after-interrupt and read-to-clear status, preserving reserved configuration
bits. Start sets power and ALS together for initial autozero; stop returns to
sleep. Reconfigure with init; a failed mutation requires init before reuse.
No soft reset command is invented. Reinitialize after external reset/power loss.

```c
tsl254x_context_t light = {0};
tsl254x_config_t config = tsl254x_default_config();
config.gain = TSL254X_GAIN_16;
config.integration_cycles = 64;
/* Check every returned status; endpoint is already bound to address 0x39. */
nsx_sensor_status_t status = tsl254x_bind(&light, &endpoint, TSL254X_TSL2541);
if (status == NSX_SENSOR_OK) status = tsl254x_init(&light, &config);
if (status == NSX_SENSOR_OK) status = tsl254x_start(&light);
```

Integration cycles are 1..256 at nominal 2.81 ms/cycle. Start is nonblocking;
allow integration plus autozero to finish. `read_sample` is a latest-register
snapshot, not proof of new data: it can return previous or zero counts if called
early. No data-ready bit is invented. For conversion events, persistence code 0
and threshold interrupt enable select an interrupt every ALS cycle. Defer GPIO
service to a task, read, then explicitly acknowledge the flags. IRQs can coalesce;
this is not a FIFO or an exactly-once sample interface.

Visible/IR values are raw 16-bit counts. Digital saturation is checked against
`min(65535, cycles*1024-1)`; analog saturation is reported separately. Status is
sticky until acknowledged, so analog saturation may describe an earlier cycle.
Both channels use one burst; there is no claimed cross-transfer latch between
status and data. All output arguments remain unchanged on error.

`get_visible_scale` returns the cached TSL2541 factory signed-percent VISADJ
multiplier (1 for TSL2540). Multiply visible counts by it in floating point;
raw threshold registers still use unadjusted counts. It is not a lux conversion.
Lux needs variant/optics calibration, gain and integration normalization; the
initial driver deliberately does not import Zephyr's empirical lux constant as
universal calibration. No accuracy or hardware qualification is claimed.

Sources: ams OSRAM [TSL2540 DS000564 v2-00](https://look.ams-osram.com/m/d83ec5eb2896cf6/original/TSL2540-DS000564.pdf)
and [TSL2541 DS000570 v2-00](https://look.ams-osram.com/m/305dde3c0d33592f/original/TSL2541-ALS-Light-to-Digital-Sensor.pdf),
register descriptions pp. 11–25. The
[Zephyr TSL2540 driver](https://github.com/zephyrproject-rtos/zephyr/blob/main/drivers/sensor/ams/tsl2540/tsl2540.c)
was inspected for integration context; its implementation was not copied.
