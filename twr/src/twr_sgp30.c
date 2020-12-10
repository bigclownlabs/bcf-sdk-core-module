#include <twr_sgp30.h>

#define _TWR_SGP30_DELAY_RUN 100
#define _TWR_SGP30_DELAY_INITIALIZE 500
#define _TWR_SGP30_DELAY_READ_FEATURE_SET 30
#define _TWR_SGP30_DELAY_INIT_AIR_QUALITY 30
#define _TWR_SGP30_DELAY_SET_HUMIDITY 30
#define _TWR_SGP30_DELAY_MEASURE_AIR_QUALITY 30
#define _TWR_SGP30_DELAY_READ_AIR_QUALITY 30

static void _twr_sgp30_task_interval(void *param);

static void _twr_sgp30_task_measure(void *param);

static uint8_t _twr_sgp30_calculate_crc(uint8_t *buffer, size_t length);

void twr_sgp30_init(twr_sgp30_t *self, twr_i2c_channel_t i2c_channel, uint8_t i2c_address)
{
    memset(self, 0, sizeof(*self));

    self->_i2c_channel = i2c_channel;
    self->_i2c_address = i2c_address;

    self->_task_id_interval = twr_scheduler_register(_twr_sgp30_task_interval, self, TWR_TICK_INFINITY);
    self->_task_id_measure = twr_scheduler_register(_twr_sgp30_task_measure, self, _TWR_SGP30_DELAY_RUN);

    self->_tick_ready = _TWR_SGP30_DELAY_RUN;

    twr_i2c_init(self->_i2c_channel, TWR_I2C_SPEED_100_KHZ);
}

void twr_sgp30_set_event_handler(twr_sgp30_t *self, void (*event_handler)(twr_sgp30_t *, twr_sgp30_event_t, void *), void *event_param)
{
    self->_event_handler = event_handler;
    self->_event_param = event_param;
}

void twr_sgp30_set_update_interval(twr_sgp30_t *self, twr_tick_t interval)
{
    self->_update_interval = interval;

    if (self->_update_interval == TWR_TICK_INFINITY)
    {
        twr_scheduler_plan_absolute(self->_task_id_interval, TWR_TICK_INFINITY);
    }
    else
    {
        twr_scheduler_plan_relative(self->_task_id_interval, self->_update_interval);

        twr_sgp30_measure(self);
    }
}

bool twr_sgp30_measure(twr_sgp30_t *self)
{
    if (self->_event_handler == NULL)
    {
        return true;
    }

    if (self->_hit_error && !self->_measurement_valid)
    {
        self->_event_handler(self, TWR_SGP30_EVENT_ERROR, self->_event_param);
    }
    else if (self->_measurement_valid)
    {
        self->_event_handler(self, TWR_SGP30_EVENT_UPDATE, self->_event_param);
    }

    return true;
}

bool twr_sgp30_get_co2eq_ppm(twr_sgp30_t *self, uint16_t *ppm)
{
    if (!self->_measurement_valid)
    {
        return false;
    }

    *ppm = self->_co2eq;

    return true;
}

bool twr_sgp30_get_tvoc_ppb(twr_sgp30_t *self, uint16_t *ppb)
{
    if (!self->_measurement_valid)
    {
        return false;
    }

    *ppb = self->_tvoc;

    return true;
}

float twr_sgp30_set_compensation(twr_sgp30_t *self, float *t_celsius, float *rh_percentage)
{
    if (t_celsius == NULL || rh_percentage == NULL)
    {
        self->_ah_scaled = 0;

        return 0.f;
    }

    double t = *t_celsius;
    double rh = *rh_percentage;
    double pws = 611.2 * exp(17.67 * t / (243.5 + t));
    double pw = pws * rh / 100.0;
    double ah = 2.165 * pw / (273.15 + t);

    self->_ah_scaled = ((uint64_t) (ah * 1000.0) * 256 * 16777) >> 24;

    return ah;
}

static void _twr_sgp30_task_interval(void *param)
{
    twr_sgp30_t *self = param;

    twr_sgp30_measure(self);

    twr_scheduler_plan_current_relative(self->_update_interval);
}

