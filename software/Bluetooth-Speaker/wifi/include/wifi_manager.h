#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WIFI_MANAGER_MAX_SSID_LEN 32
#define WIFI_MANAGER_MAX_PASSWORD_LEN 64
#define WIFI_MANAGER_PROV_SERVICE_NAME CONFIG_SMART_SPEAKER_WIFI_PROV_SERVICE_NAME
#define WIFI_MANAGER_PROV_POP CONFIG_SMART_SPEAKER_WIFI_PROV_POP

esp_err_t wifi_manager_init(QueueHandle_t event_queue);
esp_err_t wifi_manager_start_auto_connect(void);
esp_err_t wifi_manager_start_provisioning(void);
esp_err_t wifi_manager_clear_credentials(void);
esp_err_t wifi_manager_save_credentials(const char *ssid, const char *password);
esp_err_t wifi_manager_load_credentials(char *ssid,
                                        size_t ssid_size,
                                        char *password,
                                        size_t password_size);
bool wifi_manager_is_connected(void);

#ifdef __cplusplus
}
#endif
