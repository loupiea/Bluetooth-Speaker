#include "voice_wakeup.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "app_events.h"
#include "audio_input.h"
#include "esp_afe_sr_iface.h"
#include "esp_afe_sr_models.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "model_path.h"
#include "sdkconfig.h"

#ifdef CONFIG_SMART_SPEAKER_VOICE_WAKEUP_ENABLE
#define VOICE_WAKEUP_ENABLED true
#else
#define VOICE_WAKEUP_ENABLED false
#endif

#define VOICE_WAKEUP_STOP_WAIT_MS 1500
#define VOICE_WAKEUP_STOP_POLL_MS 20

static const char *TAG = "voice_wakeup";

static QueueHandle_t s_event_queue;
static TaskHandle_t s_wakeup_task;
static volatile bool s_running;
static volatile bool s_stop_requested;
static const esp_afe_sr_iface_t *s_afe_handle;
static esp_afe_sr_data_t *s_afe_data;

static bool voice_wakeup_has_wakenet_model(const srmodel_list_t *models)
{
    if (models == NULL || models->model_name == NULL || models->num <= 0) {
        return false;
    }

    bool found = false;
    for (int i = 0; i < models->num; ++i) {
        const char *name = models->model_name[i];
        if (name == NULL) {
            continue;
        }
        ESP_LOGI(TAG, "ESP-SR model loaded: %s", name);
        if (strncmp(name, "wn", 2) == 0) {
            found = true;
        }
    }
    return found;
}

static void voice_wakeup_post_event(app_event_type_t type, const char *message)
{
    if (s_event_queue == NULL) {
        return;
    }

    app_event_t event = {
        .type = type,
        .gesture = APP_GESTURE_NONE,
        .button = APP_BUTTON_NONE,
        .message = message,
    };
    xQueueSend(s_event_queue, &event, 0);
}

static esp_err_t voice_wakeup_create_afe(void)
{
    srmodel_list_t *models =
        esp_srmodel_init(CONFIG_SMART_SPEAKER_VOICE_WAKEUP_MODEL_PARTITION);
    if (models == NULL) {
        ESP_LOGE(TAG, "WakeNet model partition '%s' not found",
                 CONFIG_SMART_SPEAKER_VOICE_WAKEUP_MODEL_PARTITION);
        return ESP_ERR_NOT_FOUND;
    }
    if (!voice_wakeup_has_wakenet_model(models)) {
        ESP_LOGE(TAG, "No WakeNet model loaded; enable a CONFIG_SR_WN_* option and flash srmodels");
        esp_srmodel_deinit(models);
        return ESP_ERR_NOT_FOUND;
    }

    afe_config_t *afe_config = afe_config_init("M", models, AFE_TYPE_SR, AFE_MODE_LOW_COST);
    if (afe_config == NULL) {
        ESP_LOGE(TAG, "AFE config allocation failed");
        return ESP_ERR_NO_MEM;
    }

    afe_config->wakenet_init = true;
    afe_config->aec_init = false;
    afe_config->se_init = true;
    afe_config->vad_init = true;
    afe_config->memory_alloc_mode = AFE_MEMORY_ALLOC_MORE_PSRAM;

    s_afe_handle = esp_afe_handle_from_config(afe_config);
    if (s_afe_handle == NULL) {
        afe_config_free(afe_config);
        ESP_LOGE(TAG, "AFE handle lookup failed");
        return ESP_ERR_INVALID_STATE;
    }

    s_afe_data = s_afe_handle->create_from_config(afe_config);
    afe_config_free(afe_config);
    if (s_afe_data == NULL) {
        ESP_LOGE(TAG, "AFE create failed");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "WakeNet AFE ready: feed_chunksize=%d fetch_chunksize=%d",
             s_afe_handle->get_feed_chunksize(s_afe_data),
             s_afe_handle->get_fetch_chunksize(s_afe_data));
    return ESP_OK;
}

static void voice_wakeup_destroy_afe(void)
{
    if (s_afe_handle != NULL && s_afe_data != NULL) {
        s_afe_handle->destroy(s_afe_data);
    }
    s_afe_handle = NULL;
    s_afe_data = NULL;
}

