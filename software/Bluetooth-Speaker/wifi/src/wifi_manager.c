#include "wifi_manager.h"

#include <inttypes.h>
#include <string.h>
#include "app_events.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "protocomm_ble.h"
#include "protocomm_security.h"
#include "wifi_provisioning/manager.h"
#include "wifi_provisioning/scheme_ble.h"

#define WIFI_MANAGER_NVS_NAMESPACE "wifi"
#define WIFI_MANAGER_NVS_KEY_SSID "ssid"
#define WIFI_MANAGER_NVS_KEY_PASSWORD "password"

typedef struct {
    char ssid[WIFI_MANAGER_MAX_SSID_LEN + 1];
    char password[WIFI_MANAGER_MAX_PASSWORD_LEN + 1];
} wifi_creds_t;

static const char *TAG = "wifi_manager";
static QueueHandle_t s_event_queue;
static bool s_connected;
static bool s_nvs_ready;
static bool s_wifi_ready;
static bool s_wifi_started;
static bool s_provisioning_active;

static void wifi_manager_post_event(app_event_type_t type)
{
    if (s_event_queue == NULL) {
        return;
    }

    app_event_t event = {
        .type = type,
        .gesture = APP_GESTURE_NONE,
        .button = APP_BUTTON_NONE,
        .message = NULL,
    };
    xQueueSend(s_event_queue, &event, 0);
}

static void wifi_manager_disable_power_save(void)
{
    esp_err_t ret = esp_wifi_set_ps(WIFI_PS_NONE);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Wi-Fi power save disabled for local audio streaming");
    } else {
        ESP_LOGW(TAG, "Disable Wi-Fi power save failed: %s", esp_err_to_name(ret));
    }
}

static esp_err_t wifi_manager_init_nvs(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition needs erase before init: %s", esp_err_to_name(ret));
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "erase NVS failed");
        ret = nvs_flash_init();
    }
    ESP_RETURN_ON_ERROR(ret, TAG, "init NVS failed");

    s_nvs_ready = true;
    return ESP_OK;
}

static void wifi_manager_wifi_event_handler(void *arg,
                                            esp_event_base_t event_base,
                                            int32_t event_id,
                                            void *event_data)
{
    (void)arg;
    (void)event_data;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "Wi-Fi STA started");
        wifi_manager_disable_power_save();
        esp_err_t ret = esp_wifi_connect();
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Wi-Fi connect failed: %s", esp_err_to_name(ret));
            wifi_manager_post_event(APP_EVENT_WIFI_FAILED);
        }
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_connected = false;
        ESP_LOGW(TAG, "Wi-Fi disconnected");
        wifi_manager_post_event(APP_EVENT_WIFI_DISCONNECTED);
        if (!s_provisioning_active) {
            esp_err_t ret = esp_wifi_connect();
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "Wi-Fi reconnect failed: %s", esp_err_to_name(ret));
            }
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        s_connected = true;
        ESP_LOGI(TAG, "Wi-Fi connected, ip=" IPSTR, IP2STR(&event->ip_info.ip));
        wifi_manager_post_event(APP_EVENT_WIFI_CONNECTED);
    }
}

static void wifi_manager_prov_event_handler(void *user_data,
                                            wifi_prov_cb_event_t event,
                                            void *event_data)
{
    (void)user_data;

    switch (event) {
    case WIFI_PROV_CRED_RECV: {
        wifi_sta_config_t *sta = (wifi_sta_config_t *)event_data;
        ESP_LOGI(TAG, "BLE provisioning received credentials: ssid=%s", (char *)sta->ssid);
        esp_err_t ret = wifi_manager_save_credentials((const char *)sta->ssid,
                                                      (const char *)sta->password);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Save provisioned credentials failed: %s", esp_err_to_name(ret));
        }
        wifi_manager_post_event(APP_EVENT_WIFI_CONNECTING);
        break;
    }
    case WIFI_PROV_CRED_SUCCESS:
        ESP_LOGI(TAG, "BLE provisioning credentials connected");
        s_provisioning_active = false;
        s_connected = true;
        wifi_manager_post_event(APP_EVENT_WIFI_CONNECTED);
        break;
    case WIFI_PROV_CRED_FAIL:
        ESP_LOGW(TAG, "BLE provisioning credentials failed");
        s_connected = false;
        wifi_manager_post_event(APP_EVENT_WIFI_FAILED);
        break;
    case WIFI_PROV_END:
        ESP_LOGI(TAG, "BLE provisioning ended");
        wifi_prov_mgr_deinit();
        s_provisioning_active = false;
        break;
    default:
        break;
    }
}

