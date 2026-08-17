#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t audio_output_init(void);
esp_err_t audio_output_start(void);
esp_err_t audio_output_write_pcm(const int16_t *samples, size_t sample_count);
esp_err_t audio_output_stop(void);
esp_err_t audio_output_play_tone(uint32_t tone_hz, uint32_t duration_ms);
esp_err_t audio_output_play_test_tone(uint32_t duration_ms);
esp_err_t audio_output_set_volume(uint8_t volume_percent);
esp_err_t audio_output_volume_up(void);
esp_err_t audio_output_volume_down(void);
uint8_t audio_output_get_volume(void);
bool audio_output_is_ready(void);
bool audio_output_is_active(void);

#ifdef __cplusplus
}
#endif
