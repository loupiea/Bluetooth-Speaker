#include "xiaozhi_audio_stream.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "audio_input.h"
#include "audio_opus.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "xiaozhi_client.h"

#define XIAOZHI_AUDIO_STREAM_TASK_STACK 24576
#define XIAOZHI_AUDIO_STREAM_TASK_PRIORITY 3
#define XIAOZHI_AUDIO_SEND_TASK_STACK 8192
#define XIAOZHI_AUDIO_SEND_TASK_PRIORITY 4
#define XIAOZHI_AUDIO_STREAM_READ_CHUNK_SAMPLES 256
#define XIAOZHI_AUDIO_STREAM_OPUS_MAX_BYTES 512
#define XIAOZHI_AUDIO_SEND_QUEUE_LENGTH 6
#define XIAOZHI_AUDIO_SEND_QUEUE_WAIT_MS 20
#define XIAOZHI_AUDIO_SEND_QUEUE_FULL_LIMIT 3
#define XIAOZHI_AUDIO_STREAM_STOP_WAIT_MS 600
#define XIAOZHI_AUDIO_STREAM_NOTIFY_START BIT0
#define XIAOZHI_AUDIO_STREAM_NOTIFY_STOP BIT1

static const char *TAG = "xz_audio_stream";

typedef struct {
    size_t len;
    uint8_t data[XIAOZHI_AUDIO_STREAM_OPUS_MAX_BYTES];
} xiaozhi_audio_frame_t;

static TaskHandle_t s_stream_task;
static TaskHandle_t s_send_task;
static StackType_t s_stream_task_stack[XIAOZHI_AUDIO_STREAM_TASK_STACK];
static StackType_t s_send_task_stack[XIAOZHI_AUDIO_SEND_TASK_STACK];
static StaticTask_t s_stream_task_buffer;
static StaticTask_t s_send_task_buffer;
static QueueHandle_t s_send_queue;
static volatile bool s_running;
static volatile bool s_stop_requested;
static volatile bool s_sending;

static uint32_t xiaozhi_audio_stream_frame_level(const int16_t *samples,
                                                 size_t sample_count,
                                                 int32_t *peak)
{
    uint64_t sum_abs = 0;
    int32_t max_abs = 0;

    for (size_t i = 0; i < sample_count; ++i) {
        int32_t value = samples[i];
        int32_t abs_value = value < 0 ? -value : value;
        sum_abs += abs_value;
        if (abs_value > max_abs) {
            max_abs = abs_value;
        }
    }

    if (peak != NULL) {
        *peak = max_abs;
    }
    return sample_count > 0 ? (uint32_t)(sum_abs / sample_count) : 0;
}

static void xiaozhi_audio_stream_free_buffers(int32_t *raw_samples,
                                              int16_t *pcm_samples,
                                              uint8_t *opus_frame)
{
    free(raw_samples);
    free(pcm_samples);
    free(opus_frame);
}

