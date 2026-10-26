/* SPDX-License-Identifier: BSD-3-Clause */
/* nm symbol sizes give target ABI sizeof values without executing target code. */
#include <nsx/sensors/bme688.h>
#include <nsx/sensors/ledstick.h>
#include <nsx/sensors/ina228.h>
#include <nsx/sensors/mpu6050.h>
#include <nsx/sensors/max86150.h>
#include <nsx/sensors/icm45605.h>
#include <nsx/sensors/tsl254x.h>
#include <nsx/sensors/t5838.h>
#define SIZE(name, type) unsigned char resource_size_##name[sizeof(type)]
SIZE(bme688, bme688_context_t);
SIZE(ledstick, ledstick_context_t);
SIZE(ina228, ina228_context_t);
SIZE(mpu6050, mpu6050_context_t);
SIZE(max86150, max86150_context_t);
SIZE(icm45605, icm45605_context_t);
SIZE(tsl254x, tsl254x_context_t);
SIZE(t5838, t5838_context_t);
SIZE(bus, nsx_sensor_bus_t);
SIZE(runtime, nsx_sensor_runtime_t);
#ifdef RESOURCE_NSX
#include <nsx/sensors/port_nsx.h>
SIZE(nsx_adapter, nsx_sensor_nsx_context_t);
SIZE(nsx_scratch, ((nsx_sensor_nsx_context_t *)0)->_scratch);
#endif
#ifdef RESOURCE_ZEPHYR
#include <nsx/sensors/port_zephyr.h>
SIZE(zephyr_adapter, nsx_sensor_zephyr_context_t);
SIZE(zephyr_tx, ((nsx_sensor_zephyr_context_t *)0)->_tx);
SIZE(zephyr_rx, ((nsx_sensor_zephyr_context_t *)0)->_rx);
#endif
