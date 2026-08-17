#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t audio_http_player_init(QueueHandle_t event_queue);
esp_err_t audio_http_player_play_url_async(const char *url);
esp_err_t audio_http_player_stop(void);
bool audio_http_player_is_playing(void);

#ifdef __cplusplus
}
#endif
