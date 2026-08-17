#include "audio_http_player.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "app_events.h"
#include "audio_output.h"
#include "esp_audio_dec.h"
#include "esp_audio_dec_default.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/ringbuf.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#define AUDIO_HTTP_PLAYER_TASK_STACK 12288
#define AUDIO_HTTP_PLAYER_TASK_PRIORITY 6
#define AUDIO_HTTP_PLAYER_URL_SIZE 512
#define AUDIO_HTTP_PLAYER_HTTP_BUFFER_BYTES 8192
#define AUDIO_HTTP_PLAYER_IN_BYTES 32768
#define AUDIO_HTTP_PLAYER_OUT_BYTES 8192
#define AUDIO_HTTP_PLAYER_PCM_CHUNK 512
#define AUDIO_HTTP_PLAYER_PREFETCH_BYTES 262144
#define AUDIO_HTTP_PLAYER_PREFETCH_TASK_STACK 6144
#define AUDIO_HTTP_PLAYER_PREFETCH_TASK_PRIORITY 7
#define AUDIO_HTTP_PLAYER_TIMEOUT_MS 15000
#define AUDIO_HTTP_PLAYER_OPEN_RETRIES 2
#define AUDIO_HTTP_PLAYER_READ_RETRIES 25
#define AUDIO_HTTP_PLAYER_READ_RETRY_DELAY_MS 40
#define AUDIO_HTTP_PLAYER_SYNC_SCAN_BYTES 4096

typedef struct {
    uint32_t input_rate;
    uint8_t channels;
    uint32_t acc;
} audio_http_resample_t;

typedef struct {
    esp_http_client_handle_t http;
    RingbufHandle_t ring;
    volatile bool stop;
    volatile bool done;
    volatile bool task_done;
    esp_err_t error;
    size_t bytes;
} audio_http_prefetch_t;

static const char *TAG = "audio_http_player";
static QueueHandle_t s_event_queue;
static TaskHandle_t s_player_task;
static volatile bool s_playing;
static volatile bool s_stop_requested;
static char s_url[AUDIO_HTTP_PLAYER_URL_SIZE];
static bool s_decoders_registered;

static void audio_http_player_post_event(app_event_type_t type)
{
    if (s_event_queue == NULL) {
        return;
    }

    app_event_t event = {
        .type = type,
        .gesture = APP_GESTURE_NONE,
        .button = APP_BUTTON_NONE,
        .message = "http_music",
    };
    xQueueSend(s_event_queue, &event, 0);
}

static int audio_http_player_read_more(esp_http_client_handle_t http,
                                       uint8_t *buffer,
                                       size_t len)
{
    if (http == NULL || buffer == NULL || len == 0) {
        return -1;
    }

    for (int attempt = 0; attempt <= AUDIO_HTTP_PLAYER_READ_RETRIES; ++attempt) {
        int read = esp_http_client_read(http, (char *)buffer, len);
        if (read > 0 || s_stop_requested) {
            return read;
        }
        if (read == 0 && esp_http_client_is_complete_data_received(http)) {
            return 0;
        }
        ESP_LOGW(TAG, "retry read MP3 HTTP stream: attempt=%d read=%d",
                 attempt + 1,
                 read);
        vTaskDelay(pdMS_TO_TICKS(AUDIO_HTTP_PLAYER_READ_RETRY_DELAY_MS));
    }
    return -1;
}

static void audio_http_player_prefetch_task(void *arg)
{
    audio_http_prefetch_t *prefetch = (audio_http_prefetch_t *)arg;
    uint8_t *buffer = malloc(AUDIO_HTTP_PLAYER_HTTP_BUFFER_BYTES);
    if (prefetch == NULL || buffer == NULL) {
        if (prefetch != NULL) {
            prefetch->error = ESP_ERR_NO_MEM;
            prefetch->done = true;
            prefetch->task_done = true;
        }
        free(buffer);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "HTTP MP3 prefetch started: buffer=%u",
             (unsigned)AUDIO_HTTP_PLAYER_PREFETCH_BYTES);

    while (!prefetch->stop && !s_stop_requested) {
        int read = audio_http_player_read_more(prefetch->http,
                                               buffer,
                                               AUDIO_HTTP_PLAYER_HTTP_BUFFER_BYTES);
        if (read < 0) {
            prefetch->error = ESP_FAIL;
            break;
        }
        if (read == 0) {
            break;
        }

        size_t sent = 0;
        while (sent < (size_t)read && !prefetch->stop && !s_stop_requested) {
            size_t chunk = (size_t)read - sent;
            if (xRingbufferSend(prefetch->ring,
                                buffer + sent,
                                chunk,
                                pdMS_TO_TICKS(100)) == pdTRUE) {
                sent += chunk;
            }
        }
        prefetch->bytes += (size_t)read;
    }

    ESP_LOGI(TAG, "HTTP MP3 prefetch done: bytes=%u error=%s stop=%d",
             (unsigned)prefetch->bytes,
             esp_err_to_name(prefetch->error),
             prefetch->stop || s_stop_requested);
    free(buffer);
    prefetch->done = true;
    prefetch->task_done = true;
    vTaskDelete(NULL);
}

