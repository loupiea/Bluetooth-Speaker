#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AUDIO_OPUS_SAMPLE_RATE_HZ 16000
#define AUDIO_OPUS_FRAME_DURATION_MS 60
#define AUDIO_OPUS_FRAME_SAMPLES \
    ((AUDIO_OPUS_SAMPLE_RATE_HZ / 1000) * AUDIO_OPUS_FRAME_DURATION_MS)

esp_err_t audio_opus_init(void);
void audio_opus_deinit(void);
esp_err_t audio_opus_encode_frame(const int16_t *pcm,
                                  size_t sample_count,
                                  uint8_t *opus,
                                  size_t opus_size,
                                  size_t *encoded_bytes);
size_t audio_opus_get_out_buffer_size(void);

#ifdef __cplusplus
}
#endif
