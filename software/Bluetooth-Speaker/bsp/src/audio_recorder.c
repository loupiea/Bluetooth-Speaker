#include "audio_recorder.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "app_events.h"
#include "audio_input.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "storage_sd.h"

#define RECORDER_READ_SAMPLES 256
#define RECORDER_TASK_STACK 4096
#define RECORDER_TASK_PRIORITY 5
#define WAV_BITS_PER_SAMPLE 16
#define WAV_CHANNELS 1
#define WAV_BYTES_PER_SAMPLE (WAV_BITS_PER_SAMPLE / 8)
#define RECORDINGS_DIR CONFIG_SMART_SPEAKER_SDMMC_MOUNT_POINT "/recordings"

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

static const char *TAG = "audio_recorder";
static QueueHandle_t s_event_queue;
static TaskHandle_t s_recorder_task;
static volatile bool s_recording;
static volatile bool s_stop_requested;
static uint32_t s_recording_index = 1;

static void recorder_post_event(app_event_type_t type)
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

static wav_header_t make_wav_header(uint32_t data_size)
{
    const uint32_t sample_rate = CONFIG_SMART_SPEAKER_RECORDER_SAMPLE_RATE_HZ;
    const uint16_t block_align = WAV_CHANNELS * WAV_BYTES_PER_SAMPLE;

    wav_header_t header = {
        .riff_size = 36U + data_size,
        .fmt_size = 16,
        .audio_format = 1,
        .num_channels = WAV_CHANNELS,
        .sample_rate = sample_rate,
        .byte_rate = sample_rate * block_align,
        .block_align = block_align,
        .bits_per_sample = WAV_BITS_PER_SAMPLE,
        .data_size = data_size,
    };
    memcpy(header.riff, "RIFF", sizeof(header.riff));
    memcpy(header.wave, "WAVE", sizeof(header.wave));
    memcpy(header.fmt, "fmt ", sizeof(header.fmt));
    memcpy(header.data, "data", sizeof(header.data));
    return header;
}

static esp_err_t ensure_recordings_dir(void)
{
    if (mkdir(RECORDINGS_DIR, 0775) == 0 || errno == EEXIST) {
        return ESP_OK;
    }

    ESP_LOGE(TAG, "mkdir %s failed: errno=%d", RECORDINGS_DIR, errno);
    return ESP_FAIL;
}

