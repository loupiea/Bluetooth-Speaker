#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t voice_wakeup_init(QueueHandle_t event_queue);
esp_err_t voice_wakeup_start(void);
esp_err_t voice_wakeup_stop(void);
bool voice_wakeup_is_running(void);

#ifdef __cplusplus
}
#endif
