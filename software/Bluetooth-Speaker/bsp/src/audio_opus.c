#include "audio_opus.h"

#include <stdbool.h>
#include "esp_audio_enc.h"
#include "esp_audio_types.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_opus_enc.h"

static const char *TAG = "audio_opus";

static void *s_encoder;
static int s_encoder_frame_bytes;
static int s_encoder_outbuf_size;

esp_err_t audio_opus_init(void)
{
    if (s_encoder != NULL) {
        return ESP_OK;
    }

    esp_opus_enc_config_t config = {
        .sample_rate = ESP_AUDIO_SAMPLE_RATE_16K,
        .channel = ESP_AUDIO_MONO,
        .bits_per_sample = ESP_AUDIO_BIT16,
        .bitrate = ESP_OPUS_BITRATE_AUTO,
        .frame_duration = ESP_OPUS_ENC_FRAME_DURATION_60_MS,
        .application_mode = ESP_OPUS_ENC_APPLICATION_AUDIO,
        .complexity = 0,
        .enable_fec = false,
        .enable_dtx = true,
        .enable_vbr = true,
    };

    int ret = esp_opus_enc_open(&config, sizeof(config), &s_encoder);
    if (ret != ESP_AUDIO_ERR_OK || s_encoder == NULL) {
        ESP_LOGE(TAG, "Opus encoder open failed: %d", ret);
        s_encoder = NULL;
        return ESP_FAIL;
    }

    ret = esp_opus_enc_get_frame_size(s_encoder,
                                      &s_encoder_frame_bytes,
                                      &s_encoder_outbuf_size);
    if (ret != ESP_AUDIO_ERR_OK) {
        ESP_LOGE(TAG, "Opus frame size query failed: %d", ret);
        esp_opus_enc_close(s_encoder);
        s_encoder = NULL;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Opus encoder ready: sample_rate=%d frame=%d ms pcm=%d bytes out=%d bytes",
             AUDIO_OPUS_SAMPLE_RATE_HZ,
             AUDIO_OPUS_FRAME_DURATION_MS,
             s_encoder_frame_bytes,
             s_encoder_outbuf_size);
    return ESP_OK;
}

void audio_opus_deinit(void)
{
    if (s_encoder == NULL) {
        return;
    }

    esp_opus_enc_close(s_encoder);
    s_encoder = NULL;
    s_encoder_frame_bytes = 0;
    s_encoder_outbuf_size = 0;
    ESP_LOGI(TAG, "Opus encoder released");
}

esp_err_t audio_opus_encode_frame(const int16_t *pcm,
                                  size_t sample_count,
                                  uint8_t *opus,
                                  size_t opus_size,
                                  size_t *encoded_bytes)
{
    if (pcm == NULL || opus == NULL || encoded_bytes == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (sample_count != AUDIO_OPUS_FRAME_SAMPLES) {
        return ESP_ERR_INVALID_SIZE;
    }

    ESP_RETURN_ON_ERROR(audio_opus_init(), TAG, "Opus init failed");

    if (opus_size < (size_t)s_encoder_outbuf_size) {
        return ESP_ERR_NO_MEM;
    }

    esp_audio_enc_in_frame_t in = {
        .buffer = (uint8_t *)pcm,
        .len = (uint32_t)(sample_count * sizeof(pcm[0])),
    };
    esp_audio_enc_out_frame_t out = {
        .buffer = opus,
        .len = (uint32_t)opus_size,
        .encoded_bytes = 0,
    };

    int ret = esp_opus_enc_process(s_encoder, &in, &out);
    if (ret != ESP_AUDIO_ERR_OK) {
        ESP_LOGW(TAG, "Opus encode failed: %d", ret);
        return ESP_FAIL;
    }

    *encoded_bytes = out.encoded_bytes;
    return ESP_OK;
}

size_t audio_opus_get_out_buffer_size(void)
{
    if (audio_opus_init() != ESP_OK || s_encoder_outbuf_size <= 0) {
        return 0;
    }
    return (size_t)s_encoder_outbuf_size;
}
