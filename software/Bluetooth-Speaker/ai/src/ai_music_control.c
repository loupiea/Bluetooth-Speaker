#include "ai_music_control.h"

#include "ai_music_library.h"
#include "audio_music_player.h"
#include "audio_output.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include <string.h>

static const char *TAG = "ai_music_control";
static size_t s_next_track_index;

static void ai_music_control_set_next_after_track(const ai_music_track_t *track)
{
    size_t track_count = ai_music_library_count();
    if (track == NULL || track_count == 0) {
        return;
    }
    for (size_t i = 0; i < track_count; ++i) {
        if (ai_music_library_get(i) == track) {
            s_next_track_index = (i + 1) % track_count;
            return;
        }
    }
}

static void ai_music_control_set_next_after_url(const char *url)
{
    size_t track_count = ai_music_library_count();
    if (url == NULL || track_count == 0) {
        return;
    }
    for (size_t i = 0; i < track_count; ++i) {
        const ai_music_track_t *track = ai_music_library_get(i);
        if (track != NULL && strcmp(track->url, url) == 0) {
            s_next_track_index = (i + 1) % track_count;
            return;
        }
    }
}

static esp_err_t ai_music_control_wait_until_idle(TickType_t timeout_ticks)
{
    TickType_t started_at = xTaskGetTickCount();
    while (audio_music_player_is_playing()) {
        if (xTaskGetTickCount() - started_at >= timeout_ticks) {
            return ESP_ERR_TIMEOUT;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    return ESP_OK;
}

static esp_err_t ai_music_control_wait_output_idle(TickType_t timeout_ticks)
{
    TickType_t started_at = xTaskGetTickCount();
    while (audio_output_is_active()) {
        if (xTaskGetTickCount() - started_at >= timeout_ticks) {
            return ESP_ERR_TIMEOUT;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    return ESP_OK;
}

esp_err_t ai_music_control_play_default(void)
{
    if (audio_music_player_is_playing()) {
        ESP_LOGI(TAG, "HTTP music already playing");
        return ESP_OK;
    }

    esp_err_t ret = audio_music_player_play_url_async(CONFIG_SMART_SPEAKER_MUSIC_DEFAULT_URL);
    if (ret == ESP_OK) {
        ai_music_control_set_next_after_url(CONFIG_SMART_SPEAKER_MUSIC_DEFAULT_URL);
        ESP_LOGI(TAG, "HTTP music play requested by XiaoZhi");
    } else {
        ESP_LOGW(TAG, "HTTP music play request failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

esp_err_t ai_music_control_play_by_name(const char *query)
{
    if (audio_music_player_is_playing()) {
        ESP_LOGI(TAG, "HTTP music already playing, keeping current track");
        return ESP_OK;
    }

    const ai_music_track_t *track = ai_music_library_find(query);
    if (track == NULL) {
        ESP_LOGW(TAG, "HTTP music track not found: %s", query != NULL ? query : "null");
        return ESP_ERR_NOT_FOUND;
    }

    esp_err_t ret = audio_music_player_play_url_async(track->url);
    if (ret == ESP_OK) {
        ai_music_control_set_next_after_track(track);
        ESP_LOGI(TAG, "HTTP music play requested by XiaoZhi: %s - %s",
                 track->name,
                 track->artist);
    } else {
        ESP_LOGW(TAG, "HTTP music named play request failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

esp_err_t ai_music_control_play_next(void)
{
    size_t track_count = ai_music_library_count();
    if (track_count == 0) {
        ESP_LOGW(TAG, "HTTP music library is empty");
        return ESP_ERR_NOT_FOUND;
    }

    if (audio_music_player_is_playing()) {
        esp_err_t stop_ret = audio_music_player_stop();
        if (stop_ret != ESP_OK) {
            ESP_LOGW(TAG, "HTTP music stop before next failed: %s",
                     esp_err_to_name(stop_ret));
            return stop_ret;
        }
        esp_err_t wait_ret = ai_music_control_wait_until_idle(pdMS_TO_TICKS(1500));
        if (wait_ret != ESP_OK) {
            ESP_LOGW(TAG, "HTTP music did not stop before next: %s",
                     esp_err_to_name(wait_ret));
            return wait_ret;
        }
        vTaskDelay(pdMS_TO_TICKS(180));
        wait_ret = ai_music_control_wait_output_idle(pdMS_TO_TICKS(1000));
        if (wait_ret != ESP_OK) {
            ESP_LOGW(TAG, "Audio output busy before next track: %s",
                     esp_err_to_name(wait_ret));
            return wait_ret;
        }
    }

    const ai_music_track_t *track = ai_music_library_get(s_next_track_index % track_count);
    s_next_track_index = (s_next_track_index + 1) % track_count;
    if (track == NULL) {
        return ESP_ERR_NOT_FOUND;
    }

    esp_err_t ret = audio_music_player_play_url_async(track->url);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "HTTP music next track requested: %s - %s",
                 track->name,
                 track->artist);
    } else {
        ESP_LOGW(TAG, "HTTP music next track failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

esp_err_t ai_music_control_format_list(char *buffer, size_t buffer_size)
{
    return ai_music_library_format_list(buffer, buffer_size);
}

esp_err_t ai_music_control_stop(void)
{
    if (!audio_music_player_is_playing()) {
        ESP_LOGI(TAG, "HTTP music already stopped");
        return ESP_OK;
    }

    esp_err_t ret = audio_music_player_stop();
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "HTTP music stop requested by XiaoZhi");
    } else {
        ESP_LOGW(TAG, "HTTP music stop request failed: %s", esp_err_to_name(ret));
    }
    return ret;
}
