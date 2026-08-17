#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t xiaozhi_tts_player_init(void);
esp_err_t xiaozhi_tts_player_start(void);
esp_err_t xiaozhi_tts_player_write_opus(const uint8_t *data, size_t len);
esp_err_t xiaozhi_tts_player_stop(void);
bool xiaozhi_tts_player_is_playing(void);

#ifdef __cplusplus
}
#endif
