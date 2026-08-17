#include "i2c_bus.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "sdkconfig.h"

static const char *TAG = "i2c_bus";
static i2c_master_bus_handle_t s_bus_handle;
static SemaphoreHandle_t s_bus_mutex;

esp_err_t i2c_bus_init(void)
{
    if (s_bus_handle != NULL) {
        return ESP_OK;
    }

    if (s_bus_mutex == NULL) {
        s_bus_mutex = xSemaphoreCreateMutex();
        if (s_bus_mutex == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    i2c_master_bus_config_t config = {
        .i2c_port = APP_I2C_PORT,
        .sda_io_num = CONFIG_SMART_SPEAKER_I2C_SDA_GPIO,
        .scl_io_num = CONFIG_SMART_SPEAKER_I2C_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    esp_err_t ret = i2c_new_master_bus(&config, &s_bus_handle);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "I2C ready: SDA=%d SCL=%d",
                 CONFIG_SMART_SPEAKER_I2C_SDA_GPIO,
                 CONFIG_SMART_SPEAKER_I2C_SCL_GPIO);
    }
    return ret;
}

i2c_master_bus_handle_t i2c_bus_get_handle(void)
{
    return s_bus_handle;
}

esp_err_t i2c_bus_lock(TickType_t timeout_ticks)
{
    if (s_bus_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(s_bus_mutex, timeout_ticks) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

void i2c_bus_unlock(void)
{
    if (s_bus_mutex != NULL) {
        xSemaphoreGive(s_bus_mutex);
    }
}
