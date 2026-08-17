#include "audio_prompt.h"

#include <stddef.h>
#include <stdint.h>
#include "audio_output.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

typedef struct {
    uint32_t tone_hz;
    uint32_t duration_ms;
    uint32_t gap_ms;
} prompt_step_t;

static const char *TAG = "audio_prompt";

static const prompt_step_t s_record_start[] = {
    { 880, 70, 20 },
    { 1320, 90, 0 },
};

static const prompt_step_t s_record_stop[] = {
    { 1320, 70, 20 },
    { 880, 90, 0 },
};

static const prompt_step_t s_playback_start[] = {
    { 1040, 80, 0 },
};

static const prompt_step_t s_playback_stop[] = {
    { 660, 80, 0 },
};

static const prompt_step_t s_volume[] = {
    { 1200, 45, 0 },
};

static const prompt_step_t s_error[] = {
    { 320, 90, 30 },
    { 320, 90, 0 },
};

static const prompt_step_t *prompt_steps(audio_prompt_t prompt, size_t *count)
{
    switch (prompt) {
    case AUDIO_PROMPT_RECORD_START:
        *count = sizeof(s_record_start) / sizeof(s_record_start[0]);
        return s_record_start;
    case AUDIO_PROMPT_RECORD_STOP:
        *count = sizeof(s_record_stop) / sizeof(s_record_stop[0]);
        return s_record_stop;
    case AUDIO_PROMPT_PLAYBACK_START:
        *count = sizeof(s_playback_start) / sizeof(s_playback_start[0]);
        return s_playback_start;
    case AUDIO_PROMPT_PLAYBACK_STOP:
        *count = sizeof(s_playback_stop) / sizeof(s_playback_stop[0]);
        return s_playback_stop;
    case AUDIO_PROMPT_VOLUME:
        *count = sizeof(s_volume) / sizeof(s_volume[0]);
        return s_volume;
    case AUDIO_PROMPT_ERROR:
        *count = sizeof(s_error) / sizeof(s_error[0]);
        return s_error;
    default:
        *count = 0;
        return NULL;
    }
}

esp_err_t audio_prompt_play(audio_prompt_t prompt)
{
    if (audio_output_is_active()) {
        ESP_LOGI(TAG, "Prompt skipped while audio output is active");
        return ESP_OK;
    }

    size_t count = 0;
    const prompt_step_t *steps = prompt_steps(prompt, &count);
    if (steps == NULL || count == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    for (size_t i = 0; i < count; ++i) {
        const prompt_step_t *step = &steps[i];
        ESP_RETURN_ON_ERROR(audio_output_play_tone(step->tone_hz, step->duration_ms),
                            TAG, "play prompt tone failed");
        if (step->gap_ms > 0) {
            vTaskDelay(pdMS_TO_TICKS(step->gap_ms));
        }
    }

    return ESP_OK;
}
