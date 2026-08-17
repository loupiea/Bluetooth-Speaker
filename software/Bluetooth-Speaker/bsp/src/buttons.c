#include "buttons.h"

#include "app_events.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/task.h"
#include "sdkconfig.h"

typedef struct {
    gpio_num_t gpio;
    app_button_action_t action;
    const char *name;
    TickType_t last_tick;
} button_config_t;

static const char *TAG = "buttons";
static QueueHandle_t s_event_queue;
static QueueHandle_t s_isr_queue;

static button_config_t s_buttons[] = {
    { CONFIG_SMART_SPEAKER_BUTTON_MAIN_GPIO, APP_BUTTON_MAIN, "main", 0 },
    { CONFIG_SMART_SPEAKER_BUTTON_BACK_MUTE_GPIO, APP_BUTTON_BACK_MUTE, "back/mute", 0 },
    { CONFIG_SMART_SPEAKER_BUTTON_VOLUME_UP_GPIO, APP_BUTTON_VOLUME_UP, "volume up", 0 },
    { CONFIG_SMART_SPEAKER_BUTTON_VOLUME_DOWN_GPIO, APP_BUTTON_VOLUME_DOWN, "volume down", 0 },
};

static void IRAM_ATTR button_isr_handler(void *arg)
{
    button_config_t *button = (button_config_t *)arg;
    gpio_intr_disable(button->gpio);

    BaseType_t higher_priority_task_woken = pdFALSE;
    xQueueSendFromISR(s_isr_queue, &button, &higher_priority_task_woken);
    if (higher_priority_task_woken == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}

static void buttons_task(void *arg)
{
    (void)arg;
    button_config_t *button = NULL;

    while (true) {
        if (xQueueReceive(s_isr_queue, &button, portMAX_DELAY) != pdTRUE || button == NULL) {
            continue;
        }

        vTaskDelay(pdMS_TO_TICKS(CONFIG_SMART_SPEAKER_BUTTON_CONFIRM_MS));
        TickType_t now = xTaskGetTickCount();
        TickType_t guard_ticks = pdMS_TO_TICKS(CONFIG_SMART_SPEAKER_BUTTON_GUARD_MS);
        bool guard_ok = guard_ticks == 0 || button->last_tick == 0 ||
                        now - button->last_tick >= guard_ticks;

        if (guard_ok && gpio_get_level(button->gpio) == 0) {
            TickType_t press_start = now;
            while (gpio_get_level(button->gpio) == 0) {
                vTaskDelay(pdMS_TO_TICKS(5));
            }

            TickType_t release_tick = xTaskGetTickCount();
            TickType_t press_ticks = release_tick - press_start;
            app_button_action_t action = button->action;
            if (button->action == APP_BUTTON_MAIN &&
                press_ticks >= pdMS_TO_TICKS(CONFIG_SMART_SPEAKER_BUTTON_LONG_PRESS_MS)) {
                action = APP_BUTTON_MAIN_LONG;
            } else if (button->action == APP_BUTTON_VOLUME_UP &&
                       press_ticks >= pdMS_TO_TICKS(CONFIG_SMART_SPEAKER_BUTTON_LONG_PRESS_MS)) {
                action = APP_BUTTON_VOLUME_UP_LONG;
            } else if (button->action == APP_BUTTON_VOLUME_DOWN &&
                       press_ticks >= pdMS_TO_TICKS(CONFIG_SMART_SPEAKER_BUTTON_LONG_PRESS_MS)) {
                action = APP_BUTTON_VOLUME_DOWN_LONG;
            }

            button->last_tick = release_tick;
            app_event_t event = {
                .type = APP_EVENT_BUTTON,
                .gesture = APP_GESTURE_NONE,
                .button = action,
                .message = NULL,
            };
            xQueueSend(s_event_queue, &event, 0);
        } else {
            while (gpio_get_level(button->gpio) == 0) {
                vTaskDelay(pdMS_TO_TICKS(5));
            }
        }

        vTaskDelay(pdMS_TO_TICKS(CONFIG_SMART_SPEAKER_BUTTON_CONFIRM_MS));
        gpio_intr_enable(button->gpio);
    }
}

esp_err_t buttons_init(QueueHandle_t event_queue)
{
    if (event_queue == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    s_event_queue = event_queue;
    s_isr_queue = xQueueCreate(sizeof(s_buttons) / sizeof(s_buttons[0]), sizeof(button_config_t *));
    if (s_isr_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }

    uint64_t pin_mask = 0;
    for (size_t i = 0; i < sizeof(s_buttons) / sizeof(s_buttons[0]); ++i) {
        pin_mask |= 1ULL << s_buttons[i].gpio;
    }

    gpio_config_t config = {
        .pin_bit_mask = pin_mask,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&config), TAG, "configure button gpio failed");

    esp_err_t ret = gpio_install_isr_service(0);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_RETURN_ON_ERROR(ret, TAG, "install gpio isr service failed");
    }

    for (size_t i = 0; i < sizeof(s_buttons) / sizeof(s_buttons[0]); ++i) {
        ESP_RETURN_ON_ERROR(gpio_isr_handler_add(s_buttons[i].gpio, button_isr_handler,
                                                 (void *)&s_buttons[i]),
                            TAG, "add isr for gpio %d failed", s_buttons[i].gpio);
        ESP_LOGI(TAG, "Button %s ready on GPIO%d, guard=%d ms, confirm=%d ms, long=%d ms",
                 s_buttons[i].name, s_buttons[i].gpio,
                 CONFIG_SMART_SPEAKER_BUTTON_GUARD_MS,
                 CONFIG_SMART_SPEAKER_BUTTON_CONFIRM_MS,
                 CONFIG_SMART_SPEAKER_BUTTON_LONG_PRESS_MS);
    }

    BaseType_t task_ret = xTaskCreate(buttons_task, "buttons", 3072, NULL, 10, NULL);
    if (task_ret != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}