static void wifi_manager_protocomm_event_handler(void *arg,
                                                 esp_event_base_t event_base,
                                                 int32_t event_id,
                                                 void *event_data)
{
    (void)arg;
    (void)event_data;

    if (event_base == PROTOCOMM_TRANSPORT_BLE_EVENT) {
        switch (event_id) {
        case PROTOCOMM_TRANSPORT_BLE_CONNECTED:
            ESP_LOGI(TAG, "BLE transport connected");
            break;
        case PROTOCOMM_TRANSPORT_BLE_DISCONNECTED:
            ESP_LOGI(TAG, "BLE transport disconnected");
            break;
        default:
            ESP_LOGI(TAG, "BLE transport event: %" PRIi32, event_id);
            break;
        }
    } else if (event_base == PROTOCOMM_SECURITY_SESSION_EVENT) {
        switch (event_id) {
        case PROTOCOMM_SECURITY_SESSION_SETUP_OK:
            ESP_LOGI(TAG, "BLE provisioning secure session established");
            break;
        case PROTOCOMM_SECURITY_SESSION_INVALID_SECURITY_PARAMS:
            ESP_LOGW(TAG, "BLE provisioning invalid security parameters");
            break;
        case PROTOCOMM_SECURITY_SESSION_CREDENTIALS_MISMATCH:
            ESP_LOGW(TAG, "BLE provisioning POP mismatch");
            break;
        default:
            ESP_LOGI(TAG, "BLE provisioning security event: %" PRIi32, event_id);
            break;
        }
    }
}

static esp_err_t wifi_manager_init_wifi_stack(void)
{
    if (s_wifi_ready) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "init esp-netif failed");

    esp_err_t ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "create default event loop failed: %s", esp_err_to_name(ret));
        return ret;
    }

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg), TAG, "init Wi-Fi failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT,
                                                   ESP_EVENT_ANY_ID,
                                                   wifi_manager_wifi_event_handler,
                                                   NULL),
                        TAG,
                        "register Wi-Fi event handler failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT,
                                                   IP_EVENT_STA_GOT_IP,
                                                   wifi_manager_wifi_event_handler,
                                                   NULL),
                        TAG,
                        "register IP event handler failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(PROTOCOMM_TRANSPORT_BLE_EVENT,
                                                   ESP_EVENT_ANY_ID,
                                                   wifi_manager_protocomm_event_handler,
                                                   NULL),
                        TAG,
                        "register BLE transport event handler failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(PROTOCOMM_SECURITY_SESSION_EVENT,
                                                   ESP_EVENT_ANY_ID,
                                                   wifi_manager_protocomm_event_handler,
                                                   NULL),
                        TAG,
                        "register BLE security event handler failed");

    s_wifi_ready = true;
    return ESP_OK;
}

