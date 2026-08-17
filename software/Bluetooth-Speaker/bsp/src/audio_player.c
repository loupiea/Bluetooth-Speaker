#include "audio_player.h"

#include <errno.h>
#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "audio_output.h"
#include "app_events.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "storage_sd.h"

#define AUDIO_PLAYER_READ_SAMPLES 256
#define AUDIO_PLAYER_TASK_STACK 4096
#define AUDIO_PLAYER_TASK_PRIORITY 5
#define RECORDING_PATH_SIZE 96
#define RECORDINGS_DIR CONFIG_SMART_SPEAKER_SDMMC_MOUNT_POINT "/recordings"
#define RECORDINGS_PATTERN CONFIG_SMART_SPEAKER_SDMMC_MOUNT_POINT "/recordings/rec_%03u.wav"

typedef struct __attribute__((packed)) {
    char riff[4];
    uint32_t riff_size;
    char wave[4];
    char fmt[4];
    uint32_t fmt_size;
    uint16_t audio_format;
    uint16_t num_channels;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits_per_sample;
    char data[4];
    uint32_t data_size;
} wav_header_t;

static const char *TAG = "audio_player";
static QueueHandle_t s_event_queue;
static TaskHandle_t s_player_task;
static volatile bool s_playing;
static volatile bool s_stop_requested;

static void audio_player_post_event(app_event_type_t type)
{
    if (s_event_queue == NULL) {
        return;
    }

    app_event_t event = {
        .type = type,
        .gesture = APP_GESTURE_NONE,
        .button = APP_BUTTON_NONE,
        .message = NULL,
    };
    xQueueSend(s_event_queue, &event, 0);
}

static esp_err_t audio_player_validate_header(const wav_header_t *header)
{
    if (memcmp(header->riff, "RIFF", sizeof(header->riff)) != 0 ||
        memcmp(header->wave, "WAVE", sizeof(header->wave)) != 0 ||
        memcmp(header->fmt, "fmt ", sizeof(header->fmt)) != 0 ||
        memcmp(header->data, "data", sizeof(header->data)) != 0) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    if (header->audio_format != 1 ||
        header->num_channels != 1 ||
        header->sample_rate != CONFIG_SMART_SPEAKER_AUDIO_OUTPUT_SAMPLE_RATE_HZ ||
        header->bits_per_sample != 16 ||
        header->block_align != sizeof(int16_t) ||
        (header->data_size % sizeof(int16_t)) != 0) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    return ESP_OK;
}

esp_err_t audio_player_play_file(const char *path)
{
    if (path == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!storage_sd_is_mounted()) {
        ESP_LOGW(TAG, "Cannot play WAV: SD card is not mounted");
        return ESP_ERR_INVALID_STATE;
    }

    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        ESP_LOGE(TAG, "open %s failed: errno=%d", path, errno);
        return ESP_FAIL;
    }

    wav_header_t header = { 0 };
    esp_err_t ret = ESP_OK;
    if (fread(&header, 1, sizeof(header), file) != sizeof(header)) {
        ret = ESP_FAIL;
    } else {
        ret = audio_player_validate_header(&header);
    }

    int16_t samples[AUDIO_PLAYER_READ_SAMPLES] = { 0 };
    uint32_t bytes_remaining = header.data_size;
    if (ret == ESP_OK) {
        ret = audio_output_start();
    }

    while (ret == ESP_OK && !s_stop_requested && bytes_remaining > 0) {
        size_t bytes_to_read = sizeof(samples);
        if (bytes_to_read > bytes_remaining) {
            bytes_to_read = bytes_remaining;
        }

        size_t bytes_read = fread(samples, 1, bytes_to_read, file);
        if (bytes_read != bytes_to_read) {
            ret = ESP_FAIL;
            break;
        }

        ret = audio_output_write_pcm(samples, bytes_read / sizeof(samples[0]));
        bytes_remaining -= bytes_read;
    }

    esp_err_t stop_ret = audio_output_stop();
    if (ret == ESP_OK) {
        ret = stop_ret;
    }
    fclose(file);

    if (ret == ESP_OK && s_stop_requested) {
        ESP_LOGI(TAG, "WAV playback stopped: %s", path);
    } else if (ret == ESP_OK) {
        ESP_LOGI(TAG, "WAV played: %s, bytes=%lu",
                 path, (unsigned long)header.data_size);
    } else {
        ESP_LOGW(TAG, "WAV play failed: %s, err=%s",
                 path, esp_err_to_name(ret));
    }
    return ret;
}

static void audio_player_task(void *arg)
{
    (void)arg;

    while (true) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        audio_player_post_event(APP_EVENT_PLAYBACK_STARTED);
        esp_err_t ret = audio_player_play_latest_recording();
        if (ret == ESP_OK) {
            audio_player_post_event(APP_EVENT_PLAYBACK_STOPPED);
        } else {
            audio_player_post_event(APP_EVENT_PLAYBACK_FAILED);
        }

        s_stop_requested = false;
        s_playing = false;
    }
}

esp_err_t audio_player_init(QueueHandle_t event_queue)
{
    if (s_player_task != NULL) {
        return ESP_OK;
    }

    s_event_queue = event_queue;
    BaseType_t ok = xTaskCreate(audio_player_task, "audio_player",
                                AUDIO_PLAYER_TASK_STACK, NULL,
                                AUDIO_PLAYER_TASK_PRIORITY, &s_player_task);
    if (ok != pdPASS) {
        s_player_task = NULL;
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "WAV player task ready");
    return ESP_OK;
}

esp_err_t audio_player_play_recording(uint16_t index)
{
    if (index == 0 || index > 999) {
        return ESP_ERR_INVALID_ARG;
    }

    char path[RECORDING_PATH_SIZE] = { 0 };
    snprintf(path, sizeof(path), RECORDINGS_PATTERN, (unsigned int)index);
    return audio_player_play_file(path);
}

esp_err_t audio_player_play_latest_recording(void)
{
    if (!storage_sd_is_mounted()) {
        ESP_LOGW(TAG, "Cannot find latest recording: SD card is not mounted");
        return ESP_ERR_INVALID_STATE;
    }

    DIR *dir = opendir(RECORDINGS_DIR);
    if (dir == NULL) {
        ESP_LOGE(TAG, "open dir %s failed: errno=%d", RECORDINGS_DIR, errno);
        return ESP_FAIL;
    }

    uint16_t latest_index = 0;
    struct dirent *entry = NULL;
    while ((entry = readdir(dir)) != NULL) {
        unsigned int index = 0;
        char tail = '\0';
        if (sscanf(entry->d_name, "rec_%3u.wav%c", &index, &tail) == 1 &&
            index >= 1U && index <= 999U &&
            index > latest_index) {
            latest_index = (uint16_t)index;
        }
    }
    closedir(dir);

    if (latest_index == 0) {
        ESP_LOGW(TAG, "No recording WAV files found in %s", RECORDINGS_DIR);
        return ESP_ERR_NOT_FOUND;
    }

    return audio_player_play_recording(latest_index);
}

esp_err_t audio_player_play_latest_recording_async(void)
{
    if (s_player_task == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_playing) {
        return ESP_ERR_INVALID_STATE;
    }

    s_stop_requested = false;
    s_playing = true;
    xTaskNotifyGive(s_player_task);
    return ESP_OK;
}

esp_err_t audio_player_stop(void)
{
    if (!s_playing) {
        return ESP_ERR_INVALID_STATE;
    }

    s_stop_requested = true;
    ESP_LOGI(TAG, "WAV playback stop requested");
    return ESP_OK;
}

bool audio_player_is_playing(void)
{
    return s_playing;
}