static int audio_http_player_prefetch_read(audio_http_prefetch_t *prefetch,
                                           uint8_t *buffer,
                                           size_t len)
{
    if (prefetch == NULL || prefetch->ring == NULL || buffer == NULL || len == 0) {
        return -1;
    }

    size_t copied = 0;
    while (copied < len && !s_stop_requested) {
        size_t item_size = 0;
        uint8_t *item = (uint8_t *)xRingbufferReceiveUpTo(prefetch->ring,
                                                          &item_size,
                                                          pdMS_TO_TICKS(80),
                                                          len - copied);
        if (item != NULL && item_size > 0) {
            memcpy(buffer + copied, item, item_size);
            copied += item_size;
            vRingbufferReturnItem(prefetch->ring, item);
            continue;
        }

        if (prefetch->done) {
            if (copied > 0) {
                return (int)copied;
            }
            return prefetch->error == ESP_OK ? 0 : -1;
        }
        if (copied > 0) {
            return (int)copied;
        }
    }
    return (int)copied;
}

static size_t audio_http_player_id3v2_tag_size(const uint8_t *header, size_t len)
{
    if (header == NULL || len < 10 || memcmp(header, "ID3", 3) != 0) {
        return 0;
    }
    if ((header[6] & 0x80) != 0 || (header[7] & 0x80) != 0 ||
        (header[8] & 0x80) != 0 || (header[9] & 0x80) != 0) {
        return 0;
    }

    /* ID3v2 stores size as a 28-bit synchsafe integer. */
    size_t synchsafe = ((size_t)header[6] << 21) |
                       ((size_t)header[7] << 14) |
                       ((size_t)header[8] << 7) |
                       header[9];
    size_t total = 10 + synchsafe;
    if ((header[5] & 0x10) != 0) {
        total += 10;
    }
    return total;
}

