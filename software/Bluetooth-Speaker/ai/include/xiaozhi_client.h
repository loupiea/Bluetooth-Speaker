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

esp_err_t xiaozhi_client_init(QueueHandle_t event_queue);
esp_err_t xiaozhi_client_start(void);
esp_err_t xiaozhi_client_stop(void);
esp_err_t xiaozhi_client_start_listening(void);
esp_err_t xiaozhi_client_stop_listening(void);
esp_err_t xiaozhi_client_interrupt_tts(void);
esp_err_t xiaozhi_client_send_audio(const uint8_t *data, size_t len);
bool xiaozhi_client_is_enabled(void);
bool xiaozhi_client_is_connected(void);
bool xiaozhi_client_is_listening(void);

#ifdef __cplusplus
}
#endif
