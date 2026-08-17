#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t audio_recorder_init(QueueHandle_t event_queue);
esp_err_t audio_recorder_toggle(void);
bool audio_recorder_is_recording(void);

#ifdef __cplusplus
}
#endif