static esp_err_t open_recording_file(FILE **file, char *path, size_t path_size)
{
    if (!storage_sd_is_mounted()) {
        ESP_LOGW(TAG, "SD card is not mounted");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = ensure_recordings_dir();
    if (ret != ESP_OK) {
        return ret;
    }

    for (uint32_t attempts = 0; attempts < 1000; ++attempts) {
        snprintf(path, path_size, RECORDINGS_DIR "/rec_%03u.wav",
                 (unsigned int)s_recording_index);
        s_recording_index = (s_recording_index % 999U) + 1U;

        FILE *probe = fopen(path, "rb");
        if (probe != NULL) {
            fclose(probe);
            continue;
        }

        *file = fopen(path, "wb+");
        if (*file == NULL) {
            ESP_LOGE(TAG, "open %s failed: errno=%d", path, errno);
            return ESP_FAIL;
        }
        return ESP_OK;
    }

    return ESP_ERR_NOT_FOUND;
}

static esp_err_t write_wav_header(FILE *file, uint32_t data_size)
{
    wav_header_t header = make_wav_header(data_size);
    if (fseek(file, 0, SEEK_SET) != 0) {
        return ESP_FAIL;
    }

    if (fwrite(&header, 1, sizeof(header), file) != sizeof(header)) {
        return ESP_FAIL;
    }

    return ESP_OK;
}

static void audio_recorder_task(void *arg)
{
    (void)arg;
    int32_t input_samples[RECORDER_READ_SAMPLES] = { 0 };
    int16_t pcm_samples[RECORDER_READ_SAMPLES] = { 0 };

    while (true) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        FILE *file = NULL;
        char path[96] = { 0 };
        uint32_t data_bytes = 0;
        esp_err_t ret = open_recording_file(&file, path, sizeof(path));
        if (ret != ESP_OK) {
            s_recording = false;
            s_stop_requested = false;
            recorder_post_event(APP_EVENT_ERROR);
            continue;
        }

        ret = write_wav_header(file, 0);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "initial WAV header failed");
            fclose(file);
            s_recording = false;
            s_stop_requested = false;
            recorder_post_event(APP_EVENT_ERROR);
            continue;
        }

        ESP_LOGI(TAG, "Recording started: %s", path);
        recorder_post_event(APP_EVENT_RECORDING_STARTED);

        const uint32_t max_data_bytes =
            CONFIG_SMART_SPEAKER_RECORDER_SAMPLE_RATE_HZ *
            WAV_BYTES_PER_SAMPLE *
            CONFIG_SMART_SPEAKER_RECORDER_MAX_SECONDS;

        while (!s_stop_requested && data_bytes < max_data_bytes) {
            size_t samples_read = 0;
            ret = audio_input_read_samples(input_samples, RECORDER_READ_SAMPLES,
                                           &samples_read, pdMS_TO_TICKS(200));
            if (ret != ESP_OK || samples_read == 0) {
                ESP_LOGW(TAG, "record read failed: %s", esp_err_to_name(ret));
                continue;
            }

            if (data_bytes + (samples_read * sizeof(pcm_samples[0])) > max_data_bytes) {
                samples_read = (max_data_bytes - data_bytes) / sizeof(pcm_samples[0]);
            }

            for (size_t i = 0; i < samples_read; ++i) {
                pcm_samples[i] = (int16_t)(input_samples[i] >> 16);
            }

            size_t bytes_to_write = samples_read * sizeof(pcm_samples[0]);
            if (fwrite(pcm_samples, 1, bytes_to_write, file) != bytes_to_write) {
                ESP_LOGE(TAG, "write PCM failed: errno=%d", errno);
                break;
            }
            data_bytes += bytes_to_write;
        }

        if (data_bytes >= max_data_bytes) {
            ESP_LOGI(TAG, "Recording reached max duration: %d s",
                     CONFIG_SMART_SPEAKER_RECORDER_MAX_SECONDS);
        }

        if (write_wav_header(file, data_bytes) != ESP_OK) {
            ESP_LOGE(TAG, "final WAV header failed");
        }
        fflush(file);
        fclose(file);

        ESP_LOGI(TAG, "Recording stopped: %s, bytes=%lu",
                 path, (unsigned long)data_bytes);
        s_recording = false;
        s_stop_requested = false;
        recorder_post_event(APP_EVENT_RECORDING_STOPPED);
    }
}

esp_err_t audio_recorder_init(QueueHandle_t event_queue)
{
    if (s_recorder_task != NULL) {
        return ESP_OK;
    }

    s_event_queue = event_queue;
    BaseType_t ok = xTaskCreate(audio_recorder_task, "audio_recorder",
                                RECORDER_TASK_STACK, NULL,
                                RECORDER_TASK_PRIORITY, &s_recorder_task);
    if (ok != pdPASS) {
        s_recorder_task = NULL;
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "WAV recorder ready: dir=%s sample_rate=%d max_seconds=%d",
             RECORDINGS_DIR,
             CONFIG_SMART_SPEAKER_RECORDER_SAMPLE_RATE_HZ,
             CONFIG_SMART_SPEAKER_RECORDER_MAX_SECONDS);
    return ESP_OK;
}

esp_err_t audio_recorder_toggle(void)
{
    if (s_recorder_task == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (s_recording) {
        s_stop_requested = true;
        ESP_LOGI(TAG, "Recording stop requested");
        return ESP_OK;
    }

    if (!storage_sd_is_mounted()) {
        ESP_LOGW(TAG, "Cannot start recording: SD card is not mounted");
        return ESP_ERR_INVALID_STATE;
    }

    s_stop_requested = false;
    s_recording = true;
    xTaskNotifyGive(s_recorder_task);
    return ESP_OK;
}

bool audio_recorder_is_recording(void)
{
    return s_recording;
}
