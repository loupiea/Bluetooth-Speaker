#include "xiaozhi_tts_player.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "audio_output.h"
#include "esp_audio_dec.h"
#include "esp_audio_types.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_opus_dec.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define XIAOZHI_TTS_INPUT_SAMPLE_RATE_HZ 24000
#define XIAOZHI_TTS_OUTPUT_SAMPLE_RATE_HZ 16000
#define XIAOZHI_TTS_FRAME_DURATION_MS 60
#define XIAOZHI_TTS_INPUT_SAMPLES \
    ((XIAOZHI_TTS_INPUT_SAMPLE_RATE_HZ / 1000) * XIAOZHI_TTS_FRAME_DURATION_MS)
#define XIAOZHI_TTS_OUTPUT_SAMPLES \
    ((XIAOZHI_TTS_OUTPUT_SAMPLE_RATE_HZ / 1000) * XIAOZHI_TTS_FRAME_DURATION_MS)
#define XIAOZHI_TTS_OPUS_MAX_BYTES 512
#define XIAOZHI_TTS_QUEUE_LENGTH 16
#define XIAOZHI_TTS_OPUS_QUEUE_WAIT_MS 120
#define XIAOZHI_TTS_CONTROL_QUEUE_WAIT_MS 500
#define XIAOZHI_TTS_TASK_STACK 16384
#define XIAOZHI_TTS_TASK_PRIORITY 5

typedef enum {
    XIAOZHI_TTS_MSG_START,
    XIAOZHI_TTS_MSG_OPUS,
    XIAOZHI_TTS_MSG_STOP,
} xiaozhi_tts_msg_type_t;

typedef struct {
    xiaozhi_tts_msg_type_t type;
    size_t len;
    uint8_t data[XIAOZHI_TTS_OPUS_MAX_BYTES];
} xiaozhi_tts_msg_t;

static const char *TAG = "xz_tts_player";

static QueueHandle_t s_queue;
static SemaphoreHandle_t s_stop_done;
static TaskHandle_t s_task;
static void *s_decoder;
static bool s_playing;
static bool s_discarding;

static int xiaozhi_tts_resample_24k_to_16k(const int16_t *in,
                                           size_t in_samples,
                                           int16_t *out,
                                           size_t out_capacity)
{
    if (in == NULL || out == NULL || out_capacity < XIAOZHI_TTS_OUTPUT_SAMPLES ||
        in_samples < XIAOZHI_TTS_INPUT_SAMPLES) {
        return 0;
    }

    for (size_t i = 0; i < XIAOZHI_TTS_OUTPUT_SAMPLES; ++i) {
        size_t src_x2 = i * 3U;
        size_t src = src_x2 / 2U;
        if (src >= in_samples) {
            src = in_samples - 1;
        }

        if ((src_x2 & 1U) == 0 || src + 1U >= in_samples) {
            out[i] = in[src];
        } else {
            out[i] = (int16_t)(((int32_t)in[src] + (int32_t)in[src + 1U]) / 2);
        }
    }
    return XIAOZHI_TTS_OUTPUT_SAMPLES;
}

static esp_err_t xiaozhi_tts_decoder_init(void)
{
    if (s_decoder != NULL) {
        return ESP_OK;
    }

    esp_opus_dec_cfg_t cfg = {
        .sample_rate = ESP_AUDIO_SAMPLE_RATE_24K,
        .channel = ESP_AUDIO_MONO,
        .frame_duration = ESP_OPUS_DEC_FRAME_DURATION_60_MS,
        .self_delimited = false,
    };

    int ret = esp_opus_dec_open(&cfg, sizeof(cfg), &s_decoder);
    if (ret != ESP_AUDIO_ERR_OK || s_decoder == NULL) {
        ESP_LOGE(TAG, "Opus decoder open failed: %d", ret);
        s_decoder = NULL;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "XiaoZhi TTS decoder ready: in=%d Hz out=%d Hz frame=%d ms",
             XIAOZHI_TTS_INPUT_SAMPLE_RATE_HZ,
             XIAOZHI_TTS_OUTPUT_SAMPLE_RATE_HZ,
             XIAOZHI_TTS_FRAME_DURATION_MS);
    return ESP_OK;
}

