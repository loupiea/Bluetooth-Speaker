#include "audio_output.h"

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#define AUDIO_OUTPUT_TONE_FRAMES 128
#define AUDIO_OUTPUT_DMA_FRAMES 256
#define AUDIO_OUTPUT_PCM_FRAMES 512
#define AUDIO_OUTPUT_SILENCE_FRAMES 512
#define AUDIO_OUTPUT_WRITE_TIMEOUT_MS 1000
#define AUDIO_OUTPUT_VOLUME_STEP 10
#define AUDIO_OUTPUT_MAX_AMPLITUDE 12000

static const char *TAG = "audio_output";
static SemaphoreHandle_t s_output_mutex;
static i2s_chan_handle_t s_tx_chan;
static bool s_tx_enabled;
static uint32_t s_tx_users;
static TaskHandle_t s_tx_owner;
static uint8_t s_volume = CONFIG_SMART_SPEAKER_AUDIO_OUTPUT_DEFAULT_VOLUME;
static int16_t s_pcm_frames[AUDIO_OUTPUT_PCM_FRAMES * 2];
static int16_t s_silence_frames[AUDIO_OUTPUT_SILENCE_FRAMES * 2];

static bool audio_output_lock(TickType_t ticks_to_wait)
{
    return s_output_mutex != NULL &&
           xSemaphoreTake(s_output_mutex, ticks_to_wait) == pdTRUE;
}

static void audio_output_unlock(void)
{
    if (s_output_mutex != NULL) {
        xSemaphoreGive(s_output_mutex);
    }
}

static int16_t audio_output_tone_sample(uint32_t sample_index, uint32_t tone_hz)
{
    const uint32_t sample_rate = CONFIG_SMART_SPEAKER_AUDIO_OUTPUT_SAMPLE_RATE_HZ;
    if (tone_hz == 0) {
        tone_hz = CONFIG_SMART_SPEAKER_AUDIO_OUTPUT_TEST_TONE_HZ;
    }
    uint32_t half_period = sample_rate / (tone_hz * 2U);
    if (half_period == 0) {
        half_period = 1;
    }

    int32_t amplitude = AUDIO_OUTPUT_MAX_AMPLITUDE;
    return ((sample_index / half_period) % 2U) == 0 ? (int16_t)amplitude : (int16_t)-amplitude;
}

esp_err_t audio_output_init(void)
{
    if (s_tx_chan != NULL) {
        return ESP_OK;
    }
    if (s_output_mutex == NULL) {
        s_output_mutex = xSemaphoreCreateMutex();
        if (s_output_mutex == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    chan_cfg.dma_desc_num = 8;
    chan_cfg.dma_frame_num = AUDIO_OUTPUT_DMA_FRAMES;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, &s_tx_chan, NULL),
                        TAG, "create I2S TX channel failed");

    i2s_std_slot_config_t slot_cfg =
        I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(CONFIG_SMART_SPEAKER_AUDIO_OUTPUT_SAMPLE_RATE_HZ),
        .slot_cfg = slot_cfg,
        .gpio_cfg = {
            .mclk = GPIO_NUM_NC,
            .bclk = CONFIG_SMART_SPEAKER_MAX98357A_BCLK_GPIO,
            .ws = CONFIG_SMART_SPEAKER_MAX98357A_LRCLK_GPIO,
            .dout = CONFIG_SMART_SPEAKER_MAX98357A_DOUT_GPIO,
            .din = GPIO_NUM_NC,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_tx_chan, &std_cfg),
                        TAG, "init I2S standard TX failed");

    ESP_LOGI(TAG, "MAX98357A ready: i2s=1 BCLK=%d LRCLK=%d DOUT=%d sample_rate=%d Hz volume=%u",
             CONFIG_SMART_SPEAKER_MAX98357A_BCLK_GPIO,
             CONFIG_SMART_SPEAKER_MAX98357A_LRCLK_GPIO,
             CONFIG_SMART_SPEAKER_MAX98357A_DOUT_GPIO,
             CONFIG_SMART_SPEAKER_AUDIO_OUTPUT_SAMPLE_RATE_HZ,
             (unsigned int)s_volume);
    return ESP_OK;
}

