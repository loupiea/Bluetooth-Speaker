#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

#define APP_I2C_PORT 0

esp_err_t i2c_bus_init(void);
i2c_master_bus_handle_t i2c_bus_get_handle(void);
esp_err_t i2c_bus_lock(TickType_t timeout_ticks);
void i2c_bus_unlock(void);

#ifdef __cplusplus
}
#endif
