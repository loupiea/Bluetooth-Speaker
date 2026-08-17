#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AUDIO_PROMPT_RECORD_START = 0,
    AUDIO_PROMPT_RECORD_STOP,
    AUDIO_PROMPT_PLAYBACK_START,
    AUDIO_PROMPT_PLAYBACK_STOP,
    AUDIO_PROMPT_VOLUME,
    AUDIO_PROMPT_ERROR,
} audio_prompt_t;

esp_err_t audio_prompt_play(audio_prompt_t prompt);

#ifdef __cplusplus
}
#endif
