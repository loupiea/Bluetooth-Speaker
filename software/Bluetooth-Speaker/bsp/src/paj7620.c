#include "paj7620.h"

#include "app_events.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "i2c_bus.h"
#include "sdkconfig.h"

#define PAJ7620_REG_BANK_SELECT 0xEF
#define PAJ7620_REG_GESTURE_FLAG_1 0x43
#define PAJ7620_REG_GESTURE_FLAG_2 0x44
#define PAJ7620_WRITE_RETRY_COUNT 3

#define PAJ7620_FLAG_UP 0x01
#define PAJ7620_FLAG_DOWN 0x02
#define PAJ7620_FLAG_LEFT 0x04
#define PAJ7620_FLAG_RIGHT 0x08
#define PAJ7620_FLAG_FORWARD 0x10
#define PAJ7620_FLAG_BACKWARD 0x20
#define PAJ7620_FLAG_CLOCKWISE 0x40
#define PAJ7620_FLAG_COUNTER_CLOCKWISE 0x80
#define PAJ7620_FLAG_WAVE 0x01

static const char *TAG = "paj7620";
static i2c_master_dev_handle_t s_paj_dev;
static TaskHandle_t s_paj_task_handle;
static bool s_paj_ready;
static bool s_int_ready;

typedef struct {
    uint8_t reg;
    uint8_t val;
} paj7620_reg_pair_t;

static esp_err_t paj7620_write_reg(uint8_t reg, uint8_t val)
{
    uint8_t tx[] = { reg, val };
    esp_err_t ret = ESP_OK;
    for (uint8_t attempt = 1; attempt <= PAJ7620_WRITE_RETRY_COUNT; ++attempt) {
        ret = i2c_bus_lock(pdMS_TO_TICKS(150));
        if (ret == ESP_OK) {
            ret = i2c_master_transmit(s_paj_dev, tx, sizeof(tx), pdMS_TO_TICKS(100));
            i2c_bus_unlock();
        }
        if (ret == ESP_OK) {
            return ESP_OK;
        }
        ESP_LOGD(TAG, "Write reg 0x%02X failed on attempt %u/%u: %s",
                 reg, attempt, PAJ7620_WRITE_RETRY_COUNT, esp_err_to_name(ret));
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    return ret;
}

static esp_err_t paj7620_read_reg(uint8_t reg, uint8_t *val)
{
    esp_err_t ret = i2c_bus_lock(pdMS_TO_TICKS(150));
    if (ret != ESP_OK) {
        return ret;
    }
    ret = i2c_master_transmit_receive(s_paj_dev, &reg, 1, val, 1, pdMS_TO_TICKS(100));
    i2c_bus_unlock();
    return ret;
}

static esp_err_t paj7620_select_bank(uint8_t bank)
{
    return paj7620_write_reg(PAJ7620_REG_BANK_SELECT, bank);
}

static esp_err_t paj7620_write_table(const paj7620_reg_pair_t *table, size_t table_size)
{
    for (size_t i = 0; i < table_size; ++i) {
        ESP_RETURN_ON_ERROR(paj7620_write_reg(table[i].reg, table[i].val),
                            TAG, "init reg 0x%02X failed", table[i].reg);
    }
    return ESP_OK;
}

static app_gesture_t paj7620_decode(uint8_t flag1, uint8_t flag2)
{
    if (flag1 & PAJ7620_FLAG_UP) {
        return APP_GESTURE_UP;
    }
    if (flag1 & PAJ7620_FLAG_DOWN) {
        return APP_GESTURE_DOWN;
    }
    if (flag1 & PAJ7620_FLAG_LEFT) {
        return APP_GESTURE_LEFT;
    }
    if (flag1 & PAJ7620_FLAG_RIGHT) {
        return APP_GESTURE_RIGHT;
    }
    if (flag1 & PAJ7620_FLAG_FORWARD) {
        return APP_GESTURE_FORWARD;
    }
    if (flag1 & PAJ7620_FLAG_BACKWARD) {
        return APP_GESTURE_BACKWARD;
    }
    if (flag1 & PAJ7620_FLAG_CLOCKWISE) {
        return APP_GESTURE_CLOCKWISE;
    }
    if (flag1 & PAJ7620_FLAG_COUNTER_CLOCKWISE) {
        return APP_GESTURE_COUNTER_CLOCKWISE;
    }
    if (flag2 & PAJ7620_FLAG_WAVE) {
        return APP_GESTURE_WAVE;
    }
    return APP_GESTURE_NONE;
}

static void IRAM_ATTR paj7620_isr_handler(void *arg)
{
    (void)arg;
    TaskHandle_t task_handle = s_paj_task_handle;
    if (task_handle == NULL) {
        return;
    }

    BaseType_t higher_priority_task_woken = pdFALSE;
    xTaskNotifyFromISR(task_handle, 0, eIncrement, &higher_priority_task_woken);
    if (higher_priority_task_woken == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}

static esp_err_t paj7620_init_interrupt(void)
{
    if (s_int_ready) {
        return ESP_OK;
    }

    gpio_config_t config = {
        .pin_bit_mask = 1ULL << CONFIG_SMART_SPEAKER_PAJ7620_INT_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&config), TAG, "configure PAJ7620 INT gpio failed");

    esp_err_t ret = gpio_install_isr_service(0);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_RETURN_ON_ERROR(ret, TAG, "install gpio isr service failed");
    }

    ESP_RETURN_ON_ERROR(gpio_isr_handler_add(CONFIG_SMART_SPEAKER_PAJ7620_INT_GPIO,
                                             paj7620_isr_handler, NULL),
                        TAG, "add PAJ7620 INT isr failed");
    s_int_ready = true;
    return ESP_OK;
}