esp_err_t audio_output_start(void)
{
    if (s_tx_chan == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!audio_output_lock(pdMS_TO_TICKS(500))) {
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t ret = ESP_OK;
    TaskHandle_t current_task = xTaskGetCurrentTaskHandle();
    if (s_tx_enabled) {
        if (s_tx_owner != current_task) {
            ret = ESP_ERR_INVALID_STATE;
        }
    } else {
        ret = i2s_channel_enable(s_tx_chan);
        if (ret == ESP_OK) {
            s_tx_enabled = true;
            s_tx_owner = current_task;
        }
    }
    if (ret == ESP_OK) {
        s_tx_users++;
    }
    audio_output_unlock();
    ESP_RETURN_ON_ERROR(ret, TAG, "enable I2S TX failed");
    return ret;
}

static void audio_output_reset_tx_locked(const char *reason)
{
    ESP_LOGW(TAG, "Reset I2S TX after %s: users=%lu enabled=%d",
             reason != NULL ? reason : "error",
             (unsigned long)s_tx_users,
             s_tx_enabled ? 1 : 0);
    if (s_tx_enabled) {
        esp_err_t ret = i2s_channel_disable(s_tx_chan);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "disable I2S TX during reset failed: %s", esp_err_to_name(ret));
        }
    }
    s_tx_enabled = false;
    s_tx_users = 0;
    s_tx_owner = NULL;
}

static esp_err_t audio_output_disable_tx_locked(void)
{
    if (!s_tx_enabled) {
        s_tx_users = 0;
        s_tx_owner = NULL;
        return ESP_OK;
    }

    esp_err_t ret = i2s_channel_disable(s_tx_chan);
    if (ret == ESP_OK || ret == ESP_ERR_INVALID_STATE) {
        s_tx_enabled = false;
        s_tx_users = 0;
        s_tx_owner = NULL;
        return ESP_OK;
    }
    return ret;
}

static esp_err_t audio_output_write_silence(void)
{
    memset(s_silence_frames, 0, sizeof(s_silence_frames));
    size_t bytes_to_write = sizeof(s_silence_frames);
    size_t bytes_written = 0;
    ESP_RETURN_ON_ERROR(i2s_channel_write(s_tx_chan, s_silence_frames, bytes_to_write,
                                          &bytes_written,
                                          pdMS_TO_TICKS(AUDIO_OUTPUT_WRITE_TIMEOUT_MS)),
                        TAG, "write silence failed");
    return bytes_written == bytes_to_write ? ESP_OK : ESP_FAIL;
}

static int16_t audio_output_scale_sample(int16_t sample)
{
    int32_t scaled = ((int32_t)sample * (int32_t)s_volume) / 100;
    if (scaled > INT16_MAX) {
        scaled = INT16_MAX;
    } else if (scaled < INT16_MIN) {
        scaled = INT16_MIN;
    }
    return (int16_t)scaled;
}

static esp_err_t audio_output_write_pcm_chunk(const int16_t *samples, size_t sample_count)
{
    size_t offset = 0;

    while (offset < sample_count) {
        size_t frames_this_round = sample_count - offset;
        if (frames_this_round > AUDIO_OUTPUT_PCM_FRAMES) {
            frames_this_round = AUDIO_OUTPUT_PCM_FRAMES;
        }

        for (size_t i = 0; i < frames_this_round; ++i) {
            int16_t sample = audio_output_scale_sample(samples[offset + i]);
            s_pcm_frames[i * 2U] = sample;
            s_pcm_frames[(i * 2U) + 1U] = sample;
        }

        size_t bytes_to_write = frames_this_round * 2U * sizeof(s_pcm_frames[0]);
        size_t bytes_written = 0;
        ESP_RETURN_ON_ERROR(i2s_channel_write(s_tx_chan, s_pcm_frames, bytes_to_write,
                                              &bytes_written,
                                              pdMS_TO_TICKS(AUDIO_OUTPUT_WRITE_TIMEOUT_MS)),
                            TAG, "write PCM failed");
        if (bytes_written != bytes_to_write) {
            return ESP_FAIL;
        }

        offset += frames_this_round;
    }

    return ESP_OK;
}