static void _twr_sgp30_task_measure(void *param)
{
    twr_sgp30_t *self = param;

start:

    switch (self->_state)
    {
        case TWR_SGP30_STATE_ERROR:
        {
            self->_hit_error = true;

            self->_measurement_valid = false;

            self->_state = TWR_SGP30_STATE_INITIALIZE;

            twr_scheduler_plan_current_from_now(_TWR_SGP30_DELAY_INITIALIZE);

            return;
        }
        case TWR_SGP30_STATE_INITIALIZE:
        {
            self->_state = TWR_SGP30_STATE_GET_FEATURE_SET;

            goto start;
        }
        case TWR_SGP30_STATE_GET_FEATURE_SET:
        {
            self->_state = TWR_SGP30_STATE_ERROR;

            static const uint8_t buffer[] = { 0x20, 0x2f };

            twr_i2c_transfer_t transfer;

            transfer.device_address = self->_i2c_address;
            transfer.buffer = (uint8_t *) buffer;
            transfer.length = sizeof(buffer);

            if (!twr_i2c_write(self->_i2c_channel, &transfer))
            {
                goto start;
            }

            self->_state = TWR_SGP30_STATE_READ_FEATURE_SET;

            twr_scheduler_plan_current_from_now(_TWR_SGP30_DELAY_READ_FEATURE_SET);

            return;
        }
        case TWR_SGP30_STATE_READ_FEATURE_SET:
        {
            self->_state = TWR_SGP30_STATE_ERROR;

            uint8_t buffer[3];

            twr_i2c_transfer_t transfer;

            transfer.device_address = self->_i2c_address;
            transfer.buffer = buffer;
            transfer.length = sizeof(buffer);

            if (!twr_i2c_read(self->_i2c_channel, &transfer))
            {
                goto start;
            }

            if (_twr_sgp30_calculate_crc(&buffer[0], 3) != 0)
            {
                goto start;
            }

            if (buffer[0] != 0x00 || (buffer[1] != 0x20 && buffer[1] != 0x22))
            {
                goto start;
            }

            self->_state = TWR_SGP30_STATE_INIT_AIR_QUALITY;

            twr_scheduler_plan_current_from_now(_TWR_SGP30_DELAY_INIT_AIR_QUALITY);

            return;
        }
        case TWR_SGP30_STATE_INIT_AIR_QUALITY:
        {
            self->_state = TWR_SGP30_STATE_ERROR;

            static const uint8_t buffer[] = { 0x20, 0x03 };

            twr_i2c_transfer_t transfer;

            transfer.device_address = self->_i2c_address;
            transfer.buffer = (uint8_t *) buffer;
            transfer.length = sizeof(buffer);

            if (!twr_i2c_write(self->_i2c_channel, &transfer))
            {
                goto start;
            }

            self->_state = TWR_SGP30_STATE_SET_HUMIDITY;

            twr_scheduler_plan_current_from_now(_TWR_SGP30_DELAY_SET_HUMIDITY);

            return;
        }
        case TWR_SGP30_STATE_SET_HUMIDITY:
        {
            self->_state = TWR_SGP30_STATE_ERROR;

            uint8_t buffer[5];

            buffer[0] = 0x20;
            buffer[1] = 0x61;
            buffer[2] = self->_ah_scaled >> 8;
            buffer[3] = self->_ah_scaled;
            buffer[4] = _twr_sgp30_calculate_crc(&buffer[2], 2);

            twr_i2c_transfer_t transfer;

            transfer.device_address = self->_i2c_address;
            transfer.buffer = buffer;
            transfer.length = sizeof(buffer);

            if (!twr_i2c_write(self->_i2c_channel, &transfer))
            {
                goto start;
            }

            self->_state = TWR_SGP30_STATE_MEASURE_AIR_QUALITY;

            twr_scheduler_plan_current_from_now(_TWR_SGP30_DELAY_MEASURE_AIR_QUALITY);

            self->_tick_last_measurement = twr_scheduler_get_spin_tick();

            return;
        }
        case TWR_SGP30_STATE_MEASURE_AIR_QUALITY:
        {
            self->_state = TWR_SGP30_STATE_ERROR;

            static const uint8_t buffer[] = { 0x20, 0x08 };

            twr_i2c_transfer_t transfer;

            transfer.device_address = self->_i2c_address;
            transfer.buffer = (uint8_t *) buffer;
            transfer.length = sizeof(buffer);

            if (!twr_i2c_write(self->_i2c_channel, &transfer))
            {
                goto start;
            }

            self->_state = TWR_SGP30_STATE_READ_AIR_QUALITY;

            twr_scheduler_plan_current_from_now(_TWR_SGP30_DELAY_READ_AIR_QUALITY);

            return;
        }
        case TWR_SGP30_STATE_READ_AIR_QUALITY:
        {
            self->_state = TWR_SGP30_STATE_ERROR;

            uint8_t buffer[6];

            twr_i2c_transfer_t transfer;

            transfer.device_address = self->_i2c_address;
            transfer.buffer = buffer;
            transfer.length = sizeof(buffer);

            if (!twr_i2c_read(self->_i2c_channel, &transfer))
            {
                goto start;
            }

            if (_twr_sgp30_calculate_crc(&buffer[0], 3) != 0 ||
                _twr_sgp30_calculate_crc(&buffer[3], 3) != 0)
            {
                goto start;
            }

            self->_co2eq = (buffer[0] << 8) | buffer[1];
            self->_tvoc = (buffer[3] << 8) | buffer[4];

            self->_measurement_valid = true;

            self->_state = TWR_SGP30_STATE_SET_HUMIDITY;

            twr_scheduler_plan_current_absolute(self->_tick_last_measurement + 1000);

            return;
        }
        default:
        {
            self->_state = TWR_SGP30_STATE_ERROR;

            goto start;
        }
    }
}

static uint8_t _twr_sgp30_calculate_crc(uint8_t *buffer, size_t length)
{
    uint8_t crc = 0xff;

    for (size_t i = 0; i < length; i++)
    {
        crc ^= buffer[i];

        for (int j = 0; j < 8; j++)
        {
            if ((crc & 0x80) != 0)
            {
                crc = (crc << 1) ^ 0x31;
            }
            else
            {
                crc <<= 1;
            }
        }
    }

    return crc;
}