static esp_err_t paj7620_do_init(void)
{
    uint8_t chip_id0 = 0;
    uint8_t chip_id1 = 0;
    ESP_RETURN_ON_ERROR(paj7620_read_reg(0x00, &chip_id0), TAG, "read chip id low failed");
    ESP_RETURN_ON_ERROR(paj7620_read_reg(0x01, &chip_id1), TAG, "read chip id high failed");
    ESP_LOGI(TAG, "PAJ7620 chip id: 0x%02X 0x%02X", chip_id0, chip_id1);

    ESP_RETURN_ON_ERROR(paj7620_select_bank(0), TAG, "wake select bank 0 failed");
    ESP_RETURN_ON_ERROR(paj7620_select_bank(0), TAG, "confirm select bank 0 failed");

    const paj7620_reg_pair_t init_table[] = {
        { 0xEF, 0x00 }, { 0x41, 0x00 }, { 0x42, 0x00 }, { 0x37, 0x07 },
        { 0x38, 0x17 }, { 0x39, 0x06 }, { 0x42, 0x01 }, { 0x46, 0x2D },
        { 0x47, 0x0F }, { 0x48, 0x3C }, { 0x49, 0x00 }, { 0x4A, 0x1E },
        { 0x4C, 0x22 }, { 0x51, 0x10 }, { 0x5E, 0x10 }, { 0x60, 0x27 },
        { 0x80, 0x42 }, { 0x81, 0x44 }, { 0x82, 0x04 }, { 0x8B, 0x01 },
        { 0x90, 0x06 }, { 0x95, 0x0A }, { 0x96, 0x0C }, { 0x97, 0x05 },
        { 0x9A, 0x14 }, { 0x9C, 0x3F }, { 0xA5, 0x19 }, { 0xCC, 0x19 },
        { 0xCD, 0x0B }, { 0xCE, 0x13 }, { 0xCF, 0x64 }, { 0xD0, 0x21 },
        { 0xEF, 0x01 }, { 0x02, 0x0F }, { 0x03, 0x10 }, { 0x04, 0x02 },
        { 0x25, 0x01 }, { 0x27, 0x39 }, { 0x28, 0x7F }, { 0x29, 0x08 },
        { 0x3E, 0xFF }, { 0x5E, 0x3D }, { 0x65, 0x96 }, { 0x67, 0x97 },
        { 0x69, 0xCD }, { 0x6A, 0x01 }, { 0x6D, 0x2C }, { 0x6E, 0x01 },
        { 0x72, 0x01 }, { 0x73, 0x35 }, { 0x74, 0x00 }, { 0x77, 0x01 },
        { 0xEF, 0x00 }, { 0x41, 0xFF }, { 0x42, 0x01 },
    };

    const paj7620_reg_pair_t gesture_mode_table[] = {
        { 0xEF, 0x00 }, { 0x41, 0x00 }, { 0x42, 0x00 }, { 0x48, 0x3C },
        { 0x49, 0x00 }, { 0x51, 0x10 }, { 0x83, 0x20 }, { 0x9F, 0xF9 },
        { 0xEF, 0x01 }, { 0x01, 0x1E }, { 0x02, 0x0F }, { 0x03, 0x10 },
        { 0x04, 0x02 }, { 0x41, 0x40 }, { 0x43, 0x30 }, { 0x65, 0x96 },
        { 0x66, 0x00 }, { 0x67, 0x97 }, { 0x68, 0x01 }, { 0x69, 0xCD },
        { 0x6A, 0x01 }, { 0x6B, 0xB0 }, { 0x6C, 0x04 }, { 0x6D, 0x2C },
        { 0x6E, 0x01 }, { 0x74, 0x00 }, { 0xEF, 0x00 }, { 0x41, 0xFF },
        { 0x42, 0x01 },
    };

    ESP_RETURN_ON_ERROR(paj7620_write_table(init_table, sizeof(init_table) / sizeof(init_table[0])),
                        TAG, "write init table failed");
    ESP_RETURN_ON_ERROR(paj7620_write_table(gesture_mode_table,
                                            sizeof(gesture_mode_table) / sizeof(gesture_mode_table[0])),
                        TAG, "write gesture mode table failed");
    ESP_RETURN_ON_ERROR(paj7620_select_bank(0), TAG, "select bank 0 failed");
    return ESP_OK;
}

