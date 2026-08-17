#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t audio_player_init(QueueHandle_t event_queue);
esp_err_t audio_player_play_file(const char *path);
esp_err_t audio_player_play_recording(uint16_t index);
esp_err_t audio_player_play_latest_recording(void);
esp_err_t audio_player_play_latest_recording_async(void);
esp_err_t audio_player_stop(void);
bool audio_player_is_playing(void);

#ifdef __cplusplus
}
#endif
