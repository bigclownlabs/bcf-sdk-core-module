#ifndef _BC_RADIO_H
#define _BC_RADIO_H

#include <bc_common.h>

#define BC_RADIO_MAX_DEVICES 8

typedef enum
{
    BC_RADIO_EVENT_ATTACH = 0,
    BC_RADIO_EVENT_ATTACH_FAILURE = 1,
    BC_RADIO_EVENT_DETACH = 2,

} bc_radio_event_t;

void bc_radio_init(void);

void bc_radio_set_event_handler(void (*event_handler)(bc_radio_event_t, void *), void *event_param);

void bc_radio_listen(void);

void bc_radio_sleep(void);

void bc_radio_enroll_to_gateway(void);

void bc_radio_enrollment_start(void);

void bc_radio_enrollment_stop(void);

uint32_t bc_radio_get_event_device_address(void);

bool bc_radio_pub_push_button(uint16_t *event_count);

bool bc_radio_pub_thermometer(uint8_t i2c, float *temperature);

bool bc_radio_pub_humidity(uint8_t i2c, float *percentage);

bool bc_radio_pub_luminosity(uint8_t i2c, float *lux);

bool bc_radio_pub_barometer(uint8_t i2c, float *pascal, float *meter);

bool bc_radio_pub_co2(float *concentration);

bool bc_radio_pub_buffer(void *buffer, size_t length);

#endif // _BC_RADIO_H