esp_err_t paj7620_init(void)
{
    uint8_t device_address = CONFIG_SMART_SPEAKER_PAJ7620_I2C_ADDR;
    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = device_address,
        .scl_speed_hz = CONFIG_SMART_SPEAKER_PAJ7620_I2C_FREQ_HZ,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(i2c_bus_get_handle(), &dev_config, &s_paj_dev),
                        TAG, "add PAJ7620 failed");

    ESP_LOGI(TAG, "PAJ7620 device registered at 0x%02X, recovery task will initialize sensor",
             device_address);
    return ESP_OK;
}

void paj7620_task(void *arg)
{
    QueueHandle_t event_queue = (QueueHandle_t)arg;
    app_gesture_t last_gesture = APP_GESTURE_NONE;
    s_paj_task_handle = xTaskGetCurrentTaskHandle();

    ESP_LOGI(TAG, "PAJ7620 recovery task waiting %d ms before first init",
             CONFIG_SMART_SPEAKER_PAJ7620_POWER_ON_DELAY_MS);
    vTaskDelay(pdMS_TO_TICKS(CONFIG_SMART_SPEAKER_PAJ7620_POWER_ON_DELAY_MS));

    while (!s_paj_ready) {
        esp_err_t ret = paj7620_do_init();
        if (ret == ESP_OK) {
            ret = paj7620_init_interrupt();
        }

        if (ret == ESP_OK) {
            s_paj_ready = true;
            ESP_LOGI(TAG, "PAJ7620 ready at 0x%02X, i2c=%d Hz, int=GPIO%d falling edge",
                     CONFIG_SMART_SPEAKER_PAJ7620_I2C_ADDR,
                     CONFIG_SMART_SPEAKER_PAJ7620_I2C_FREQ_HZ,
                     CONFIG_SMART_SPEAKER_PAJ7620_INT_GPIO);

            app_event_t ready_event = {
                .type = APP_EVENT_GESTURE_SENSOR_READY,
                .gesture = APP_GESTURE_NONE,
                .button = APP_BUTTON_NONE,
                .message = "PAJ7620 ready",
            };
            xQueueSend(event_queue, &ready_event, 0);
            break;
        }

        ESP_LOGW(TAG, "PAJ7620 not ready, retry in %d ms: %s",
                 CONFIG_SMART_SPEAKER_PAJ7620_RECOVERY_RETRY_MS,
                 esp_err_to_name(ret));
        vTaskDelay(pdMS_TO_TICKS(CONFIG_SMART_SPEAKER_PAJ7620_RECOVERY_RETRY_MS));
    }

    ESP_LOGI(TAG, "Gesture interrupt started on GPIO%d",
             CONFIG_SMART_SPEAKER_PAJ7620_INT_GPIO);

    while (true) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        uint8_t flag1 = 0;
        uint8_t flag2 = 0;
        esp_err_t ret = paj7620_read_reg(PAJ7620_REG_GESTURE_FLAG_1, &flag1);
        if (ret == ESP_OK) {
            ret = paj7620_read_reg(PAJ7620_REG_GESTURE_FLAG_2, &flag2);
        }

        if (ret == ESP_OK) {
            app_gesture_t gesture = paj7620_decode(flag1, flag2);
            if (gesture != APP_GESTURE_NONE && gesture != last_gesture) {
                ESP_LOGI(TAG, "Gesture: %s", app_gesture_to_string(gesture));
                app_event_t event = {
                    .type = APP_EVENT_GESTURE,
                    .gesture = gesture,
                    .message = NULL,
                };
                xQueueSend(event_queue, &event, 0);
                last_gesture = gesture;
            } else if (gesture == APP_GESTURE_NONE) {
                last_gesture = APP_GESTURE_NONE;
            }
        } else {
            ESP_LOGW(TAG, "Gesture read failed: %s", esp_err_to_name(ret));
        }
    }
}