static void voice_wakeup_task(void *arg)
{
    (void)arg;

    esp_err_t ret = voice_wakeup_create_afe();
    if (ret != ESP_OK) {
        voice_wakeup_post_event(APP_EVENT_VOICE_WAKEUP_FAILED, "WakeNet failed");
        s_wakeup_task = NULL;
        vTaskDelete(NULL);
        return;
    }

    int feed_chunksize = s_afe_handle->get_feed_chunksize(s_afe_data);
    int feed_channel = s_afe_handle->get_feed_channel_num(s_afe_data);
    int32_t *raw_samples = calloc((size_t)feed_chunksize, sizeof(raw_samples[0]));
    int16_t *afe_samples = calloc((size_t)feed_chunksize * (size_t)feed_channel,
                                  sizeof(afe_samples[0]));
    if (raw_samples == NULL || afe_samples == NULL) {
        ESP_LOGE(TAG, "WakeNet buffers allocation failed");
        free(raw_samples);
        free(afe_samples);
        voice_wakeup_destroy_afe();
        voice_wakeup_post_event(APP_EVENT_VOICE_WAKEUP_FAILED, "WakeNet memory");
        s_wakeup_task = NULL;
        vTaskDelete(NULL);
        return;
    }

    s_running = true;
    s_stop_requested = false;
    voice_wakeup_post_event(APP_EVENT_VOICE_WAKEUP_READY, "WakeNet ready");
    ESP_LOGI(TAG, "WakeNet listening for wake word");

    TickType_t next_detection_tick = 0;
    while (!s_stop_requested) {
        size_t samples_read = 0;
        ret = audio_input_read_samples(raw_samples,
                                       (size_t)feed_chunksize,
                                       &samples_read,
                                       pdMS_TO_TICKS(120));
        if (ret != ESP_OK || samples_read != (size_t)feed_chunksize) {
            ESP_LOGW(TAG, "WakeNet MIC read failed: %s read=%u/%d",
                     esp_err_to_name(ret),
                     (unsigned)samples_read,
                     feed_chunksize);
            continue;
        }

        for (int i = 0; i < feed_chunksize; ++i) {
            int16_t sample = (int16_t)(raw_samples[i] >> 16);
            for (int ch = 0; ch < feed_channel; ++ch) {
                afe_samples[i * feed_channel + ch] = sample;
            }
        }

        int feed_ret = s_afe_handle->feed(s_afe_data, afe_samples);
        if (feed_ret < 0) {
            ESP_LOGW(TAG, "WakeNet AFE feed failed: %d", feed_ret);
            continue;
        }

        afe_fetch_result_t *result = s_afe_handle->fetch(s_afe_data);
        if (result == NULL || result->ret_value != ESP_OK) {
            continue;
        }

        TickType_t now = xTaskGetTickCount();
        if (result->wakeup_state == WAKENET_DETECTED && now >= next_detection_tick) {
            ESP_LOGI(TAG, "WakeNet detected wake word id=%d", result->wake_word_index);
            voice_wakeup_post_event(APP_EVENT_VOICE_WAKEUP, "Wake word");
            next_detection_tick =
                now + pdMS_TO_TICKS(CONFIG_SMART_SPEAKER_VOICE_WAKEUP_COOLDOWN_MS);
        }
    }

    s_running = false;
    free(raw_samples);
    free(afe_samples);
    voice_wakeup_destroy_afe();
    s_wakeup_task = NULL;
    vTaskDelete(NULL);
}

esp_err_t voice_wakeup_init(QueueHandle_t event_queue)
{
    if (!VOICE_WAKEUP_ENABLED) {
        ESP_LOGI(TAG, "WakeNet voice wakeup disabled");
        return ESP_OK;
    }
    if (event_queue == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    s_event_queue = event_queue;
    ESP_LOGI(TAG, "WakeNet voice wakeup bridge ready");
    return ESP_OK;
}

esp_err_t voice_wakeup_start(void)
{
    if (!VOICE_WAKEUP_ENABLED) {
        return ESP_OK;
    }
    if (s_event_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_wakeup_task != NULL) {
        return ESP_OK;
    }

    BaseType_t created = xTaskCreate(voice_wakeup_task,
                                     "voice_wakeup",
                                     CONFIG_SMART_SPEAKER_VOICE_WAKEUP_TASK_STACK,
                                     NULL,
                                     CONFIG_SMART_SPEAKER_VOICE_WAKEUP_TASK_PRIORITY,
                                     &s_wakeup_task);
    return created == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t voice_wakeup_stop(void)
{
    if (s_wakeup_task == NULL) {
        return ESP_OK;
    }
    s_stop_requested = true;
    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(VOICE_WAKEUP_STOP_WAIT_MS);
    while (s_wakeup_task != NULL && xTaskGetTickCount() < deadline) {
        vTaskDelay(pdMS_TO_TICKS(VOICE_WAKEUP_STOP_POLL_MS));
    }
    if (s_wakeup_task != NULL) {
        ESP_LOGW(TAG, "WakeNet stop still pending");
    } else {
        ESP_LOGI(TAG, "WakeNet stopped cleanly");
    }
    return ESP_OK;
}

bool voice_wakeup_is_running(void)
{
    return s_running;
}
