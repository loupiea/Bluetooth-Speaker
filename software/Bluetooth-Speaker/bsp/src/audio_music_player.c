#include "audio_music_player.h"

#include <ctype.h>
#include <stdint.h>
#include <string.h>
#include "app_events.h"
#include "audio_http_player.h"
#include "audio_output.h"
#include "esp_check.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "sdkconfig.h"

#define AUDIO_MUSIC_PLAYER_TASK_STACK 6144
#define AUDIO_MUSIC_PLAYER_TASK_PRIORITY 6
#define AUDIO_MUSIC_PLAYER_URL_SIZE 512
#define AUDIO_MUSIC_PLAYER_HTTP_BUFFER_BYTES 1024
#define AUDIO_MUSIC_PLAYER_PCM_CHUNK 256
#define AUDIO_MUSIC_PLAYER_TIMEOUT_MS 15000
#define AUDIO_MUSIC_PLAYER_OPEN_RETRIES 2
#define AUDIO_MUSIC_PLAYER_READ_RETRIES 25
#define AUDIO_MUSIC_PLAYER_READ_RETRY_DELAY_MS 80

typedef struct {
    uint16_t audio_format;
    uint16_t channels;
    uint32_t sample_rate;
    uint16_t block_align;
    uint16_t bits_per_sample;
    uint32_t data_size;
} audio_music_wav_info_t;

typedef struct {
    uint32_t input_rate;
    uint8_t channels;
    uint16_t bits_per_sample;
    uint32_t acc;
} audio_music_resample_t;

static const char *TAG = "audio_music_player";
static QueueHandle_t s_event_queue;
static TaskHandle_t s_wav_task;
static volatile bool s_wav_playing;
static volatile bool s_wav_stop_requested;
static char s_wav_url[AUDIO_MUSIC_PLAYER_URL_SIZE];

static void audio_music_player_post_event(app_event_type_t type)
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

static bool audio_music_player_has_suffix(const char *url, const char *suffix)
{
    if (url == NULL || suffix == NULL) {
        return false;
    }

    const char *end = strchr(url, '?');
    size_t url_len = end != NULL ? (size_t)(end - url) : strlen(url);
    size_t suffix_len = strlen(suffix);
    if (url_len < suffix_len) {
        return false;
    }

    const char *start = url + url_len - suffix_len;
    for (size_t i = 0; i < suffix_len; ++i) {
        if (tolower((unsigned char)start[i]) != tolower((unsigned char)suffix[i])) {
            return false;
        }
    }
    return true;
}

static bool audio_music_player_is_mp3_url(const char *url)
{
    return audio_music_player_has_suffix(url, ".mp3");
}

static bool audio_music_player_is_wav_url(const char *url)
{
    return audio_music_player_has_suffix(url, ".wav");
}

static esp_err_t audio_music_player_read_exact(esp_http_client_handle_t http,
                                               uint8_t *buffer,
                                               size_t len)
{
    size_t offset = 0;
    uint32_t retry_count = 0;
    while (offset < len) {
        if (s_wav_stop_requested) {
            return ESP_ERR_INVALID_STATE;
        }
        int read = esp_http_client_read(http, (char *)buffer + offset, len - offset);
        if (read < 0) {
            retry_count++;
            if (retry_count > AUDIO_MUSIC_PLAYER_READ_RETRIES) {
                ESP_LOGW(TAG, "retry read WAV HTTP stream exhausted: read=%d offset=%u len=%u",
                         read,
                         (unsigned)offset,
                         (unsigned)len);
                return ESP_FAIL;
            }
            ESP_LOGW(TAG, "retry read WAV HTTP stream: read=%d retry=%lu offset=%u len=%u",
                     read,
                     (unsigned long)retry_count,
                     (unsigned)offset,
                     (unsigned)len);
            vTaskDelay(pdMS_TO_TICKS(AUDIO_MUSIC_PLAYER_READ_RETRY_DELAY_MS));
            continue;
        }
        if (read == 0) {
            retry_count++;
            if (retry_count > AUDIO_MUSIC_PLAYER_READ_RETRIES) {
                ESP_LOGW(TAG, "retry read WAV HTTP stream exhausted: read=0 offset=%u len=%u",
                         (unsigned)offset,
                         (unsigned)len);
                return ESP_ERR_INVALID_RESPONSE;
            }
            ESP_LOGW(TAG, "retry read WAV HTTP stream: read=0 retry=%lu offset=%u len=%u",
                     (unsigned long)retry_count,
                     (unsigned)offset,
                     (unsigned)len);
            vTaskDelay(pdMS_TO_TICKS(AUDIO_MUSIC_PLAYER_READ_RETRY_DELAY_MS));
            continue;
        }
        offset += (size_t)read;
        retry_count = 0;
    }
    return ESP_OK;
}

