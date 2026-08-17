#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t xiaozhi_protocol_init(QueueHandle_t event_queue);
esp_err_t xiaozhi_protocol_open_audio_channel(void);
esp_err_t xiaozhi_protocol_close_audio_channel(void);
esp_err_t xiaozhi_protocol_send_text(const char *text);
esp_err_t xiaozhi_protocol_send_audio(const uint8_t *data, size_t len);
esp_err_t xiaozhi_protocol_start_listening(void);
esp_err_t xiaozhi_protocol_stop_listening(void);
esp_err_t xiaozhi_protocol_interrupt_tts(void);
bool xiaozhi_protocol_is_audio_channel_open(void);
bool xiaozhi_protocol_is_listening(void);

#ifdef __cplusplus
}
#endif