static void xiaozhi_audio_stream_task(void *arg)
{
    (void)arg;

    int32_t *raw_samples = calloc(AUDIO_OPUS_FRAME_SAMPLES, sizeof(raw_samples[0]));
    int16_t *pcm_samples = calloc(AUDIO_OPUS_FRAME_SAMPLES, sizeof(pcm_samples[0]));
    uint8_t *opus_frame = calloc(XIAOZHI_AUDIO_STREAM_OPUS_MAX_BYTES,
                                 sizeof(opus_frame[0]));
    if (raw_samples == NULL || pcm_samples == NULL || opus_frame == NULL) {
        ESP_LOGE(TAG, "Audio stream buffer allocation failed");
        xiaozhi_audio_stream_free_buffers(raw_samples, pcm_samples, opus_frame);
        s_stream_task = NULL;
        vTaskDelete(NULL);
        return;
    }

    while (true) {
        uint32_t notify_value = 0;
        if (xTaskNotifyWait(0, UINT32_MAX, &notify_value, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if ((notify_value & XIAOZHI_AUDIO_STREAM_NOTIFY_START) == 0) {
            continue;
        }

        s_stop_requested = false;
        s_running = true;
        uint32_t frames_sent = 0;
        size_t bytes_sent = 0;
        uint64_t level_total = 0;
        int32_t peak_max = 0;
        uint32_t queue_full_count = 0;
        bool stack_logged = false;
        TickType_t started_at = xTaskGetTickCount();
        ESP_LOGI(TAG, "XiaoZhi audio stream started");

        while (!s_stop_requested && xiaozhi_client_is_listening()) {
            if (xTaskNotifyWait(0,
                                UINT32_MAX,
                                &notify_value,
                                0) == pdTRUE &&
                (notify_value & XIAOZHI_AUDIO_STREAM_NOTIFY_STOP) != 0) {
                s_stop_requested = true;
                break;
            }

            size_t total_read = 0;
            while (!s_stop_requested && total_read < AUDIO_OPUS_FRAME_SAMPLES) {
                size_t samples_to_read = AUDIO_OPUS_FRAME_SAMPLES - total_read;
                if (samples_to_read > XIAOZHI_AUDIO_STREAM_READ_CHUNK_SAMPLES) {
                    samples_to_read = XIAOZHI_AUDIO_STREAM_READ_CHUNK_SAMPLES;
                }

                size_t samples_read = 0;
                esp_err_t ret = audio_input_read_samples(&raw_samples[total_read],
                                                         samples_to_read,
                                                         &samples_read,
                                                         pdMS_TO_TICKS(120));
                if (ret != ESP_OK) {
                    ESP_LOGW(TAG, "MIC read failed: %s", esp_err_to_name(ret));
                    break;
                }
                total_read += samples_read;
            }

            if (s_stop_requested || total_read != AUDIO_OPUS_FRAME_SAMPLES) {
                continue;
            }

            for (size_t i = 0; i < AUDIO_OPUS_FRAME_SAMPLES; ++i) {
                pcm_samples[i] = (int16_t)(raw_samples[i] >> 16);
            }
            int32_t frame_peak = 0;
            level_total += xiaozhi_audio_stream_frame_level(pcm_samples,
                                                            AUDIO_OPUS_FRAME_SAMPLES,
                                                            &frame_peak);
            if (frame_peak > peak_max) {
                peak_max = frame_peak;
            }

            size_t encoded_bytes = 0;
            esp_err_t ret = audio_opus_encode_frame(pcm_samples,
                                                    AUDIO_OPUS_FRAME_SAMPLES,
                                                    opus_frame,
                                                    XIAOZHI_AUDIO_STREAM_OPUS_MAX_BYTES,
                                                    &encoded_bytes);
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "Opus encode failed: %s", esp_err_to_name(ret));
                continue;
            }
            if (encoded_bytes == 0) {
                continue;
            }
            if (!stack_logged) {
                ESP_LOGI(TAG, "XiaoZhi audio stream stack free words=%u",
                         (unsigned)uxTaskGetStackHighWaterMark(NULL));
                stack_logged = true;
            }

            xiaozhi_audio_frame_t frame = {
                .len = encoded_bytes,
            };
            memcpy(frame.data, opus_frame, encoded_bytes);
            if (xQueueSend(s_send_queue,
                           &frame,
                           pdMS_TO_TICKS(XIAOZHI_AUDIO_SEND_QUEUE_WAIT_MS)) != pdTRUE) {
                queue_full_count++;
                ESP_LOGW(TAG, "XiaoZhi audio send queue full, dropping frame=%lu free=%u",
                         (unsigned long)frames_sent,
                         (unsigned int)uxQueueSpacesAvailable(s_send_queue));
                if (queue_full_count >= XIAOZHI_AUDIO_SEND_QUEUE_FULL_LIMIT) {
                    ESP_LOGW(TAG, "XiaoZhi audio send queue blocked, stopping listen");
                    s_stop_requested = true;
                }
                continue;
            }
            queue_full_count = 0;
            frames_sent++;
            bytes_sent += encoded_bytes;
            vTaskDelay(1);
        }

        s_running = false;
        audio_opus_deinit();
        ESP_LOGI(TAG, "XiaoZhi audio stream encoder released: free=%lu min=%lu largest=%lu",
                 (unsigned long)heap_caps_get_free_size(MALLOC_CAP_8BIT),
                 (unsigned long)heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT),
                 (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
        ESP_LOGI(TAG, "XiaoZhi audio stream stopped: frames=%lu bytes=%u duration=%lu ms level=%lu peak=%ld",
                 (unsigned long)frames_sent,
                 (unsigned)bytes_sent,
                 (unsigned long)((xTaskGetTickCount() - started_at) * portTICK_PERIOD_MS),
                 frames_sent > 0 ? (unsigned long)(level_total / frames_sent) : 0,
                 (long)peak_max);
    }
}

static void xiaozhi_audio_send_task(void *arg)
{
    (void)arg;

    xiaozhi_audio_frame_t frame;
    uint32_t frames_sent = 0;
    size_t bytes_sent = 0;
    bool stack_logged = false;

    while (true) {
        if (xQueueReceive(s_send_queue, &frame, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        if (!s_running || s_stop_requested || !xiaozhi_client_is_listening()) {
            continue;
        }

        if (!stack_logged) {
            ESP_LOGI(TAG, "XiaoZhi audio send stack free words=%u",
                     (unsigned)uxTaskGetStackHighWaterMark(NULL));
            stack_logged = true;
        }

        s_sending = true;
        esp_err_t ret = xiaozhi_client_send_audio(frame.data, frame.len);
        s_sending = false;
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "XiaoZhi audio sender failed after %lu frames, %u bytes: %s",
                     (unsigned long)frames_sent,
                     (unsigned)bytes_sent,
                     esp_err_to_name(ret));
            s_stop_requested = true;
            if (s_stream_task != NULL) {
                xTaskNotify(s_stream_task,
                            XIAOZHI_AUDIO_STREAM_NOTIFY_STOP,
                            eSetBits);
            }
            xQueueReset(s_send_queue);
            frames_sent = 0;
            bytes_sent = 0;
            stack_logged = false;
            continue;
        }

        frames_sent++;
        bytes_sent += frame.len;

        if (!s_running) {
            ESP_LOGI(TAG, "XiaoZhi audio sender idle: frames=%lu bytes=%u",
                     (unsigned long)frames_sent,
                     (unsigned)bytes_sent);
            frames_sent = 0;
            bytes_sent = 0;
            stack_logged = false;
        }
    }
}

esp_err_t xiaozhi_audio_stream_init(void)
{
    if (s_stream_task != NULL) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "XiaoZhi audio stream init heap: free=%lu min=%lu largest=%lu",
             (unsigned long)heap_caps_get_free_size(MALLOC_CAP_8BIT),
             (unsigned long)heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT),
             (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));

    esp_err_t ret = audio_opus_init();
    if (ret != ESP_OK) {
        return ret;
    }

    ESP_LOGI(TAG, "XiaoZhi audio stream after Opus init heap: free=%lu min=%lu largest=%lu",
             (unsigned long)heap_caps_get_free_size(MALLOC_CAP_8BIT),
             (unsigned long)heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT),
             (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));

    s_send_queue = xQueueCreate(XIAOZHI_AUDIO_SEND_QUEUE_LENGTH,
                                sizeof(xiaozhi_audio_frame_t));
    if (s_send_queue == NULL) {
        ESP_LOGW(TAG, "XiaoZhi audio send queue allocation failed: free=%lu",
                 (unsigned long)heap_caps_get_free_size(MALLOC_CAP_8BIT));
        audio_opus_deinit();
        return ESP_ERR_NO_MEM;
    }

    s_send_task = xTaskCreateStatic(xiaozhi_audio_send_task,
                                    "xz_audio_send",
                                    XIAOZHI_AUDIO_SEND_TASK_STACK,
                                    NULL,
                                    XIAOZHI_AUDIO_SEND_TASK_PRIORITY,
                                    s_send_task_stack,
                                    &s_send_task_buffer);
    if (s_send_task == NULL) {
        ESP_LOGW(TAG, "XiaoZhi audio send task allocation failed: stack=%u free=%lu",
                 (unsigned)XIAOZHI_AUDIO_SEND_TASK_STACK,
                 (unsigned long)heap_caps_get_free_size(MALLOC_CAP_8BIT));
        vQueueDelete(s_send_queue);
        s_send_queue = NULL;
        s_send_task = NULL;
        audio_opus_deinit();
        return ESP_ERR_NO_MEM;
    }

    s_stream_task = xTaskCreateStatic(xiaozhi_audio_stream_task,
                                      "xz_audio_stream",
                                      XIAOZHI_AUDIO_STREAM_TASK_STACK,
                                      NULL,
                                      XIAOZHI_AUDIO_STREAM_TASK_PRIORITY,
                                      s_stream_task_stack,
                                      &s_stream_task_buffer);
    if (s_stream_task == NULL) {
        ESP_LOGW(TAG, "XiaoZhi audio stream task allocation failed: stack=%u free=%lu largest=%lu",
                 (unsigned)XIAOZHI_AUDIO_STREAM_TASK_STACK,
                 (unsigned long)heap_caps_get_free_size(MALLOC_CAP_8BIT),
                 (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
        vTaskDelete(s_send_task);
        vQueueDelete(s_send_queue);
        s_send_task = NULL;
        s_send_queue = NULL;
        s_stream_task = NULL;
        audio_opus_deinit();
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "XiaoZhi audio stream init done: free=%lu min=%lu largest=%lu",
             (unsigned long)heap_caps_get_free_size(MALLOC_CAP_8BIT),
             (unsigned long)heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT),
             (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    return ESP_OK;
}

esp_err_t xiaozhi_audio_stream_start(void)
{
    if (s_running) {
        return ESP_OK;
    }
    if (s_stream_task == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!xiaozhi_client_is_listening()) {
        ESP_LOGW(TAG, "Cannot start audio stream before XiaoZhi listen starts");
        return ESP_ERR_INVALID_STATE;
    }

    s_stop_requested = false;
    xQueueReset(s_send_queue);
    return xTaskNotify(s_stream_task,
                       XIAOZHI_AUDIO_STREAM_NOTIFY_START,
                       eSetBits) == pdPASS ? ESP_OK : ESP_FAIL;
}

esp_err_t xiaozhi_audio_stream_stop(void)
{
    if (s_stream_task == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    s_stop_requested = true;
    xQueueReset(s_send_queue);
    if (xTaskNotify(s_stream_task,
                    XIAOZHI_AUDIO_STREAM_NOTIFY_STOP,
                    eSetBits) != pdPASS) {
        return ESP_FAIL;
    }

    TickType_t deadline = xTaskGetTickCount() +
                          pdMS_TO_TICKS(XIAOZHI_AUDIO_STREAM_STOP_WAIT_MS);
    while ((s_running || s_sending) &&
           xTaskGetTickCount() < deadline) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (s_running || s_sending) {
        ESP_LOGW(TAG, "XiaoZhi audio stream stop wait timed out: running=%d sending=%d",
                 s_running,
                 s_sending);
    }
    return ESP_OK;
}

bool xiaozhi_audio_stream_is_running(void)
{
    return s_running;
}

bool xiaozhi_audio_stream_is_stopping(void)
{
    return s_stop_requested;
}
