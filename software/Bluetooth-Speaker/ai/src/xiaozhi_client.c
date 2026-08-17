#include "xiaozhi_client.h"

#include <string.h>
#include "esp_log.h"
#include "sdkconfig.h"
#include "xiaozhi_protocol.h"

#ifdef CONFIG_SMART_SPEAKER_XIAOZHI_ENABLE
#define XIAOZHI_ENABLED true
#else
#define XIAOZHI_ENABLED false
#endif

#ifdef CONFIG_SMART_SPEAKER_XIAOZHI_AUTO_START
#define XIAOZHI_AUTO_START true
#else
#define XIAOZHI_AUTO_START false
#endif

static const char *TAG = "xiaozhi_client";

esp_err_t xiaozhi_client_init(QueueHandle_t event_queue)
{
    esp_err_t ret = xiaozhi_protocol_init(event_queue);
    if (ret != ESP_OK) {
        return ret;
    }

    ESP_LOGI(TAG, "XiaoZhi client ready, enabled=%s auto=%s ota=%s direct=%s",
             XIAOZHI_ENABLED ? "on" : "off",
             XIAOZHI_AUTO_START ? "on" : "off",
             CONFIG_SMART_SPEAKER_XIAOZHI_OTA_URL,
             strlen(CONFIG_SMART_SPEAKER_XIAOZHI_DIRECT_WS_URL) > 0 ? "set" : "empty");
    return ESP_OK;
}

esp_err_t xiaozhi_client_start(void)
{
    return xiaozhi_protocol_open_audio_channel();
}

esp_err_t xiaozhi_client_stop(void)
{
    return xiaozhi_protocol_close_audio_channel();
}

esp_err_t xiaozhi_client_start_listening(void)
{
    return xiaozhi_protocol_start_listening();
}

esp_err_t xiaozhi_client_stop_listening(void)
{
    return xiaozhi_protocol_stop_listening();
}

esp_err_t xiaozhi_client_interrupt_tts(void)
{
    return xiaozhi_protocol_interrupt_tts();
}

esp_err_t xiaozhi_client_send_audio(const uint8_t *data, size_t len)
{
    return xiaozhi_protocol_send_audio(data, len);
}

bool xiaozhi_client_is_enabled(void)
{
    return XIAOZHI_ENABLED;
}

bool xiaozhi_client_is_connected(void)
{
    return xiaozhi_protocol_is_audio_channel_open();
}

bool xiaozhi_client_is_listening(void)
{
    return xiaozhi_protocol_is_listening();
}
