/* SPDX-License-Identifier: BSD-3-Clause */
#include <nsx/sensors/ledstick.h>
#include <assert.h>
#include <string.h>
typedef struct { uint8_t bytes[5]; size_t size; unsigned calls; nsx_sensor_status_t error; } fake_t;
static nsx_sensor_status_t transfer(void *user, const nsx_sensor_transfer_t *t)
{
    fake_t *f = user;
    assert(t->tx_size <= sizeof(f->bytes) && t->rx_size == 0);
    memcpy(f->bytes, t->tx, t->tx_size); f->size = t->tx_size; ++f->calls;
    return f->error;
}
static const nsx_sensor_bus_ops_t ops = { transfer };
static void expect(fake_t *f, const uint8_t *bytes, size_t length)
{ assert(f->size == length && memcmp(f->bytes, bytes, length) == 0); }
int main(void)
{
    fake_t a = {0}, b = {0};
    nsx_sensor_bus_t bus = { &ops, &a, NSX_SENSOR_BUS_I2C, 5, 0 };
    uint8_t bytes[6] = {0};
    nsx_sensor_transfer_t t = {bytes, 6, NULL, 0};
    assert(nsx_sensor_bus_transfer(&bus, &t) == NSX_SENSOR_E_UNSUPPORTED);
    t.tx_size = 0;
    assert(nsx_sensor_bus_transfer(&bus, &t) == NSX_SENSOR_E_ARGUMENT);
    t.tx_size = 1; t.tx = NULL;
    assert(nsx_sensor_bus_transfer(&bus, &t) == NSX_SENSOR_E_ARGUMENT);
    t.tx = bytes; t.rx = bytes; t.rx_size = 1;
    assert(nsx_sensor_bus_transfer(&bus, &t) == NSX_SENSOR_E_UNSUPPORTED);
    assert(nsx_sensor_bus_transfer(NULL, &t) == NSX_SENSOR_E_ARGUMENT);
    assert(a.calls == 0);
    ledstick_context_t ca = {0}, cb = {0};
    ledstick_config_t config = { 10 }, bad = { 0 };
    assert(ledstick_bind(&ca, &bus) == NSX_SENSOR_OK && a.calls == 0);
    bus.user = &b;
    assert(ledstick_bind(&cb, &bus) == NSX_SENSOR_OK);
    bus.kind = NSX_SENSOR_BUS_SPI;
    assert(ledstick_bind(&cb, &bus) == NSX_SENSOR_E_UNSUPPORTED);
    assert(ledstick_set_all_off(&ca) == NSX_SENSOR_E_STATE);
    assert(ledstick_init(&ca, &bad) == NSX_SENSOR_E_ARGUMENT && a.calls == 0);
    assert(ledstick_set_led_length(&ca, 10) == NSX_SENSOR_OK);
    expect(&a, (uint8_t[]){0x70,10}, 2);
    assert(ledstick_init(&ca, &config) == NSX_SENSOR_OK);
    expect(&a, (uint8_t[]){0x78}, 1);
    assert(ledstick_init(&cb, &config) == NSX_SENSOR_OK);
    assert(ledstick_set_color(&ca, 10, 1, 2, 3) == NSX_SENSOR_OK);
    expect(&a, (uint8_t[]){0x71,10,1,2,3}, 5);
    assert(b.calls == 2);
    assert(ledstick_set_all_colors(&ca, 4, 5, 6) == NSX_SENSOR_OK);
    expect(&a, (uint8_t[]){0x72,4,5,6}, 4);
    assert(ledstick_set_brightness(&ca, 1, 31) == NSX_SENSOR_OK);
    expect(&a, (uint8_t[]){0x76,1,31}, 3);
    assert(ledstick_set_all_brightness(&ca, 0) == NSX_SENSOR_OK);
    expect(&a, (uint8_t[]){0x77,0}, 2);
    unsigned calls = a.calls;
    assert(ledstick_set_color(&ca, 0, 0, 0, 0) == NSX_SENSOR_E_ARGUMENT);
    assert(ledstick_set_color(&ca, 11, 0, 0, 0) == NSX_SENSOR_E_ARGUMENT);
    assert(ledstick_set_brightness(&ca, 1, 32) == NSX_SENSOR_E_ARGUMENT);
    assert(ledstick_set_all_brightness(&ca, 255) == NSX_SENSOR_E_ARGUMENT);
    assert(ledstick_change_address(&ca, 0x78) == NSX_SENSOR_E_ARGUMENT);
    assert(a.calls == calls);
    a.error = NSX_SENSOR_E_IO;
    assert(ledstick_set_all_off(&ca) == NSX_SENSOR_E_IO);
    assert(ledstick_set_led_length(&ca, 20) == NSX_SENSOR_E_IO);
    assert(ledstick_set_all_off(&ca) == NSX_SENSOR_E_STATE);
    a.error = NSX_SENSOR_OK;
    assert(ledstick_init(&ca, &config) == NSX_SENSOR_OK);
    assert(ledstick_change_address(&ca, 0x30) == NSX_SENSOR_OK);
    expect(&a, (uint8_t[]){0xc7,0x30}, 2);
    assert(ledstick_set_all_off(&ca) == NSX_SENSOR_E_STATE);
    bus.kind = NSX_SENSOR_BUS_I2C; bus.user = &a;
    assert(ledstick_bind(&ca, &bus) == NSX_SENSOR_OK);
    assert(ledstick_init(&ca, &config) == NSX_SENSOR_OK);
    a.error = NSX_SENSOR_E_IO;
    assert(ledstick_change_address(&ca, 0x31) == NSX_SENSOR_E_IO);
    assert(ledstick_init(&ca, &config) == NSX_SENSOR_E_STATE);
    assert(ledstick_set_all_off(&cb) == NSX_SENSOR_OK);
    return 0;
}
