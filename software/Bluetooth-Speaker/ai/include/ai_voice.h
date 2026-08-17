#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t ai_voice_init(QueueHandle_t event_queue);
esp_err_t ai_voice_submit_latest_recording(void);
bool ai_voice_is_pending(void);

#ifdef __cplusplus
}
#endif