static uint16_t audio_music_le16(const uint8_t *data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static uint32_t audio_music_le32(const uint8_t *data)
{
    return (uint32_t)data[0] |
           ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) |
           ((uint32_t)data[3] << 24);
}

static esp_err_t audio_music_player_skip_bytes(esp_http_client_handle_t http,
                                               uint8_t *buffer,
                                               size_t buffer_size,
                                               uint32_t bytes)
{
    while (bytes > 0) {
        size_t chunk = bytes > buffer_size ? buffer_size : bytes;
        ESP_RETURN_ON_ERROR(audio_music_player_read_exact(http, buffer, chunk),
                            TAG, "skip WAV chunk failed");
        bytes -= (uint32_t)chunk;
    }
    return ESP_OK;
}

static esp_err_t audio_music_player_read_wav_header(esp_http_client_handle_t http,
                                                    uint8_t *scratch,
                                                    size_t scratch_size,
                                                    audio_music_wav_info_t *info)
{
    if (http == NULL || scratch == NULL || scratch_size < 16 || info == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t riff[12] = { 0 };
    ESP_RETURN_ON_ERROR(audio_music_player_read_exact(http, riff, sizeof(riff)),
                        TAG, "read WAV RIFF header failed");
    if (memcmp(riff, "RIFF", 4) != 0 || memcmp(riff + 8, "WAVE", 4) != 0) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    bool fmt_found = false;
    bool data_found = false;
    while (!data_found) {
        uint8_t chunk_header[8] = { 0 };
        ESP_RETURN_ON_ERROR(audio_music_player_read_exact(http, chunk_header, sizeof(chunk_header)),
                            TAG, "read WAV chunk header failed");
        uint32_t chunk_size = audio_music_le32(chunk_header + 4);

        if (memcmp(chunk_header, "fmt ", 4) == 0) {
            if (chunk_size < 16) {
                return ESP_ERR_INVALID_RESPONSE;
            }
            uint8_t fmt[16] = { 0 };
            ESP_RETURN_ON_ERROR(audio_music_player_read_exact(http, fmt, sizeof(fmt)),
                                TAG, "read WAV fmt chunk failed");
            info->audio_format = audio_music_le16(fmt);
            info->channels = audio_music_le16(fmt + 2);
            info->sample_rate = audio_music_le32(fmt + 4);
            info->block_align = audio_music_le16(fmt + 12);
            info->bits_per_sample = audio_music_le16(fmt + 14);
            fmt_found = true;
            if (chunk_size > sizeof(fmt)) {
                ESP_RETURN_ON_ERROR(audio_music_player_skip_bytes(http,
                                                                  scratch,
                                                                  scratch_size,
                                                                  chunk_size - sizeof(fmt)),
                                    TAG, "skip WAV fmt extra failed");
            }
        } else if (memcmp(chunk_header, "data", 4) == 0) {
            if (!fmt_found) {
                return ESP_ERR_INVALID_RESPONSE;
            }
            info->data_size = chunk_size;
            data_found = true;
        } else {
            ESP_RETURN_ON_ERROR(audio_music_player_skip_bytes(http,
                                                              scratch,
                                                              scratch_size,
                                                              chunk_size),
                                TAG, "skip WAV chunk failed");
        }

        if ((chunk_size & 1U) != 0 && !data_found) {
            ESP_RETURN_ON_ERROR(audio_music_player_skip_bytes(http, scratch, scratch_size, 1),
                                TAG, "skip WAV padding failed");
        }
    }

    if (info->audio_format != 1 ||
        (info->channels != 1 && info->channels != 2) ||
        info->sample_rate == 0 ||
        (info->bits_per_sample != 16 && info->bits_per_sample != 24) ||
        info->block_align != info->channels * (info->bits_per_sample / 8)) {
        ESP_LOGW(TAG, "Unsupported WAV: format=%u rate=%lu bits=%u channels=%u align=%u",
                 (unsigned int)info->audio_format,
                 (unsigned long)info->sample_rate,
                 (unsigned int)info->bits_per_sample,
                 (unsigned int)info->channels,
                 (unsigned int)info->block_align);
        return ESP_ERR_NOT_SUPPORTED;
    }
    return ESP_OK;
}

static int16_t audio_music_player_sample_to_s16(const uint8_t *sample,
                                                uint16_t bits_per_sample)
{
    if (bits_per_sample == 24) {
        int32_t value = ((int32_t)sample[0]) |
                        ((int32_t)sample[1] << 8) |
                        ((int32_t)sample[2] << 16);
        if ((value & 0x00800000) != 0) {
            value |= (int32_t)0xFF000000;
        }
        return (int16_t)(value >> 8);
    }

    return (int16_t)((uint16_t)sample[0] | ((uint16_t)sample[1] << 8));
}

static int16_t audio_music_player_mono_sample(const uint8_t *pcm,
                                              size_t frame_index,
                                              const audio_music_resample_t *resample)
{
    size_t bytes_per_sample = resample->bits_per_sample / 8;
    const uint8_t *frame = pcm + frame_index * resample->channels * bytes_per_sample;
    if (resample->channels == 1) {
        return audio_music_player_sample_to_s16(frame, resample->bits_per_sample);
    }

    int32_t left = audio_music_player_sample_to_s16(frame, resample->bits_per_sample);
    int32_t right = audio_music_player_sample_to_s16(frame + bytes_per_sample,
                                                     resample->bits_per_sample);
    return (int16_t)((left + right) / 2);
}

static esp_err_t audio_music_player_write_wav_pcm(audio_music_resample_t *resample,
                                                  const uint8_t *pcm,
                                                  size_t bytes)
{
    if (resample == NULL || pcm == NULL || bytes == 0) {
        return ESP_OK;
    }

    const uint32_t output_rate = CONFIG_SMART_SPEAKER_AUDIO_OUTPUT_SAMPLE_RATE_HZ;
    size_t frame_bytes = resample->channels * (resample->bits_per_sample / 8);
    size_t source_frames = frame_bytes > 0 ? bytes / frame_bytes : 0;
    int16_t out[AUDIO_MUSIC_PLAYER_PCM_CHUNK] = { 0 };
    size_t out_count = 0;

    for (size_t i = 0; i < source_frames; ++i) {
        int16_t mono = audio_music_player_mono_sample(pcm, i, resample);
        resample->acc += output_rate;
        while (resample->acc >= resample->input_rate) {
            resample->acc -= resample->input_rate;
            out[out_count++] = mono;
            if (out_count == AUDIO_MUSIC_PLAYER_PCM_CHUNK) {
                ESP_RETURN_ON_ERROR(audio_output_write_pcm(out, out_count),
                                    TAG, "write decoded WAV PCM failed");
                out_count = 0;
            }
        }
    }

    if (out_count > 0) {
        ESP_RETURN_ON_ERROR(audio_output_write_pcm(out, out_count),
                            TAG, "write decoded WAV PCM tail failed");
    }
    return ESP_OK;
}

static esp_err_t audio_music_player_open_http(esp_http_client_handle_t http)
{
    esp_err_t ret = ESP_FAIL;
    for (int attempt = 0; attempt <= AUDIO_MUSIC_PLAYER_OPEN_RETRIES; ++attempt) {
        ret = esp_http_client_open(http, 0);
        if (ret == ESP_OK) {
            return ESP_OK;
        }
        ESP_LOGW(TAG, "retry open WAV URL: attempt=%d ret=%s",
                 attempt + 1,
                 esp_err_to_name(ret));
        esp_http_client_close(http);
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    return ret;
}

static esp_err_t audio_music_player_play_http_wav(const char *url)
{
    esp_http_client_config_t http_cfg = {
        .url = url,
        .timeout_ms = AUDIO_MUSIC_PLAYER_TIMEOUT_MS,
        .buffer_size = AUDIO_MUSIC_PLAYER_HTTP_BUFFER_BYTES,
    };
    esp_http_client_handle_t http = esp_http_client_init(&http_cfg);
    if (http == NULL) {
        return ESP_ERR_NO_MEM;
    }

    uint8_t buffer[AUDIO_MUSIC_PLAYER_HTTP_BUFFER_BYTES] = { 0 };
    audio_music_wav_info_t info = { 0 };
    esp_err_t ret = ESP_OK;
    bool output_started = false;
    uint32_t bytes_remaining = 0;
    uint32_t played_bytes = 0;

    do {
        ESP_GOTO_ON_ERROR(audio_music_player_open_http(http), fail, TAG, "open WAV URL failed");
        int status = esp_http_client_fetch_headers(http);
        if (status < 0) {
            ESP_LOGW(TAG, "fetch WAV headers failed: %d", status);
        }
        int status_code = esp_http_client_get_status_code(http);
        if (status_code != 200) {
            ESP_LOGW(TAG, "WAV HTTP status=%d", status_code);
            ret = ESP_FAIL;
            break;
        }

        ret = audio_music_player_read_wav_header(http, buffer, sizeof(buffer), &info);
        if (ret != ESP_OK) {
            break;
        }
        ESP_LOGI(TAG, "HTTP WAV info: sample_rate=%lu bits=%u channels=%u data=%lu",
                 (unsigned long)info.sample_rate,
                 (unsigned int)info.bits_per_sample,
                 (unsigned int)info.channels,
                 (unsigned long)info.data_size);

        ret = audio_output_start();
        if (ret != ESP_OK) {
            break;
        }
        output_started = true;

        audio_music_resample_t resample = {
            .input_rate = info.sample_rate,
            .channels = (uint8_t)info.channels,
            .bits_per_sample = info.bits_per_sample,
            .acc = 0,
        };
        bytes_remaining = info.data_size;
        while (!s_wav_stop_requested && bytes_remaining > 0) {
            size_t bytes_to_read = bytes_remaining > sizeof(buffer) ? sizeof(buffer) : bytes_remaining;
            bytes_to_read -= bytes_to_read % info.block_align;
            if (bytes_to_read == 0) {
                break;
            }

            ret = audio_music_player_read_exact(http, buffer, bytes_to_read);
            if (ret != ESP_OK) {
                break;
            }
            ret = audio_music_player_write_wav_pcm(&resample, buffer, bytes_to_read);
            if (ret != ESP_OK) {
                break;
            }
            bytes_remaining -= (uint32_t)bytes_to_read;
            played_bytes += (uint32_t)bytes_to_read;
        }

        if (s_wav_stop_requested && ret != ESP_OK) {
            ESP_LOGI(TAG, "HTTP WAV playback stopped by user: read=%s decoded=%lu url=%s",
                     esp_err_to_name(ret),
                     (unsigned long)played_bytes,
                     url);
            ret = ESP_OK;
        }

        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "HTTP WAV playback %s: decoded=%lu url=%s",
                     s_wav_stop_requested ? "stopped" : "finished",
                     (unsigned long)played_bytes,
                     url);
        }
    } while (0);

fail:
    if (output_started) {
        esp_err_t stop_ret = audio_output_stop();
        if (ret == ESP_OK) {
            ret = stop_ret;
        }
    }
    esp_http_client_close(http);
    esp_http_client_cleanup(http);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "HTTP WAV playback failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

static void audio_music_player_wav_task(void *arg)
{
    (void)arg;

    while (true) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        char url[AUDIO_MUSIC_PLAYER_URL_SIZE] = { 0 };
        strlcpy(url, s_wav_url, sizeof(url));

        audio_music_player_post_event(APP_EVENT_PLAYBACK_STARTED);
        esp_err_t ret = audio_music_player_play_http_wav(url);
        if (ret == ESP_OK) {
            audio_music_player_post_event(APP_EVENT_PLAYBACK_STOPPED);
        } else {
            audio_music_player_post_event(APP_EVENT_PLAYBACK_FAILED);
        }

        s_wav_stop_requested = false;
        s_wav_playing = false;
    }
}