static void xiaozhi_tts_decoder_release(void)
{
    if (s_decoder == NULL) {
        return;
    }

    esp_audio_err_t ret = esp_opus_dec_close(s_decoder);
    if (ret != ESP_AUDIO_ERR_OK) {
        ESP_LOGW(TAG, "Opus decoder close failed: %d", ret);
    }
    s_decoder = NULL;
    ESP_LOGI(TAG, "XiaoZhi TTS decoder released");
}

static esp_err_t xiaozhi_tts_decode_and_play(const uint8_t *opus, size_t len)
{
    if (opus == NULL || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    ESP_RETURN_ON_ERROR(xiaozhi_tts_decoder_init(), TAG, "TTS decoder init failed");

    int16_t pcm_24k[XIAOZHI_TTS_INPUT_SAMPLES] = { 0 };
    int16_t pcm_16k[XIAOZHI_TTS_OUTPUT_SAMPLES] = { 0 };
    esp_audio_dec_in_raw_t raw = {
        .buffer = (uint8_t *)opus,
        .len = (uint32_t)len,
        .consumed = 0,
        .frame_recover = ESP_AUDIO_DEC_RECOVERY_NONE,
    };
    esp_audio_dec_out_frame_t frame = {
        .buffer = (uint8_t *)pcm_24k,
        .len = sizeof(pcm_24k),
        .needed_size = 0,
        .decoded_size = 0,
    };
    esp_audio_dec_info_t info = { 0 };

    int ret = esp_opus_dec_decode(s_decoder, &raw, &frame, &info);
    if (ret != ESP_AUDIO_ERR_OK) {
        ESP_LOGW(TAG, "TTS Opus decode failed: %d", ret);
        return ESP_FAIL;
    }
    if (frame.decoded_size == 0) {
        return ESP_OK;
    }

    size_t pcm_samples = frame.decoded_size / sizeof(pcm_24k[0]);
    int out_samples = xiaozhi_tts_resample_24k_to_16k(pcm_24k,
                                                      pcm_samples,
                                                      pcm_16k,
                                                      XIAOZHI_TTS_OUTPUT_SAMPLES);
    if (out_samples <= 0) {
        return ESP_FAIL;
    }

    return audio_output_write_pcm(pcm_16k, (size_t)out_samples);
}

static void xiaozhi_tts_abort_output_after_error(void)
{
    if (s_playing) {
        esp_err_t ret = audio_output_stop();
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "stop speaker after TTS frame failure failed: %s", esp_err_to_name(ret));
        }
    }
    xiaozhi_tts_decoder_release();
    s_playing = false;
    s_discarding = true;
    ESP_LOGW(TAG, "XiaoZhi TTS output disabled until stop");
}

static void xiaozhi_tts_player_task(void *arg)
{
    (void)arg;

    while (true) {
        xiaozhi_tts_msg_t msg = { 0 };
        if (xQueueReceive(s_queue, &msg, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        if (msg.type == XIAOZHI_TTS_MSG_START) {
            s_discarding = false;
            if (xiaozhi_tts_decoder_init() == ESP_OK &&
                audio_output_start() == ESP_OK) {
                s_playing = true;
                ESP_LOGI(TAG, "XiaoZhi TTS playback started");
            } else {
                s_discarding = true;
            }
        } else if (msg.type == XIAOZHI_TTS_MSG_OPUS) {
            if (s_discarding) {
                continue;
            }
            if (!s_playing) {
                if (xiaozhi_tts_decoder_init() != ESP_OK ||
                    audio_output_start() != ESP_OK) {
                    ESP_LOGW(TAG, "start speaker before TTS frame failed");
                    s_discarding = true;
                    continue;
                }
                s_playing = true;
                ESP_LOGI(TAG, "XiaoZhi TTS playback started");
            }
            esp_err_t ret = xiaozhi_tts_decode_and_play(msg.data, msg.len);
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "TTS frame playback failed: %s", esp_err_to_name(ret));
                xiaozhi_tts_abort_output_after_error();
            }
        } else if (msg.type == XIAOZHI_TTS_MSG_STOP) {
            if (s_playing) {
                if (audio_output_stop() != ESP_OK) {
                    ESP_LOGW(TAG, "stop speaker after TTS failed");
                }
            }
            xiaozhi_tts_decoder_release();
            s_playing = false;
            s_discarding = false;
            ESP_LOGI(TAG, "XiaoZhi TTS playback stopped");
            if (s_stop_done != NULL) {
                xSemaphoreGive(s_stop_done);
            }
        }
    }
}