static esp_err_t audio_http_player_skip_id3v2(esp_http_client_handle_t http,
                                              uint8_t *buffer,
                                              size_t buffer_size,
                                              size_t *prefix_len)
{
    if (http == NULL || buffer == NULL || buffer_size < 10 || prefix_len == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *prefix_len = 0;
    while (*prefix_len < 10) {
        int read = audio_http_player_read_more(http,
                                               buffer + *prefix_len,
                                               10 - *prefix_len);
        if (read < 0) {
            return ESP_FAIL;
        }
        if (read == 0) {
            return *prefix_len > 0 ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
        }
        *prefix_len += (size_t)read;
    }

    size_t tag_size = audio_http_player_id3v2_tag_size(buffer, *prefix_len);
    if (tag_size == 0) {
        return ESP_OK;
    }

    size_t remaining = tag_size - *prefix_len;
    while (remaining > 0) {
        size_t chunk = remaining > buffer_size ? buffer_size : remaining;
        int read = audio_http_player_read_more(http, buffer, chunk);
        if (read < 0) {
            return ESP_FAIL;
        }
        if (read == 0) {
            return ESP_ERR_INVALID_RESPONSE;
        }
        remaining -= (size_t)read;
    }

    ESP_LOGI(TAG, "ID3v2 tag skipped: %u bytes", (unsigned)tag_size);
    *prefix_len = 0;
    return ESP_OK;
}

static bool audio_http_player_is_mp3_sync(const uint8_t *data, size_t len, size_t offset)
{
    if (data == NULL || offset + 1 >= len) {
        return false;
    }
    return data[offset] == 0xFF && (data[offset + 1] & 0xE0) == 0xE0;
}

static ssize_t audio_http_player_find_mp3_sync(const uint8_t *data, size_t len)
{
    if (data == NULL || len < 2) {
        return -1;
    }
    for (size_t i = 0; i + 1 < len; ++i) {
        if (audio_http_player_is_mp3_sync(data, len, i)) {
            return (ssize_t)i;
        }
    }
    return -1;
}

static esp_err_t audio_http_player_prime_mp3_stream(esp_http_client_handle_t http,
                                                    uint8_t *buffer,
                                                    size_t buffer_size,
                                                    size_t *prefix_len)
{
    if (http == NULL || buffer == NULL || prefix_len == NULL || buffer_size < 10) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = audio_http_player_skip_id3v2(http,
                                                 buffer,
                                                 buffer_size,
                                                 prefix_len);
    if (ret != ESP_OK) {
        return ret;
    }

    while (*prefix_len < buffer_size && *prefix_len < AUDIO_HTTP_PLAYER_SYNC_SCAN_BYTES) {
        ssize_t sync = audio_http_player_find_mp3_sync(buffer, *prefix_len);
        if (sync >= 0) {
            if (sync > 0) {
                memmove(buffer, buffer + sync, *prefix_len - (size_t)sync);
                *prefix_len -= (size_t)sync;
                ESP_LOGI(TAG, "MP3 sync aligned: skipped=%d bytes", (int)sync);
            }
            return ESP_OK;
        }

        int read = audio_http_player_read_more(http,
                                               buffer + *prefix_len,
                                               buffer_size - *prefix_len);
        if (read < 0) {
            return ESP_FAIL;
        }
        if (read == 0) {
            break;
        }
        *prefix_len += (size_t)read;
    }

    ssize_t sync = audio_http_player_find_mp3_sync(buffer, *prefix_len);
    if (sync < 0) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    if (sync > 0) {
        memmove(buffer, buffer + sync, *prefix_len - (size_t)sync);
        *prefix_len -= (size_t)sync;
        ESP_LOGI(TAG, "MP3 sync aligned: skipped=%d bytes", (int)sync);
    }
    return ESP_OK;
}

static int16_t audio_http_player_mono_sample(const int16_t *samples,
                                             size_t frame_index,
                                             uint8_t channels)
{
    if (channels == 1) {
        return samples[frame_index];
    }

    size_t offset = frame_index * channels;
    int32_t mixed = 0;
    for (uint8_t ch = 0; ch < channels; ++ch) {
        mixed += samples[offset + ch];
    }
    return (int16_t)(mixed / channels);
}

static esp_err_t audio_http_player_write_pcm(audio_http_resample_t *resample,
                                             const uint8_t *pcm,
                                             size_t bytes)
{
    if (resample == NULL || pcm == NULL || bytes == 0) {
        return ESP_OK;
    }
    if (resample->input_rate == 0 || resample->channels == 0) {
        return ESP_ERR_INVALID_STATE;
    }

    const uint32_t output_rate = CONFIG_SMART_SPEAKER_AUDIO_OUTPUT_SAMPLE_RATE_HZ;
    const int16_t *samples = (const int16_t *)pcm;
    size_t source_frames = bytes / (sizeof(int16_t) * resample->channels);
    int16_t out[AUDIO_HTTP_PLAYER_PCM_CHUNK] = { 0 };
    size_t out_count = 0;

    for (size_t i = 0; i < source_frames; ++i) {
        int16_t mono = audio_http_player_mono_sample(samples, i, resample->channels);
        resample->acc += output_rate;
        while (resample->acc >= resample->input_rate) {
            resample->acc -= resample->input_rate;
            out[out_count++] = mono;
            if (out_count == AUDIO_HTTP_PLAYER_PCM_CHUNK) {
                ESP_RETURN_ON_ERROR(audio_output_write_pcm(out, out_count),
                                    TAG, "write decoded MP3 PCM failed");
                out_count = 0;
            }
        }
    }

    if (out_count > 0) {
        ESP_RETURN_ON_ERROR(audio_output_write_pcm(out, out_count),
                            TAG, "write decoded MP3 PCM tail failed");
    }
    return ESP_OK;
}

static void audio_http_player_consume_raw(esp_audio_dec_in_raw_t *raw,
                                          const char *reason)
{
    if (raw == NULL || raw->consumed == 0) {
        return;
    }

    if (raw->consumed > raw->len) {
        ESP_LOGW(TAG, "MP3 decoder consumed overflow: consumed=%lu len=%lu reason=%s",
                 (unsigned long)raw->consumed,
                 (unsigned long)raw->len,
                 reason != NULL ? reason : "unknown");
        raw->consumed = raw->len;
    }

    raw->buffer += raw->consumed;
    raw->len -= raw->consumed;
    raw->consumed = 0;
}

static esp_err_t audio_http_player_process_decoded(esp_audio_dec_handle_t decoder,
                                                   esp_audio_dec_out_frame_t *out_frame,
                                                   audio_http_resample_t *resample,
                                                   bool *info_ready,
                                                   bool *output_started,
                                                   uint32_t *decoded_bytes)
{
    if (out_frame->decoded_size == 0) {
        return ESP_OK;
    }

    if (!*info_ready) {
        esp_audio_dec_info_t info = { 0 };
        esp_audio_err_t info_ret = esp_audio_dec_get_info(decoder, &info);
        if (info_ret != ESP_AUDIO_ERR_OK) {
            ESP_LOGW(TAG, "MP3 info not ready: %d", (int)info_ret);
            return ESP_OK;
        }
        if (info.bits_per_sample != 16 || info.channel == 0) {
            ESP_LOGW(TAG, "Unsupported MP3 PCM: rate=%lu bits=%u channel=%u",
                     (unsigned long)info.sample_rate,
                     (unsigned int)info.bits_per_sample,
                     (unsigned int)info.channel);
            return ESP_ERR_NOT_SUPPORTED;
        }
        resample->input_rate = info.sample_rate;
        resample->channels = info.channel;
        resample->acc = 0;
        *info_ready = true;
        ESP_LOGI(TAG, "HTTP MP3 info: sample_rate=%lu bits=%u channels=%u",
                 (unsigned long)info.sample_rate,
                 (unsigned int)info.bits_per_sample,
                 (unsigned int)info.channel);
    }

    if (!*output_started) {
        ESP_RETURN_ON_ERROR(audio_output_start(), TAG, "start decoded MP3 output failed");
        *output_started = true;
    }

    ESP_RETURN_ON_ERROR(audio_http_player_write_pcm(resample,
                                                    out_frame->buffer,
                                                    out_frame->decoded_size),
                        TAG, "output decoded MP3 failed");
    *decoded_bytes += out_frame->decoded_size;
    return ESP_OK;
}

static esp_err_t audio_http_player_open_http(esp_http_client_handle_t http)
{
    esp_err_t ret = ESP_FAIL;
    for (int attempt = 0; attempt <= AUDIO_HTTP_PLAYER_OPEN_RETRIES; ++attempt) {
        ret = esp_http_client_open(http, 0);
        if (ret == ESP_OK) {
            return ESP_OK;
        }
        ESP_LOGW(TAG, "retry open MP3 URL: attempt=%d ret=%s",
                 attempt + 1,
                 esp_err_to_name(ret));
        esp_http_client_close(http);
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    return ret;
}

static esp_err_t audio_http_player_decode_url(const char *url)
{
    if (url == NULL || strlen(url) == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_http_client_config_t http_cfg = {
        .url = url,
        .timeout_ms = AUDIO_HTTP_PLAYER_TIMEOUT_MS,
        .buffer_size = AUDIO_HTTP_PLAYER_HTTP_BUFFER_BYTES,
    };
    esp_http_client_handle_t http = esp_http_client_init(&http_cfg);
    if (http == NULL) {
        return ESP_ERR_NO_MEM;
    }

    uint8_t *in_buf = NULL;
    uint8_t *out_buf = NULL;
    esp_audio_dec_handle_t decoder = NULL;
    audio_http_prefetch_t prefetch = { 0 };
    TaskHandle_t prefetch_task = NULL;
    esp_err_t ret = ESP_OK;
    bool output_started = false;

    do {
        ESP_GOTO_ON_ERROR(audio_http_player_open_http(http), fail, TAG, "open MP3 URL failed");
        int status = esp_http_client_fetch_headers(http);
        if (status < 0) {
            ESP_LOGW(TAG, "fetch MP3 headers failed: %d", status);
        }
        int status_code = esp_http_client_get_status_code(http);
        if (status_code != 200) {
            ESP_LOGW(TAG, "MP3 HTTP status=%d", status_code);
            ret = ESP_FAIL;
            break;
        }
        ESP_LOGI(TAG, "MP3 HTTP status=%d content_length=%lld",
                 status_code,
                 esp_http_client_get_content_length(http));

        in_buf = malloc(AUDIO_HTTP_PLAYER_IN_BYTES);
        out_buf = malloc(AUDIO_HTTP_PLAYER_OUT_BYTES);
        if (in_buf == NULL || out_buf == NULL) {
            ret = ESP_ERR_NO_MEM;
            break;
        }

        size_t pending_len = 0;
        ret = audio_http_player_prime_mp3_stream(http,
                                                 in_buf,
                                                 AUDIO_HTTP_PLAYER_IN_BYTES,
                                                 &pending_len);
        if (ret != ESP_OK) {
            break;
        }

        prefetch.http = http;
        prefetch.error = ESP_OK;
        prefetch.ring = xRingbufferCreateWithCaps(AUDIO_HTTP_PLAYER_PREFETCH_BYTES,
                                                  RINGBUF_TYPE_BYTEBUF,
                                                  MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (prefetch.ring == NULL) {
            prefetch.ring = xRingbufferCreateWithCaps(AUDIO_HTTP_PLAYER_PREFETCH_BYTES,
                                                      RINGBUF_TYPE_BYTEBUF,
                                                      MALLOC_CAP_8BIT);
        }
        if (prefetch.ring == NULL) {
            ret = ESP_ERR_NO_MEM;
            break;
        }
        BaseType_t task_ok = xTaskCreate(audio_http_player_prefetch_task,
                                         "mp3_prefetch",
                                         AUDIO_HTTP_PLAYER_PREFETCH_TASK_STACK,
                                         &prefetch,
                                         AUDIO_HTTP_PLAYER_PREFETCH_TASK_PRIORITY,
                                         &prefetch_task);
        if (task_ok != pdPASS) {
            ret = ESP_ERR_NO_MEM;
            break;
        }

        ESP_LOGI(TAG, "HTTP MP3 heap before decoder: free=%lu min=%lu largest=%lu",
                 (unsigned long)heap_caps_get_free_size(MALLOC_CAP_8BIT),
                 (unsigned long)heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT),
                 (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));

        esp_audio_dec_cfg_t dec_cfg = {
            .type = ESP_AUDIO_TYPE_MP3,
            .cfg = NULL,
            .cfg_sz = 0,
        };
        esp_audio_err_t dec_ret = esp_audio_dec_open(&dec_cfg, &decoder);
        if (dec_ret != ESP_AUDIO_ERR_OK) {
            ESP_LOGW(TAG, "open MP3 decoder failed: %d", (int)dec_ret);
            ret = ESP_FAIL;
            break;
        }

        audio_http_resample_t resample = { 0 };
        bool info_ready = false;
        uint32_t decoded_bytes = 0;
        int out_size = AUDIO_HTTP_PLAYER_OUT_BYTES;

        while (!s_stop_requested) {
            int read = (int)pending_len;
            if (pending_len < AUDIO_HTTP_PLAYER_IN_BYTES) {
                int more = audio_http_player_prefetch_read(&prefetch,
                                                           in_buf + pending_len,
                                                           AUDIO_HTTP_PLAYER_IN_BYTES - pending_len);
                if (more < 0) {
                    ret = ESP_FAIL;
                    break;
                }
                read += more;
            }
            if (read == 0) {
                break;
            }

            esp_audio_dec_in_raw_t raw = {
                .buffer = in_buf,
                .len = (uint32_t)read,
            };
            pending_len = 0;

            while (raw.len > 0 && !s_stop_requested) {
                esp_audio_dec_out_frame_t out_frame = {
                    .buffer = out_buf,
                    .len = (uint32_t)out_size,
                };

                dec_ret = esp_audio_dec_process(decoder, &raw, &out_frame);
                if (dec_ret == ESP_AUDIO_ERR_BUFF_NOT_ENOUGH) {
                    uint8_t *new_out = realloc(out_buf, out_frame.needed_size);
                    if (new_out == NULL) {
                        ret = ESP_ERR_NO_MEM;
                        break;
                    }
                    out_buf = new_out;
                    out_size = (int)out_frame.needed_size;
                    continue;
                }
                if (dec_ret == ESP_AUDIO_ERR_DATA_LACK) {
                    audio_http_player_consume_raw(&raw, "data_lack");
                    ESP_LOGD(TAG, "MP3 data lack, waiting for more HTTP data: remain=%lu",
                             (unsigned long)raw.len);
                    break;
                }
                if (dec_ret != ESP_AUDIO_ERR_OK) {
                    ESP_LOGW(TAG, "MP3 decode failed: %d free=%lu largest=%lu",
                             (int)dec_ret,
                             (unsigned long)heap_caps_get_free_size(MALLOC_CAP_8BIT),
                             (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
                    ret = ESP_FAIL;
                    break;
                }

                ret = audio_http_player_process_decoded(decoder, &out_frame,
                                                        &resample, &info_ready,
                                                        &output_started,
                                                        &decoded_bytes);
                if (ret != ESP_OK) {
                    break;
                }

                if (raw.consumed == 0) {
                    break;
                }
                audio_http_player_consume_raw(&raw, "decoded");
            }

            if (ret != ESP_OK) {
                break;
            }
            if (raw.len > 0) {
                if (raw.len > AUDIO_HTTP_PLAYER_IN_BYTES) {
                    ret = ESP_ERR_INVALID_SIZE;
                    break;
                }
                memmove(in_buf, raw.buffer, raw.len);
                pending_len = raw.len;
            }
        }

        if (ret == ESP_OK && decoded_bytes == 0 && !s_stop_requested) {
            ret = ESP_ERR_INVALID_RESPONSE;
        }
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "HTTP MP3 playback %s: decoded=%lu url=%s",
                     s_stop_requested ? "stopped" : "finished",
                     (unsigned long)decoded_bytes,
                     url);
        }
    } while (0);

fail:
    if (decoder != NULL) {
        esp_audio_dec_close(decoder);
    }
    prefetch.stop = true;
    if (prefetch_task != NULL) {
        TickType_t wait_started = xTaskGetTickCount();
        while (!prefetch.task_done &&
               xTaskGetTickCount() - wait_started < pdMS_TO_TICKS(1200)) {
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }
    if (prefetch.ring != NULL) {
        vRingbufferDelete(prefetch.ring);
    }
    if (http != NULL) {
        esp_http_client_close(http);
        esp_http_client_cleanup(http);
    }
    free(in_buf);
    free(out_buf);

    if (output_started) {
        esp_err_t stop_ret = audio_output_stop();
        if (ret == ESP_OK) {
            ret = stop_ret;
        }
    }
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "HTTP MP3 playback failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

static void audio_http_player_task(void *arg)
{
    (void)arg;

    while (true) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        char url[AUDIO_HTTP_PLAYER_URL_SIZE] = { 0 };
        strlcpy(url, s_url, sizeof(url));

        ESP_LOGI(TAG, "HTTP MP3 player stack free words=%u",
                 (unsigned)uxTaskGetStackHighWaterMark(NULL));
        audio_http_player_post_event(APP_EVENT_PLAYBACK_STARTED);
        esp_err_t ret = audio_http_player_decode_url(url);
        if (ret == ESP_OK) {
            audio_http_player_post_event(APP_EVENT_PLAYBACK_STOPPED);
        } else {
            audio_http_player_post_event(APP_EVENT_PLAYBACK_FAILED);
        }

        s_stop_requested = false;
        s_playing = false;
    }
}

esp_err_t audio_http_player_init(QueueHandle_t event_queue)
{
    if (!s_decoders_registered) {
        esp_audio_err_t dec_ret = esp_audio_dec_register_default();
        if (dec_ret != ESP_AUDIO_ERR_OK) {
            ESP_LOGW(TAG, "register audio decoders failed: %d", (int)dec_ret);
            return ESP_FAIL;
        }

        s_decoders_registered = true;
    }

    if (s_player_task != NULL) {
        return ESP_OK;
    }

    s_event_queue = event_queue;
    BaseType_t ok = xTaskCreate(audio_http_player_task, "http_mp3_player",
                                AUDIO_HTTP_PLAYER_TASK_STACK, NULL,
                                AUDIO_HTTP_PLAYER_TASK_PRIORITY, &s_player_task);
    if (ok != pdPASS) {
        s_player_task = NULL;
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "HTTP MP3 player task ready");
    return ESP_OK;
}

esp_err_t audio_http_player_play_url_async(const char *url)
{
    if (s_player_task == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (url == NULL || strlen(url) == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_playing) {
        return ESP_ERR_INVALID_STATE;
    }
    if (strlcpy(s_url, url, sizeof(s_url)) >= sizeof(s_url)) {
        return ESP_ERR_INVALID_ARG;
    }

    s_stop_requested = false;
    s_playing = true;
    xTaskNotifyGive(s_player_task);
    ESP_LOGI(TAG, "HTTP MP3 playback requested: %s", url);
    return ESP_OK;
}

esp_err_t audio_http_player_stop(void)
{
    if (!s_playing) {
        return ESP_ERR_INVALID_STATE;
    }

    s_stop_requested = true;
    ESP_LOGI(TAG, "HTTP MP3 playback stop requested");
    return ESP_OK;
}

bool audio_http_player_is_playing(void)
{
    return s_playing;
}
