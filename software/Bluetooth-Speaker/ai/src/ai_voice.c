#include "ai_voice.h"

#include <stdbool.h>
#include <stdio.h>

#include "app_events.h"
#include "esp_err.h"
#include "esp_log.h"

static const char *TAG = "ai_voice";

static QueueHandle_t s_event_queue;
static volatile bool s_pending;
static char s_message[64];

static void ai_voice_post_event(app_event_type_t type)
{
    if (s_event_queue == NULL) {
        return;
    }

    app_event_t event = {
        .type = type,
        .gesture = APP_GESTURE_NONE,
        .button = APP_BUTTON_NONE,
        .message = s_message,
    };
    xQueueSend(s_event_queue, &event, 0);
}

esp_err_t ai_voice_init(QueueHandle_t event_queue)
{
    if (event_queue == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    s_event_queue = event_queue;
    s_pending = false;
    snprintf(s_message, sizeof(s_message), "AI voice ready");

    ESP_LOGI(TAG, "AI voice bridge ready for XiaoZhi audio path");
    return ESP_OK;
}

esp_err_t ai_voice_submit_latest_recording(void)
{
    if (s_event_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (s_pending) {
        ESP_LOGI(TAG, "Replacing pending AI request with latest recording");
    }

    s_pending = true;
    snprintf(s_message, sizeof(s_message), "AI recording pending");
    ESP_LOGI(TAG, "AI request reserved for latest recording");
    ai_voice_post_event(APP_EVENT_AI_REQUEST_PENDING);
    return ESP_OK;
}

bool ai_voice_is_pending(void)
{
    return s_pending;
}