esp_err_t xiaozhi_tts_player_init(void)
{
    if (s_task != NULL) {
        return ESP_OK;
    }

    s_queue = xQueueCreate(XIAOZHI_TTS_QUEUE_LENGTH, sizeof(xiaozhi_tts_msg_t));
    if (s_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }

    s_stop_done = xSemaphoreCreateBinary();
    if (s_stop_done == NULL) {
        vQueueDelete(s_queue);
        s_queue = NULL;
        return ESP_ERR_NO_MEM;
    }

    BaseType_t ok = xTaskCreate(xiaozhi_tts_player_task,
                                "xz_tts_player",
                                XIAOZHI_TTS_TASK_STACK,
                                NULL,
                                XIAOZHI_TTS_TASK_PRIORITY,
                                &s_task);
    if (ok != pdPASS) {
        vQueueDelete(s_queue);
        s_queue = NULL;
        vSemaphoreDelete(s_stop_done);
        s_stop_done = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

static esp_err_t xiaozhi_tts_send_msg(const xiaozhi_tts_msg_t *msg)
{
    if (s_queue == NULL || msg == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    TickType_t wait_ticks = pdMS_TO_TICKS(XIAOZHI_TTS_CONTROL_QUEUE_WAIT_MS);
    if (msg->type == XIAOZHI_TTS_MSG_OPUS) {
        wait_ticks = pdMS_TO_TICKS(XIAOZHI_TTS_OPUS_QUEUE_WAIT_MS);
    }
    if (xQueueSend(s_queue, msg, wait_ticks) == pdTRUE) {
        return ESP_OK;
    }
    ESP_LOGW(TAG, "TTS queue full: type=%d free=%u/%u",
             (int)msg->type,
             (unsigned int)uxQueueSpacesAvailable(s_queue),
             (unsigned int)XIAOZHI_TTS_QUEUE_LENGTH);
    return ESP_ERR_TIMEOUT;
}

esp_err_t xiaozhi_tts_player_start(void)
{
    xiaozhi_tts_msg_t msg = {
        .type = XIAOZHI_TTS_MSG_START,
    };
    return xiaozhi_tts_send_msg(&msg);
}

esp_err_t xiaozhi_tts_player_write_opus(const uint8_t *data, size_t len)
{
    if (data == NULL || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (len > XIAOZHI_TTS_OPUS_MAX_BYTES) {
        ESP_LOGW(TAG, "TTS Opus frame too large: %u", (unsigned int)len);
        return ESP_ERR_INVALID_SIZE;
    }

    xiaozhi_tts_msg_t msg = {
        .type = XIAOZHI_TTS_MSG_OPUS,
        .len = len,
    };
    memcpy(msg.data, data, len);
    return xiaozhi_tts_send_msg(&msg);
}

esp_err_t xiaozhi_tts_player_stop(void)
{
    xiaozhi_tts_msg_t msg = {
        .type = XIAOZHI_TTS_MSG_STOP,
    };
    if (s_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    xQueueReset(s_queue);
    if (s_stop_done != NULL) {
        xSemaphoreTake(s_stop_done, 0);
    }
    if (xQueueSendToFront(s_queue,
                          &msg,
                          pdMS_TO_TICKS(XIAOZHI_TTS_CONTROL_QUEUE_WAIT_MS)) == pdTRUE) {
        if (s_stop_done == NULL ||
            xSemaphoreTake(s_stop_done,
                           pdMS_TO_TICKS(XIAOZHI_TTS_CONTROL_QUEUE_WAIT_MS)) == pdTRUE) {
            return ESP_OK;
        }
        return ESP_ERR_TIMEOUT;
    }
    return ESP_ERR_TIMEOUT;
}

bool xiaozhi_tts_player_is_playing(void)
{
    return s_playing;
}
