#pragma once

#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t ai_music_control_play_default(void);
esp_err_t ai_music_control_play_by_name(const char *query);
esp_err_t ai_music_control_play_next(void);
esp_err_t ai_music_control_format_list(char *buffer, size_t buffer_size);
esp_err_t ai_music_control_stop(void);

#ifdef __cplusplus
}
#endif
