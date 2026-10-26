/* SPDX-License-Identifier: BSD-3-Clause */
/* Link fixture only: no sensor I/O or hardware qualification. */
#include <nsx/sensors/bme688.h>
#include <nsx/sensors/ledstick.h>
#include <nsx/sensors/ina228.h>
#include <nsx/sensors/mpu6050.h>
#include <nsx/sensors/max86150.h>
#include <nsx/sensors/icm45605.h>
#include <nsx/sensors/tsl254x.h>
#include <nsx/sensors/t5838.h>
#include <nsx/sensors/port_nsx.h>
#include <nsx_core.h>
#ifdef NSX_FREERTOS
#include <nsx_freertos.h>
#include <FreeRTOS.h>
#include <task.h>
#include <semphr.h>
static SemaphoreHandle_t iom_mutex;
static nsx_sensor_status_t acquire(void *user)
{
    return xSemaphoreTake((SemaphoreHandle_t)user, portMAX_DELAY) == pdTRUE
        ? NSX_SENSOR_OK : NSX_SENSOR_E_IO;
}
static void release(void *user)
{
    configASSERT(xSemaphoreGive((SemaphoreHandle_t)user) == pdTRUE);
}
void vApplicationMallocFailedHook(void) { configASSERT(0); }
void vApplicationStackOverflowHook(TaskHandle_t task, char *name)
{
    (void)task; (void)name; configASSERT(0);
}
#endif
/* Busy wait preserves sub-tick durations; never round down to scheduler ticks. */
static void delay_us(void *user, uint32_t us) { (void)user; nsx_delay_us(us); }
static volatile int result;
static nsx_sensor_nsx_context_t i2c_port, spi_port;
/* Deliberately uninitialized controllers: binding rejects these without I/O.
 * Real SDK transfer implementations remain reachable through adapter ops.
 * This fixture does not claim board pin or endpoint qualification. */
static nsx_i2c_config_t i2c;
static nsx_spi_config_t spi;
static bme688_context_t bme;
static void sensor_contract(void)
{
    const nsx_sensor_runtime_t runtime = { .delay_us = delay_us };
    nsx_sensor_nsx_lock_t lock = {0};
#ifdef NSX_FREERTOS
    lock = (nsx_sensor_nsx_lock_t){iom_mutex, acquire, release};
#endif
    result = nsx_sensor_nsx_bind_i2c(&i2c_port, &i2c, 0x76, &lock);
    result = nsx_sensor_nsx_bind_spi(&spi_port, &spi, 0, &lock);
    result = bme688_bind(&bme, &i2c_port.bus, &runtime);
    bme688_config_t bme688_config = bme688_default_config();
    result = bme688_validate_config(&bme688_config);
    ledstick_config_t ledstick_config = {10};
    result = ledstick_validate_config(&ledstick_config);
    ina228_config_t ina228_config = ina228_default_config();
    result = ina228_validate_config(&ina228_config);
    mpu6050_config_t mpu6050_config = mpu6050_default_config();
    result = mpu6050_validate_config(&mpu6050_config);
    max86150_config_t max86150_config = max86150_default_config();
    result = max86150_validate_config(&max86150_config);
    icm45605_config_t icm45605_config = icm45605_default_config();
    result = icm45605_validate_config(&icm45605_config);
    tsl254x_config_t tsl254x_config = tsl254x_default_config();
    result = tsl254x_validate_config(&tsl254x_config);
    t5838_config_t t5838_config = t5838_default_config();
    result = t5838_validate_config(&t5838_config);
}
#ifdef NSX_FREERTOS
static void sensor_task(void *argument)
{
    (void)argument;
    sensor_contract();
    for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
}
#endif
int main(void)
{
    /* CMSIS clock data is owned by the SDK system translation unit. */
    SystemCoreClockUpdate();
#ifdef NSX_FREERTOS
    iom_mutex = xSemaphoreCreateMutex();
    configASSERT(iom_mutex != NULL);
    configASSERT(xTaskCreate(sensor_task, "sensors", 1024, NULL, 1, NULL) == pdPASS);
    nsx_freertos_start();
#else
    sensor_contract();
#endif
    for (;;) {}
}
