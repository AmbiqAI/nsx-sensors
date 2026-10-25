/* SPDX-License-Identifier: BSD-3-Clause */
#include "t5838_pair.h"
nsx_sensor_status_t spark_t5838_configure_pair(t5838_context_t *left,
    t5838_context_t *right, const t5838_config_t *config,
    const spark_microphone_clock_t *clock)
{
    if (!left || !right || left == right || !clock || !clock->set_clock_hz ||
        !clock->runtime.delay_us) return NSX_SENSOR_E_ARGUMENT;
    nsx_sensor_status_t s = t5838_validate_config(config);
    if (s != NSX_SENSOR_OK) return s;
    const t5838_config_t settings = *config;
    uint32_t final_clock = settings.mode == T5838_AAD_ANALOG ||
        settings.mode == T5838_AAD_DIGITAL2 ? 0 : 750000;
    /* Clock changes can affect both devices, even if programming one fails. */
    t5838_invalidate(left); t5838_invalidate(right);
    s = clock->set_clock_hz(clock->user, 100000);
    if (s != NSX_SENSOR_OK) goto fail;
    clock->runtime.delay_us(clock->runtime.user, 2000);
    s = t5838_init(left, &settings);
    if (s != NSX_SENSOR_OK) goto fail;
    s = t5838_init(right, &settings);
    if (s != NSX_SENSOR_OK) goto fail;
    s = t5838_enable(left);
    if (s != NSX_SENSOR_OK) goto fail;
    s = t5838_enable(right);
    if (s != NSX_SENSOR_OK) goto fail;
    clock->runtime.delay_us(clock->runtime.user, 2000);
    s = clock->set_clock_hz(clock->user, final_clock);
    if (s == NSX_SENSOR_OK) return s;
fail:
    t5838_invalidate(left); t5838_invalidate(right);
    return s;
}