esp_err_t audio_output_write_pcm(const int16_t *samples, size_t sample_count)
{
    if (s_tx_chan == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (samples == NULL && sample_count > 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (sample_count == 0) {
        return ESP_OK;
    }
    if (!audio_output_lock(pdMS_TO_TICKS(500))) {
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t ret = ESP_OK;
    if (!s_tx_enabled) {
        ret = ESP_ERR_INVALID_STATE;
    } else if (s_tx_owner != xTaskGetCurrentTaskHandle()) {
        ESP_LOGW(TAG, "write owner mismatch");
        ret = ESP_ERR_INVALID_STATE;
    } else {
        ret = audio_output_write_pcm_chunk(samples, sample_count);
        if (ret != ESP_OK) {
            audio_output_reset_tx_locked("PCM write failure");
        }
    }

    audio_output_unlock();
    return ret;
}

esp_err_t audio_output_stop(void)
{
    if (s_tx_chan == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!audio_output_lock(pdMS_TO_TICKS(500))) {
        return ESP_ERR_TIMEOUT;
    }

    if (s_tx_enabled && s_tx_owner != xTaskGetCurrentTaskHandle()) {
        ESP_LOGW(TAG, "stop owner mismatch");
        audio_output_unlock();
        return ESP_ERR_INVALID_STATE;
    }

    if (s_tx_users > 0) {
        s_tx_users--;
    }
    if (s_tx_users > 0) {
        audio_output_unlock();
        return ESP_OK;
    }
    if (!s_tx_enabled) {
        audio_output_unlock();
        return ESP_OK;
    }

    esp_err_t ret = audio_output_write_silence();
    if (ret == ESP_OK) {
        ret = audio_output_disable_tx_locked();
    }
    if (ret != ESP_OK) {
        audio_output_reset_tx_locked("stop/disable failure");
    }
    audio_output_unlock();
    ESP_RETURN_ON_ERROR(ret, TAG, "stop I2S TX failed");
    return ESP_OK;
}

esp_err_t audio_output_play_tone(uint32_t tone_hz, uint32_t duration_ms)
{
    if (s_tx_chan == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (duration_ms == 0) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(audio_output_start(), TAG, "start I2S TX failed");

    int16_t samples[AUDIO_OUTPUT_TONE_FRAMES] = { 0 };
    uint32_t frames_remaining =
        (CONFIG_SMART_SPEAKER_AUDIO_OUTPUT_SAMPLE_RATE_HZ * duration_ms) / 1000U;
    uint32_t sample_index = 0;

    esp_err_t ret = ESP_OK;
    while (frames_remaining > 0) {
        uint32_t frames_this_round = frames_remaining;
        if (frames_this_round > AUDIO_OUTPUT_TONE_FRAMES) {
            frames_this_round = AUDIO_OUTPUT_TONE_FRAMES;
        }

        for (uint32_t i = 0; i < frames_this_round; ++i) {
            samples[i] = audio_output_tone_sample(sample_index++, tone_hz);
        }

        ret = audio_output_write_pcm(samples, frames_this_round);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "write test tone failed: %s", esp_err_to_name(ret));
            break;
        }

        frames_remaining -= frames_this_round;
    }

    esp_err_t disable_ret = audio_output_stop();
    if (ret == ESP_OK) {
        ret = disable_ret;
    }
    ESP_RETURN_ON_ERROR(ret, TAG, "finish test tone failed");

    ESP_LOGI(TAG, "Tone played: %lu Hz %lu ms volume=%u",
             (unsigned long)tone_hz, (unsigned long)duration_ms, (unsigned int)s_volume);
    return ESP_OK;
}

esp_err_t audio_output_play_test_tone(uint32_t duration_ms)
{
    return audio_output_play_tone(CONFIG_SMART_SPEAKER_AUDIO_OUTPUT_TEST_TONE_HZ,
                                  duration_ms);
}

esp_err_t audio_output_set_volume(uint8_t volume_percent)
{
    if (volume_percent > 100U) {
        volume_percent = 100U;
    }
    s_volume = volume_percent;
    ESP_LOGI(TAG, "Speaker volume: %u", (unsigned int)s_volume);
    return ESP_OK;
}

esp_err_t audio_output_volume_up(void)
{
    uint8_t volume = s_volume;
    if (volume + AUDIO_OUTPUT_VOLUME_STEP > 100U) {
        volume = 100U;
    } else {
        volume += AUDIO_OUTPUT_VOLUME_STEP;
    }
    return audio_output_set_volume(volume);
}

esp_err_t audio_output_volume_down(void)
{
    uint8_t volume = s_volume;
    if (volume < AUDIO_OUTPUT_VOLUME_STEP) {
        volume = 0U;
    } else {
        volume -= AUDIO_OUTPUT_VOLUME_STEP;
    }
    return audio_output_set_volume(volume);
}

uint8_t audio_output_get_volume(void)
{
    return s_volume;
}

bool audio_output_is_ready(void)
{
    return s_tx_chan != NULL;
}

bool audio_output_is_active(void)
{
    if (s_tx_chan == NULL) {
        return false;
    }
    if (!audio_output_lock(pdMS_TO_TICKS(20))) {
        return true;
    }

    bool active = s_tx_enabled && s_tx_users > 0;
    audio_output_unlock();
    return active;
}
