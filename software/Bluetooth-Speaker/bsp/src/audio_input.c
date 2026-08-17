#include "audio_input.h"

#include <stdbool.h>
#include <stdint.h>
#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "esp_check.h"
#include "esp_log.h"
#include "sdkconfig.h"

#define INMP441_READ_SAMPLES 256

static const char *TAG = "audio_input";
static i2s_chan_handle_t s_rx_chan;

esp_err_t audio_input_init(void)
{
    if (s_rx_chan != NULL) {
        return ESP_OK;
    }

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = 4;
    chan_cfg.dma_frame_num = INMP441_READ_SAMPLES;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, NULL, &s_rx_chan),
                        TAG, "create I2S RX channel failed");

    i2s_std_slot_config_t slot_cfg =
        I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO);
    slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(CONFIG_SMART_SPEAKER_INMP441_SAMPLE_RATE_HZ),
        .slot_cfg = slot_cfg,
        .gpio_cfg = {
            .mclk = GPIO_NUM_NC,
            .bclk = CONFIG_SMART_SPEAKER_INMP441_BCLK_GPIO,
            .ws = CONFIG_SMART_SPEAKER_INMP441_LRCLK_GPIO,
            .dout = GPIO_NUM_NC,
            .din = CONFIG_SMART_SPEAKER_INMP441_DIN_GPIO,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_rx_chan, &std_cfg),
                        TAG, "init I2S standard RX failed");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_rx_chan), TAG, "enable I2S RX failed");

    ESP_LOGI(TAG, "INMP441 ready: i2s=0 BCLK=%d LRCLK=%d DIN=%d sample_rate=%d Hz",
             CONFIG_SMART_SPEAKER_INMP441_BCLK_GPIO,
             CONFIG_SMART_SPEAKER_INMP441_LRCLK_GPIO,
             CONFIG_SMART_SPEAKER_INMP441_DIN_GPIO,
             CONFIG_SMART_SPEAKER_INMP441_SAMPLE_RATE_HZ);
    return ESP_OK;
}

esp_err_t audio_input_read_samples(int32_t *samples,
                                   size_t sample_count,
                                   size_t *samples_read,
                                   TickType_t timeout_ticks)
{
    if (samples == NULL || sample_count == 0 || s_rx_chan == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t bytes_read = 0;
    uint32_t timeout_ms = pdTICKS_TO_MS(timeout_ticks);
    if (timeout_ticks != 0 && timeout_ms == 0) {
        timeout_ms = 1;
    }
    esp_err_t ret = i2s_channel_read(s_rx_chan, samples, sample_count * sizeof(samples[0]),
                                     &bytes_read, timeout_ms);
    if (samples_read != NULL) {
        *samples_read = bytes_read / sizeof(samples[0]);
    }
    return ret;
}