static esp_err_t wifi_manager_start_sta(const wifi_creds_t *creds)
{
    if (creds == NULL || creds->ssid[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    wifi_config_t wifi_config = { 0 };
    strlcpy((char *)wifi_config.sta.ssid, creds->ssid, sizeof(wifi_config.sta.ssid));
    strlcpy((char *)wifi_config.sta.password, creds->password, sizeof(wifi_config.sta.password));

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "set Wi-Fi STA mode failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &wifi_config), TAG, "set Wi-Fi config failed");

    esp_err_t ret = ESP_OK;
    if (!s_wifi_started) {
        ret = esp_wifi_start();
        if (ret == ESP_OK) {
            s_wifi_started = true;
        }
    } else {
        ret = esp_wifi_connect();
    }

    if (ret == ESP_OK) {
        wifi_manager_post_event(APP_EVENT_WIFI_CONNECTING);
    }
    return ret;
}

static esp_err_t wifi_manager_init_provisioning(void)
{
    wifi_prov_mgr_config_t config = {
        .scheme = wifi_prov_scheme_ble,
        .scheme_event_handler = WIFI_PROV_SCHEME_BLE_EVENT_HANDLER_FREE_BLE,
        .app_event_handler = {
            .event_cb = wifi_manager_prov_event_handler,
            .user_data = NULL,
        },
    };

    return wifi_prov_mgr_init(config);
}

esp_err_t wifi_manager_init(QueueHandle_t event_queue)
{
    if (event_queue == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    s_event_queue = event_queue;
    s_connected = false;
    ESP_RETURN_ON_ERROR(wifi_manager_init_nvs(), TAG, "init NVS storage failed");
    ESP_RETURN_ON_ERROR(wifi_manager_init_wifi_stack(), TAG, "init Wi-Fi stack failed");
    ESP_LOGI(TAG, "Wi-Fi manager ready");
    return ESP_OK;
}

esp_err_t wifi_manager_start_auto_connect(void)
{
    if (s_event_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    wifi_creds_t creds = { 0 };
    esp_err_t ret = wifi_manager_load_credentials(creds.ssid, sizeof(creds.ssid),
                                                  creds.password, sizeof(creds.password));
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Wi-Fi credentials loaded from NVS: ssid=%s", creds.ssid);
        return wifi_manager_start_sta(&creds);
    }
    if (ret != ESP_ERR_NOT_FOUND) {
        ESP_LOGW(TAG, "Wi-Fi credential lookup failed: %s", esp_err_to_name(ret));
    }

    ESP_LOGI(TAG, "No Wi-Fi credentials saved, entering provisioning");
    return wifi_manager_start_provisioning();
}

esp_err_t wifi_manager_start_provisioning(void)
{
    if (s_event_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_RETURN_ON_ERROR(wifi_manager_init_provisioning(), TAG, "init BLE provisioning failed");

    uint8_t custom_service_uuid[] = {
        0xb4, 0xdf, 0x5a, 0x1c, 0x3f, 0x6b, 0xf4, 0xbf,
        0xea, 0x4a, 0x82, 0x03, 0x04, 0x90, 0x1a, 0x02,
    };
    ESP_RETURN_ON_ERROR(wifi_prov_scheme_ble_set_service_uuid(custom_service_uuid),
                        TAG,
                        "set BLE provisioning UUID failed");

    const char *pop = WIFI_MANAGER_PROV_POP;
    ESP_RETURN_ON_ERROR(wifi_prov_mgr_start_provisioning(WIFI_PROV_SECURITY_1,
                                                         (const void *)pop,
                                                         WIFI_MANAGER_PROV_SERVICE_NAME,
                                                         NULL),
                        TAG,
                        "start BLE provisioning failed");

    s_connected = false;
    s_provisioning_active = true;
    ESP_LOGI(TAG, "BLE provisioning started: service=%s", WIFI_MANAGER_PROV_SERVICE_NAME);
    wifi_manager_post_event(APP_EVENT_WIFI_PROVISIONING);
    return ESP_OK;
}

esp_err_t wifi_manager_clear_credentials(void)
{
    if (!s_nvs_ready) {
        return ESP_ERR_INVALID_STATE;
    }

    nvs_handle_t handle;
    esp_err_t ret = nvs_open(WIFI_MANAGER_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        return ret;
    }

    esp_err_t ssid_ret = nvs_erase_key(handle, WIFI_MANAGER_NVS_KEY_SSID);
    esp_err_t pass_ret = nvs_erase_key(handle, WIFI_MANAGER_NVS_KEY_PASSWORD);
    ret = nvs_commit(handle);
    nvs_close(handle);

    if (ssid_ret != ESP_OK && ssid_ret != ESP_ERR_NVS_NOT_FOUND) {
        return ssid_ret;
    }
    if (pass_ret != ESP_OK && pass_ret != ESP_ERR_NVS_NOT_FOUND) {
        return pass_ret;
    }
    if (ret != ESP_OK) {
        return ret;
    }

    s_connected = false;
    ESP_LOGI(TAG, "Wi-Fi credentials cleared");
    wifi_manager_post_event(APP_EVENT_WIFI_PROVISIONING);
    return ESP_OK;
}

esp_err_t wifi_manager_save_credentials(const char *ssid, const char *password)
{
    if (!s_nvs_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (ssid == NULL || password == NULL || ssid[0] == '\0' ||
        strlen(ssid) > WIFI_MANAGER_MAX_SSID_LEN ||
        strlen(password) > WIFI_MANAGER_MAX_PASSWORD_LEN) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t ret = nvs_open(WIFI_MANAGER_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = nvs_set_str(handle, WIFI_MANAGER_NVS_KEY_SSID, ssid);
    if (ret == ESP_OK) {
        ret = nvs_set_str(handle, WIFI_MANAGER_NVS_KEY_PASSWORD, password);
    }
    if (ret == ESP_OK) {
        ret = nvs_commit(handle);
    }
    nvs_close(handle);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Wi-Fi credentials saved to NVS: ssid=%s", ssid);
    }
    return ret;
}

esp_err_t wifi_manager_load_credentials(char *ssid,
                                        size_t ssid_size,
                                        char *password,
                                        size_t password_size)
{
    if (!s_nvs_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (ssid == NULL || password == NULL ||
        ssid_size == 0 || password_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t ret = nvs_open(WIFI_MANAGER_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (ret != ESP_OK) {
        return ret == ESP_ERR_NVS_NOT_FOUND ? ESP_ERR_NOT_FOUND : ret;
    }

    size_t ssid_len = ssid_size;
    ret = nvs_get_str(handle, WIFI_MANAGER_NVS_KEY_SSID, ssid, &ssid_len);
    if (ret == ESP_OK) {
        size_t password_len = password_size;
        ret = nvs_get_str(handle, WIFI_MANAGER_NVS_KEY_PASSWORD, password, &password_len);
    }
    nvs_close(handle);

    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_ERR_NOT_FOUND;
    }
    return ret;
}

bool wifi_manager_is_connected(void)
{
    return s_connected;
}