esp_err_t audio_music_player_init(QueueHandle_t event_queue)
{
    ESP_RETURN_ON_ERROR(audio_http_player_init(event_queue), TAG, "init HTTP MP3 player failed");

    if (s_wav_task != NULL) {
        return ESP_OK;
    }

    s_event_queue = event_queue;
    BaseType_t ok = xTaskCreate(audio_music_player_wav_task,
                                "http_wav_player",
                                AUDIO_MUSIC_PLAYER_TASK_STACK,
                                NULL,
                                AUDIO_MUSIC_PLAYER_TASK_PRIORITY,
                                &s_wav_task);
    if (ok != pdPASS) {
        s_wav_task = NULL;
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "HTTP music player ready");
    return ESP_OK;
}

esp_err_t audio_music_player_play_url_async(const char *url)
{
    if (url == NULL || strlen(url) == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (audio_music_player_is_playing()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (audio_output_is_active()) {
        ESP_LOGW(TAG, "Audio output is busy, music playback blocked");
        return ESP_ERR_INVALID_STATE;
    }

    if (audio_music_player_is_mp3_url(url)) {
        return audio_http_player_play_url_async(url);
    }
    if (!audio_music_player_is_wav_url(url)) {
        ESP_LOGW(TAG, "Unsupported music URL: %s", url);
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (s_wav_task == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (strlcpy(s_wav_url, url, sizeof(s_wav_url)) >= sizeof(s_wav_url)) {
        return ESP_ERR_INVALID_ARG;
    }

    s_wav_stop_requested = false;
    s_wav_playing = true;
    xTaskNotifyGive(s_wav_task);
    ESP_LOGI(TAG, "HTTP WAV playback requested: %s", url);
    return ESP_OK;
}

esp_err_t audio_music_player_stop(void)
{
    esp_err_t ret = ESP_ERR_INVALID_STATE;
    if (audio_http_player_is_playing()) {
        ret = audio_http_player_stop();
    }
    if (s_wav_playing) {
        s_wav_stop_requested = true;
        ESP_LOGI(TAG, "HTTP WAV playback stop requested");
        ret = ESP_OK;
    }
    return ret;
}

bool audio_music_player_is_playing(void)
{
    return s_wav_playing || audio_http_player_is_playing();
}
