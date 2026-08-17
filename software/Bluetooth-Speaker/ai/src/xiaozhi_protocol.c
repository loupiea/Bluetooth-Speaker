#include "xiaozhi_protocol.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "ai_music_control.h"
#include "ai_music_library.h"
#include "cJSON.h"
#include "app_events.h"
#include "esp_app_desc.h"
#include "esp_chip_info.h"
#include "esp_crt_bundle.h"
#include "esp_err.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "esp_websocket_client.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "wifi_manager.h"
#include "xiaozhi_audio_stream.h"
#include "xiaozhi_tts_player.h"

#define XIAOZHI_OPEN_TASK_STACK 8192
#define XIAOZHI_CLIENT_TASK_STACK 12288
#define XIAOZHI_CLIENT_TASK_PRIORITY 4
#define XIAOZHI_DEVICE_ID_SIZE 18
#define XIAOZHI_CLIENT_ID_SIZE 37
#define XIAOZHI_HEADER_SIZE 512
#define XIAOZHI_HELLO_SIZE 256
#define XIAOZHI_AUTH_HEADER_SIZE 300
#define XIAOZHI_URI_SIZE 512
#define XIAOZHI_TOKEN_SIZE 256
#define XIAOZHI_SESSION_ID_SIZE 64
#define XIAOZHI_MCP_RESPONSE_SIZE 2048
#define XIAOZHI_MUSIC_LIST_TEXT_SIZE 1600
#define XIAOZHI_OTA_BODY_SIZE 1536
#define XIAOZHI_OTA_RESPONSE_SIZE 2048
#define XIAOZHI_ACTIVATION_TEXT_SIZE 96
#define XIAOZHI_SEND_TEXT_TIMEOUT_MS 1000
#define XIAOZHI_SEND_AUDIO_TIMEOUT_MS 250
#define XIAOZHI_SEND_AUDIO_LOCK_TIMEOUT_MS 100
#define XIAOZHI_LOCAL_MUSIC_CLOSE_DELAY_MS 300
#define XIAOZHI_LOCAL_MUSIC_CLOSE_TASK_STACK 4096
#define XIAOZHI_BINARY_PROTOCOL2_HEADER_SIZE 16
#define XIAOZHI_BINARY_PROTOCOL3_HEADER_SIZE 4
#define XIAOZHI_AUDIO_BINARY_MAX_BYTES 768
#define XIAOZHI_BINARY_TYPE_OPUS 0

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

static const char *TAG = "xiaozhi";
static QueueHandle_t s_event_queue;
static SemaphoreHandle_t s_client_mutex;
static esp_websocket_client_handle_t s_client;
static TaskHandle_t s_open_task_handle;
static bool s_started;
static bool s_starting;
static bool s_connected;
static bool s_hello_received;
static bool s_listening;
static bool s_activation_required;
static int s_server_sample_rate;
static int s_server_frame_duration;
static char s_activation_code[XIAOZHI_ACTIVATION_TEXT_SIZE];
static char s_activation_message[XIAOZHI_ACTIVATION_TEXT_SIZE];
static char s_device_id[XIAOZHI_DEVICE_ID_SIZE];
static char s_client_id[XIAOZHI_CLIENT_ID_SIZE];
static char s_headers[XIAOZHI_HEADER_SIZE];
static char s_uri[XIAOZHI_URI_SIZE];
static char s_token[XIAOZHI_TOKEN_SIZE];
static char s_session_id[XIAOZHI_SESSION_ID_SIZE];
static int s_protocol_version = 1;
static bool s_first_audio_frame_logged;
static bool s_tts_interrupted;
static bool s_tts_drop_logged;
static bool s_local_music_active;
static bool s_websocket_stack_logged;
static uint32_t s_audio_frame_count;

static esp_err_t xiaozhi_protocol_send_listen(const char *state);
static esp_err_t xiaozhi_protocol_send_abort(const char *reason);

static bool xiaozhi_protocol_lock_client(TickType_t ticks_to_wait)
{
    return s_client_mutex != NULL &&
           xSemaphoreTake(s_client_mutex, ticks_to_wait) == pdTRUE;
}

static void xiaozhi_protocol_unlock_client(void)
{
    if (s_client_mutex != NULL) {
        xSemaphoreGive(s_client_mutex);
    }
}

static void xiaozhi_protocol_clear_tts_interrupt(const char *reason)
{
    if (s_tts_interrupted || s_tts_drop_logged) {
        ESP_LOGI(TAG, "XiaoZhi TTS interrupt cleared: %s", reason);
    }
    s_tts_interrupted = false;
    s_tts_drop_logged = false;
}

static void xiaozhi_protocol_reset_connection_state(void)
{
    s_started = false;
    s_connected = false;
    s_hello_received = false;
    s_listening = false;
}

static bool xiaozhi_protocol_connection_is_alive(void)
{
    return s_client != NULL && s_connected &&
           esp_websocket_client_is_connected(s_client);
}

static void xiaozhi_protocol_reset_stale_connection(const char *reason)
{
    if (s_client == NULL && !s_connected && !s_started && !s_hello_received && !s_listening) {
        return;
    }

    ESP_LOGW(TAG, "XiaoZhi websocket stale, reset state: %s",
             reason != NULL ? reason : "unknown");
    xiaozhi_protocol_reset_connection_state();
    xiaozhi_protocol_clear_tts_interrupt("stale websocket");
}

static esp_err_t xiaozhi_protocol_destroy_client(void)
{
    if (!xiaozhi_protocol_lock_client(pdMS_TO_TICKS(1500))) {
        ESP_LOGW(TAG, "XiaoZhi websocket destroy lock timeout");
        xiaozhi_protocol_reset_connection_state();
        return ESP_ERR_TIMEOUT;
    }

    if (s_client == NULL) {
        xiaozhi_protocol_reset_connection_state();
        xiaozhi_protocol_unlock_client();
        return ESP_OK;
    }

    esp_websocket_client_handle_t client = s_client;
    s_client = NULL;

    esp_err_t ret = ESP_OK;
    esp_err_t stop_ret = esp_websocket_client_stop(client);
    if (stop_ret != ESP_OK && stop_ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "XiaoZhi websocket stop before destroy failed: %s",
                 esp_err_to_name(stop_ret));
        ret = stop_ret;
    }

    esp_websocket_client_destroy(client);
    xiaozhi_protocol_reset_connection_state();
    xiaozhi_protocol_unlock_client();
    ESP_LOGI(TAG, "XiaoZhi websocket client destroyed");
    return ret;
}

static void xiaozhi_protocol_prepare_local_music(const char *reason)
{
    s_tts_interrupted = true;
    s_tts_drop_logged = false;
    if (xiaozhi_audio_stream_is_running()) {
        esp_err_t stream_ret = xiaozhi_audio_stream_stop();
        ESP_LOGI(TAG, "XiaoZhi audio stream stopped for local music: %s",
                 esp_err_to_name(stream_ret));
    }
    if (s_listening) {
        esp_err_t listen_ret = xiaozhi_protocol_send_listen("stop");
        if (listen_ret == ESP_OK) {
            s_listening = false;
        }
        ESP_LOGI(TAG, "XiaoZhi listen stopped for local music: %s",
                 esp_err_to_name(listen_ret));
    }
    esp_err_t abort_ret = xiaozhi_protocol_send_abort("local_music");
    if (abort_ret == ESP_OK) {
        ESP_LOGI(TAG, "XiaoZhi abort sent for local music");
    } else {
        ESP_LOGW(TAG, "XiaoZhi abort for local music failed: %s",
                 esp_err_to_name(abort_ret));
    }
    esp_err_t ret = xiaozhi_tts_player_stop();
    ESP_LOGI(TAG, "XiaoZhi TTS interrupted for local music: %s, stop=%s",
             reason != NULL ? reason : "unknown",
             esp_err_to_name(ret));
}

static void xiaozhi_protocol_close_after_local_music_task(void *arg)
{
    (void)arg;

    vTaskDelay(pdMS_TO_TICKS(XIAOZHI_LOCAL_MUSIC_CLOSE_DELAY_MS));
    esp_err_t ret = xiaozhi_protocol_close_audio_channel();
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "XiaoZhi audio channel closed after local music takeover");
    } else {
        ESP_LOGW(TAG, "XiaoZhi audio channel close after local music failed: %s",
                 esp_err_to_name(ret));
    }
    vTaskDelete(NULL);
}

static void xiaozhi_protocol_close_after_local_music(void)
{
    BaseType_t task_ret = xTaskCreate(xiaozhi_protocol_close_after_local_music_task,
                                      "xz_local_close",
                                      XIAOZHI_LOCAL_MUSIC_CLOSE_TASK_STACK,
                                      NULL,
                                      XIAOZHI_CLIENT_TASK_PRIORITY,
                                      NULL);
    if (task_ret != pdPASS) {
        ESP_LOGW(TAG, "XiaoZhi local music close task create failed");
    }
}

static bool xiaozhi_protocol_text_contains_any(const char *text,
                                               const char *const *keywords,
                                               size_t keyword_count)
{
    if (text == NULL) {
        return false;
    }

    for (size_t i = 0; i < keyword_count; ++i) {
        if (keywords[i] != NULL && strstr(text, keywords[i]) != NULL) {
            return true;
        }
    }
    return false;
}

static bool xiaozhi_protocol_try_local_music_command(const char *text)
{
    static const char *const play_keywords[] = {
        "播放",
        "放",
        "听",
        "来一首",
    };
    static const char *const list_keywords[] = {
        "哪些",
        "列表",
        "歌单",
        "有什么",
    };

    if (!xiaozhi_protocol_text_contains_any(text,
                                            play_keywords,
                                            sizeof(play_keywords) / sizeof(play_keywords[0]))) {
        return false;
    }
    if (xiaozhi_protocol_text_contains_any(text,
                                           list_keywords,
                                           sizeof(list_keywords) / sizeof(list_keywords[0]))) {
        return false;
    }
    if (ai_music_library_find(text) == NULL) {
        return false;
    }

    s_local_music_active = true;
    xiaozhi_protocol_prepare_local_music("stt local music fallback");
    esp_err_t ret = ai_music_control_play_by_name(text);
    if (ret != ESP_OK) {
        s_local_music_active = false;
    } else {
        xiaozhi_protocol_close_after_local_music();
    }
    ESP_LOGI(TAG, "XiaoZhi STT local music fallback: text=%s ret=%s",
             text,
             esp_err_to_name(ret));
    return ret == ESP_OK;
}

static void xiaozhi_protocol_handle_stt_text(const cJSON *root)
{
    cJSON *text = cJSON_GetObjectItem(root, "text");
    if (!cJSON_IsString(text)) {
        return;
    }

    (void)xiaozhi_protocol_try_local_music_command(text->valuestring);
}

static void xiaozhi_protocol_resume_audio_after_mcp_tools_list(void)
{
    s_first_audio_frame_logged = false;
    s_audio_frame_count = 0;

    esp_err_t stream_ret = xiaozhi_audio_stream_start();
    if (stream_ret != ESP_OK) {
        ESP_LOGW(TAG, "XiaoZhi audio stream resume after MCP tools/list failed: %s",
                 esp_err_to_name(stream_ret));
        return;
    }

    ESP_LOGI(TAG, "XiaoZhi audio stream resumed after MCP tools/list");
}

static void xiaozhi_write_be16(uint8_t *dst, uint16_t value)
{
    dst[0] = (uint8_t)(value >> 8);
    dst[1] = (uint8_t)value;
}

static void xiaozhi_write_be32(uint8_t *dst, uint32_t value)
{
    dst[0] = (uint8_t)(value >> 24);
    dst[1] = (uint8_t)(value >> 16);
    dst[2] = (uint8_t)(value >> 8);
    dst[3] = (uint8_t)value;
}

static uint16_t xiaozhi_read_be16(const uint8_t *src)
{
    return ((uint16_t)src[0] << 8) | src[1];
}

static uint32_t xiaozhi_read_be32(const uint8_t *src)
{
    return ((uint32_t)src[0] << 24) |
           ((uint32_t)src[1] << 16) |
           ((uint32_t)src[2] << 8) |
           src[3];
}

static esp_err_t xiaozhi_extract_audio_payload(const uint8_t *data,
                                               size_t len,
                                               const uint8_t **payload,
                                               size_t *payload_len)
{
    if (data == NULL || len == 0 || payload == NULL || payload_len == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *payload = data;
    *payload_len = len;

    if (s_protocol_version == 2) {
        if (len < XIAOZHI_BINARY_PROTOCOL2_HEADER_SIZE) {
            return ESP_ERR_INVALID_SIZE;
        }
        uint16_t type = xiaozhi_read_be16(data + 2);
        uint32_t size = xiaozhi_read_be32(data + 12);
        if (type != XIAOZHI_BINARY_TYPE_OPUS ||
            size > len - XIAOZHI_BINARY_PROTOCOL2_HEADER_SIZE) {
            return ESP_ERR_INVALID_RESPONSE;
        }
        *payload = data + XIAOZHI_BINARY_PROTOCOL2_HEADER_SIZE;
        *payload_len = size;
    } else if (s_protocol_version == 3) {
        if (len < XIAOZHI_BINARY_PROTOCOL3_HEADER_SIZE) {
            return ESP_ERR_INVALID_SIZE;
        }
        uint8_t type = data[0];
        uint16_t size = xiaozhi_read_be16(data + 2);
        if (type != XIAOZHI_BINARY_TYPE_OPUS ||
            size > len - XIAOZHI_BINARY_PROTOCOL3_HEADER_SIZE) {
            return ESP_ERR_INVALID_RESPONSE;
        }
        *payload = data + XIAOZHI_BINARY_PROTOCOL3_HEADER_SIZE;
        *payload_len = size;
    }

    return ESP_OK;
}

static void xiaozhi_post_event(app_event_type_t type, const char *message)
{
    if (s_event_queue == NULL) {
        return;
    }

    app_event_t event = {
        .type = type,
        .gesture = APP_GESTURE_NONE,
        .button = APP_BUTTON_NONE,
        .message = message,
    };
    xQueueSend(s_event_queue, &event, 0);
}

static esp_err_t xiaozhi_make_ids(void)
{
    uint8_t mac[6] = { 0 };
    esp_err_t ret = esp_read_mac(mac, ESP_MAC_WIFI_STA);
    if (ret != ESP_OK) {
        return ret;
    }

    snprintf(s_device_id, sizeof(s_device_id), "%02x:%02x:%02x:%02x:%02x:%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    snprintf(s_client_id, sizeof(s_client_id),
             "%02x%02x%02x%02x-%02x%02x-4000-8000-%02x%02x%02x%02x%02x%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return ESP_OK;
}

static esp_err_t xiaozhi_make_headers(void)
{
    if (strlen(s_token) == 0) {
        ESP_LOGW(TAG, "XiaoZhi token is empty");
        return ESP_ERR_INVALID_STATE;
    }

    char auth[XIAOZHI_AUTH_HEADER_SIZE];
    const char *prefix = "Bearer ";
    if (strncmp(s_token, prefix, strlen(prefix)) == 0) {
        int auth_len = snprintf(auth, sizeof(auth), "%s", s_token);
        if (auth_len < 0 || auth_len >= (int)sizeof(auth)) {
            return ESP_ERR_NO_MEM;
        }
    } else {
        int auth_len = snprintf(auth, sizeof(auth), "Bearer %s", s_token);
        if (auth_len < 0 || auth_len >= (int)sizeof(auth)) {
            return ESP_ERR_NO_MEM;
        }
    }

    int written = snprintf(s_headers, sizeof(s_headers),
                           "Authorization: %s\r\n"
                           "Protocol-Version: %d\r\n"
                           "Device-Id: %s\r\n"
                           "Client-Id: %s\r\n",
                           auth,
                           s_protocol_version,
                           s_device_id,
                           s_client_id);
    if (written < 0 || written >= (int)sizeof(s_headers)) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

static esp_err_t xiaozhi_copy_string(char *dst, size_t dst_size, const char *src)
{
    if (dst == NULL || dst_size == 0 || src == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    int written = snprintf(dst, dst_size, "%s", src);
    if (written < 0 || written >= (int)dst_size) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

static esp_err_t xiaozhi_make_ota_body(char *body, size_t body_size)
{
    if (body == NULL || body_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    uint32_t flash_size = 0;
    esp_flash_get_size(NULL, &flash_size);

    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    const esp_app_desc_t *app_desc = esp_app_get_description();

    int written = snprintf(body,
                           body_size,
                           "{\"version\":2,"
                           "\"language\":\"zh-CN\","
                           "\"flash_size\":%lu,"
                           "\"minimum_free_heap_size\":\"%lu\","
                           "\"mac_address\":\"%s\","
                           "\"uuid\":\"%s\","
                           "\"chip_model_name\":\"esp32s3\","
                           "\"chip_info\":{\"model\":%d,\"cores\":%d,"
                           "\"revision\":%d,\"features\":%lu},"
                           "\"application\":{\"name\":\"%s\","
                           "\"version\":\"%s\","
                           "\"compile_time\":\"%sT%sZ\","
                           "\"idf_version\":\"%s\"},"
                           "\"board\":{\"type\":\"smart-speaker\"}}",
                           (unsigned long)flash_size,
                           (unsigned long)esp_get_minimum_free_heap_size(),
                           s_device_id,
                           s_client_id,
                           chip_info.model,
                           chip_info.cores,
                           chip_info.revision,
                           (unsigned long)chip_info.features,
                           app_desc->project_name,
                           app_desc->version,
                           app_desc->date,
                           app_desc->time,
                           app_desc->idf_ver);
    if (written < 0 || written >= (int)body_size) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

static esp_err_t xiaozhi_protocol_parse_ota_response(const char *response)
{
    cJSON *root = cJSON_Parse(response);
    if (root == NULL) {
        ESP_LOGW(TAG, "XiaoZhi OTA response is not JSON");
        return ESP_ERR_INVALID_RESPONSE;
    }

    esp_err_t ret = ESP_ERR_INVALID_RESPONSE;
    s_activation_required = false;
    s_activation_code[0] = '\0';
    s_activation_message[0] = '\0';

    cJSON *activation = cJSON_GetObjectItem(root, "activation");
    if (cJSON_IsObject(activation)) {
        cJSON *message = cJSON_GetObjectItem(activation, "message");
        cJSON *code = cJSON_GetObjectItem(activation, "code");
        cJSON *challenge = cJSON_GetObjectItem(activation, "challenge");
        if (cJSON_IsString(message)) {
            xiaozhi_copy_string(s_activation_message,
                                sizeof(s_activation_message),
                                message->valuestring);
        }
        if (cJSON_IsString(code)) {
            xiaozhi_copy_string(s_activation_code,
                                sizeof(s_activation_code),
                                code->valuestring);
        }
        if (cJSON_IsString(code) || cJSON_IsString(challenge)) {
            s_activation_required = true;
            ESP_LOGW(TAG, "XiaoZhi activation required: code=%s message=%s",
                     strlen(s_activation_code) > 0 ? s_activation_code : "none",
                     strlen(s_activation_message) > 0 ? s_activation_message : "none");
        }
    }

    cJSON *websocket = cJSON_GetObjectItem(root, "websocket");
    cJSON *url = cJSON_GetObjectItem(websocket, "url");
    cJSON *token = cJSON_GetObjectItem(websocket, "token");
    cJSON *version = cJSON_GetObjectItem(websocket, "version");

    if (!cJSON_IsObject(websocket) || !cJSON_IsString(url) || !cJSON_IsString(token)) {
        ESP_LOGW(TAG, "XiaoZhi OTA response has no websocket url/token");
        goto done;
    }

    ret = xiaozhi_copy_string(s_uri, sizeof(s_uri), url->valuestring);
    if (ret != ESP_OK) {
        goto done;
    }
    ret = xiaozhi_copy_string(s_token, sizeof(s_token), token->valuestring);
    if (ret != ESP_OK) {
        goto done;
    }
    s_protocol_version = cJSON_IsNumber(version) ? version->valueint : 1;
    if (s_protocol_version <= 0) {
        s_protocol_version = 1;
    }

    ESP_LOGI(TAG, "XiaoZhi OTA websocket discovered: url=%s token=%s version=%d",
             s_uri,
             strlen(s_token) > 0 ? "set" : "empty",
             s_protocol_version);

done:
    cJSON_Delete(root);
    return ret;
}

static esp_err_t xiaozhi_protocol_fetch_ota_config(void)
{
    ESP_LOGI(TAG, "XiaoZhi heap before OTA TLS: free=%lu min=%lu",
             (unsigned long)heap_caps_get_free_size(MALLOC_CAP_8BIT),
             (unsigned long)heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT));

    esp_http_client_config_t config = {
        .url = CONFIG_SMART_SPEAKER_XIAOZHI_OTA_URL,
        .timeout_ms = CONFIG_SMART_SPEAKER_XIAOZHI_TIMEOUT_MS,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t ret = esp_http_client_set_method(client, HTTP_METHOD_POST);
    if (ret == ESP_OK) {
        ret = esp_http_client_set_header(client, "Activation-Version", "1");
    }
    if (ret == ESP_OK) {
        ret = esp_http_client_set_header(client, "Device-Id", s_device_id);
    }
    if (ret == ESP_OK) {
        ret = esp_http_client_set_header(client, "Client-Id", s_client_id);
    }
    if (ret == ESP_OK) {
        ret = esp_http_client_set_header(client, "User-Agent", "SmartSpeaker/0.1");
    }
    if (ret == ESP_OK) {
        ret = esp_http_client_set_header(client, "Accept-Language", "zh-CN");
    }
    if (ret == ESP_OK) {
        ret = esp_http_client_set_header(client, "Content-Type", "application/json");
    }
    if (ret != ESP_OK) {
        esp_http_client_cleanup(client);
        return ret;
    }

    char body[XIAOZHI_OTA_BODY_SIZE];
    ret = xiaozhi_make_ota_body(body, sizeof(body));
    if (ret != ESP_OK) {
        esp_http_client_cleanup(client);
        return ret;
    }

    size_t body_len = strlen(body);
    ret = esp_http_client_open(client, body_len);
    if (ret == ESP_OK) {
        int written = esp_http_client_write(client, body, body_len);
        if (written != (int)body_len) {
            ret = ESP_FAIL;
        }
    }

    char response[XIAOZHI_OTA_RESPONSE_SIZE] = { 0 };
    if (ret == ESP_OK) {
        int content_length = esp_http_client_fetch_headers(client);
        int status_code = esp_http_client_get_status_code(client);
        int response_len = esp_http_client_read_response(client,
                                                         response,
                                                         sizeof(response) - 1);
        if (status_code != 200 || response_len <= 0) {
            ESP_LOGW(TAG, "XiaoZhi OTA failed: status=%d content_length=%d response_len=%d",
                     status_code,
                     content_length,
                     response_len);
            ret = ESP_FAIL;
        } else {
            response[response_len] = '\0';
            ret = xiaozhi_protocol_parse_ota_response(response);
        }
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return ret;
}

static esp_err_t xiaozhi_load_connection_config(void)
{
    s_uri[0] = '\0';
    s_token[0] = '\0';
    s_protocol_version = 1;

    if (strlen(CONFIG_SMART_SPEAKER_XIAOZHI_DIRECT_WS_URL) > 0) {
        esp_err_t ret = xiaozhi_copy_string(s_uri,
                                            sizeof(s_uri),
                                            CONFIG_SMART_SPEAKER_XIAOZHI_DIRECT_WS_URL);
        if (ret != ESP_OK) {
            return ret;
        }
        ret = xiaozhi_copy_string(s_token,
                                  sizeof(s_token),
                                  CONFIG_SMART_SPEAKER_XIAOZHI_DIRECT_TOKEN);
        if (ret != ESP_OK) {
            return ret;
        }
        ESP_LOGI(TAG, "XiaoZhi direct websocket configured: url=%s token=%s",
                 s_uri,
                 strlen(s_token) > 0 ? "set" : "empty");
        return ESP_OK;
    }

    if (strlen(CONFIG_SMART_SPEAKER_XIAOZHI_OTA_URL) == 0) {
        return ESP_ERR_INVALID_STATE;
    }
    return xiaozhi_protocol_fetch_ota_config();
}

static esp_err_t xiaozhi_protocol_send_hello(void)
{
    if (s_client == NULL || !s_connected) {
        return ESP_ERR_INVALID_STATE;
    }

    char hello[XIAOZHI_HELLO_SIZE];
    int len = snprintf(hello, sizeof(hello),
                       "{\"type\":\"hello\",\"version\":%d,"
                       "\"features\":{\"mcp\":true},"
                       "\"transport\":\"websocket\","
                       "\"audio_params\":{\"format\":\"opus\","
                       "\"sample_rate\":16000,\"channels\":1,"
                       "\"frame_duration\":60}}",
                       s_protocol_version);
    if (len < 0 || len >= (int)sizeof(hello)) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t ret = xiaozhi_protocol_send_text(hello);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "XiaoZhi hello sent: version=%d", s_protocol_version);
    }
    return ret;
}

static esp_err_t xiaozhi_protocol_send_listen(const char *state)
{
    if (s_client == NULL || !s_connected || !s_hello_received || state == NULL) {
        ESP_LOGW(TAG, "Cannot send XiaoZhi listen %s before server hello",
                 state != NULL ? state : "unknown");
        return ESP_ERR_INVALID_STATE;
    }

    char message[XIAOZHI_HELLO_SIZE];
    int len = 0;
    if (strcmp(state, "start") == 0) {
        len = snprintf(message,
                       sizeof(message),
                       "{\"session_id\":\"%s\",\"type\":\"listen\","
                       "\"state\":\"start\",\"mode\":\"auto\"}",
                       s_session_id);
    } else {
        len = snprintf(message,
                       sizeof(message),
                       "{\"session_id\":\"%s\",\"type\":\"listen\","
                       "\"state\":\"stop\"}",
                       s_session_id);
    }
    if (len < 0 || len >= (int)sizeof(message)) {
        return ESP_ERR_NO_MEM;
    }

    return xiaozhi_protocol_send_text(message);
}

static esp_err_t xiaozhi_protocol_send_abort(const char *reason)
{
    if (s_client == NULL || !s_connected || !s_hello_received) {
        ESP_LOGW(TAG, "Cannot send XiaoZhi abort before server hello");
        return ESP_ERR_INVALID_STATE;
    }

    const char *abort_reason = reason != NULL ? reason : "wake_word_detected";
    char message[XIAOZHI_HELLO_SIZE];
    int len = snprintf(message,
                       sizeof(message),
                       "{\"session_id\":\"%s\",\"type\":\"abort\",\"reason\":\"%s\"}",
                       s_session_id,
                       abort_reason);
    if (len < 0 || len >= (int)sizeof(message)) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t ret = xiaozhi_protocol_send_text(message);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "XiaoZhi abort sent: reason=%s", abort_reason);
    }
    return ret;
}

static bool xiaozhi_data_contains(const char *data, int data_len, const char *needle)
{
    if (data == NULL || data_len <= 0 || needle == NULL) {
        return false;
    }

    size_t needle_len = strlen(needle);
    if (needle_len == 0 || data_len < (int)needle_len) {
        return false;
    }

    for (int i = 0; i <= data_len - (int)needle_len; ++i) {
        if (memcmp(data + i, needle, needle_len) == 0) {
            return true;
        }
    }
    return false;
}

static void xiaozhi_protocol_parse_server_hello(const char *data, int data_len)
{
    cJSON *root = cJSON_ParseWithLength(data, data_len);
    if (root == NULL) {
        ESP_LOGW(TAG, "XiaoZhi server hello is not JSON");
        return;
    }

    cJSON *transport = cJSON_GetObjectItem(root, "transport");
    if (cJSON_IsString(transport) && strcmp(transport->valuestring, "websocket") != 0) {
        ESP_LOGW(TAG, "XiaoZhi unsupported transport: %s", transport->valuestring);
    }

    cJSON *session_id = cJSON_GetObjectItem(root, "session_id");
    if (cJSON_IsString(session_id)) {
        xiaozhi_copy_string(s_session_id, sizeof(s_session_id), session_id->valuestring);
        ESP_LOGI(TAG, "XiaoZhi session id: %s", session_id->valuestring);
    }

    cJSON *audio_params = cJSON_GetObjectItem(root, "audio_params");
    if (cJSON_IsObject(audio_params)) {
        cJSON *sample_rate = cJSON_GetObjectItem(audio_params, "sample_rate");
        cJSON *frame_duration = cJSON_GetObjectItem(audio_params, "frame_duration");
        if (cJSON_IsNumber(sample_rate)) {
            s_server_sample_rate = sample_rate->valueint;
        }
        if (cJSON_IsNumber(frame_duration)) {
            s_server_frame_duration = frame_duration->valueint;
        }
        ESP_LOGI(TAG, "XiaoZhi server audio: sample_rate=%d frame_duration=%d",
                 s_server_sample_rate,
                 s_server_frame_duration);
    }

    cJSON_Delete(root);
}

static esp_err_t xiaozhi_protocol_send_mcp_initialize_result(int id)
{
    if (id < 0) {
        return ESP_ERR_INVALID_ARG;
    }

    const esp_app_desc_t *app_desc = esp_app_get_description();
    char response[XIAOZHI_MCP_RESPONSE_SIZE];
    int len = snprintf(response,
                       sizeof(response),
                       "{\"session_id\":\"%s\",\"type\":\"mcp\","
                       "\"payload\":{\"jsonrpc\":\"2.0\",\"id\":%d,"
                       "\"result\":{\"protocolVersion\":\"2024-11-05\","
                       "\"capabilities\":{\"tools\":{}},"
                       "\"serverInfo\":{\"name\":\"SmartSpeaker\","
                       "\"version\":\"%s\"}}}}",
                       s_session_id,
                       id,
                       app_desc->version);
    if (len < 0 || len >= (int)sizeof(response)) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t ret = xiaozhi_protocol_send_text(response);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "XiaoZhi MCP initialize replied: id=%d", id);
    }
    return ret;
}

static esp_err_t xiaozhi_protocol_send_mcp_tools_list_result(int id)
{
    if (id < 0) {
        return ESP_ERR_INVALID_ARG;
    }

    char response[XIAOZHI_MCP_RESPONSE_SIZE];
    int len = snprintf(response,
                       sizeof(response),
                       "{\"session_id\":\"%s\",\"type\":\"mcp\","
                       "\"payload\":{\"jsonrpc\":\"2.0\",\"id\":%d,"
                       "\"result\":{\"tools\":["
                       "{\"name\":\"music.play_default\","
                       "\"description\":\"Play the default local HTTP MP3 music track\","
                       "\"inputSchema\":{\"type\":\"object\",\"properties\":{},\"required\":[]}},"
                       "{\"name\":\"music.list\","
                       "\"description\":\"List local HTTP MP3 music tracks\","
                       "\"inputSchema\":{\"type\":\"object\",\"properties\":{},\"required\":[]}},"
                       "{\"name\":\"music.play_by_name\","
                       "\"description\":\"Play a local HTTP MP3 track by name\","
                       "\"inputSchema\":{\"type\":\"object\",\"properties\":{"
                       "\"query\":{\"type\":\"string\",\"description\":\"Track name or artist\"}},"
                       "\"required\":[\"query\"]}},"
                       "{\"name\":\"music.stop\","
                       "\"description\":\"Stop local HTTP music playback\","
                       "\"inputSchema\":{\"type\":\"object\",\"properties\":{},\"required\":[]}}"
                       "]}}}",
                       s_session_id,
                       id);
    if (len < 0 || len >= (int)sizeof(response)) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t ret = xiaozhi_protocol_send_text(response);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "XiaoZhi MCP tools/list replied: id=%d", id);
    }
    return ret;
}

static esp_err_t xiaozhi_protocol_send_mcp_tool_text_result(int id,
                                                            const char *tool_name,
                                                            const char *text,
                                                            bool is_error)
{
    if (id < 0 || tool_name == NULL || text == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    char response[XIAOZHI_MCP_RESPONSE_SIZE];
    int len = snprintf(response,
                       sizeof(response),
                       "{\"session_id\":\"%s\",\"type\":\"mcp\","
                       "\"payload\":{\"jsonrpc\":\"2.0\",\"id\":%d,"
                       "\"result\":{\"content\":[{\"type\":\"text\","
                       "\"text\":\"%s\"}],\"isError\":%s}}}",
                       s_session_id,
                       id,
                       text,
                       is_error ? "true" : "false");
    if (len < 0 || len >= (int)sizeof(response)) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t ret = xiaozhi_protocol_send_text(response);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "XiaoZhi MCP tool text replied: id=%d tool=%s",
                 id,
                 tool_name);
    }
    return ret;
}

static esp_err_t xiaozhi_protocol_send_mcp_tool_result(int id,
                                                       const char *tool_name,
                                                       esp_err_t tool_ret)
{
    if (id < 0 || tool_name == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const bool is_error = tool_ret != ESP_OK;
    char response[XIAOZHI_MCP_RESPONSE_SIZE];
    int len = snprintf(response,
                       sizeof(response),
                       "{\"session_id\":\"%s\",\"type\":\"mcp\","
                       "\"payload\":{\"jsonrpc\":\"2.0\",\"id\":%d,"
                       "\"result\":{\"content\":[{\"type\":\"text\","
                       "\"text\":\"%s %s\"}],\"isError\":%s}}}",
                       s_session_id,
                       id,
                       tool_name,
                       is_error ? "failed" : "ok",
                       is_error ? "true" : "false");
    if (len < 0 || len >= (int)sizeof(response)) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t ret = xiaozhi_protocol_send_text(response);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "XiaoZhi MCP tool result replied: id=%d tool=%s ret=%s",
                 id, tool_name, esp_err_to_name(tool_ret));
    }
    return ret;
}

static const char *xiaozhi_protocol_get_tool_query(const cJSON *params)
{
    if (!cJSON_IsObject(params)) {
        return NULL;
    }

    cJSON *arguments = cJSON_GetObjectItem(params, "arguments");
    cJSON *query = NULL;
    if (cJSON_IsObject(arguments)) {
        query = cJSON_GetObjectItem(arguments, "query");
        if (!cJSON_IsString(query)) {
            query = cJSON_GetObjectItem(arguments, "name");
        }
    }
    if (!cJSON_IsString(query)) {
        query = cJSON_GetObjectItem(params, "query");
    }
    if (!cJSON_IsString(query)) {
        query = cJSON_GetObjectItem(params, "name");
    }
    return cJSON_IsString(query) ? query->valuestring : NULL;
}

static void xiaozhi_protocol_handle_mcp_tool_call(const cJSON *payload, int id)
{
    cJSON *params = cJSON_GetObjectItem(payload, "params");
    cJSON *name = NULL;
    if (cJSON_IsObject(params)) {
        name = cJSON_GetObjectItem(params, "name");
    }

    if (!cJSON_IsString(name)) {
        ESP_LOGW(TAG, "XiaoZhi MCP tools/call missing tool name");
        (void)xiaozhi_protocol_send_mcp_tool_result(id, "unknown", ESP_ERR_INVALID_ARG);
        return;
    }

    esp_err_t tool_ret = ESP_ERR_NOT_SUPPORTED;
    if (strcmp(name->valuestring, "music.play_default") == 0) {
        xiaozhi_protocol_prepare_local_music("music.play_default");
        tool_ret = ai_music_control_play_default();
    } else if (strcmp(name->valuestring, "music.list") == 0) {
        char list_text[XIAOZHI_MUSIC_LIST_TEXT_SIZE];
        tool_ret = ai_music_control_format_list(list_text, sizeof(list_text));
        if (tool_ret == ESP_OK) {
            if (xiaozhi_protocol_send_mcp_tool_text_result(id,
                                                           name->valuestring,
                                                           list_text,
                                                           false) != ESP_OK) {
                ESP_LOGW(TAG, "XiaoZhi MCP music.list reply failed");
            }
            return;
        }
    } else if (strcmp(name->valuestring, "music.play_by_name") == 0) {
        const char *query = xiaozhi_protocol_get_tool_query(params);
        xiaozhi_protocol_prepare_local_music("music.play_by_name");
        tool_ret = ai_music_control_play_by_name(query);
    } else if (strcmp(name->valuestring, "music.stop") == 0) {
        tool_ret = ai_music_control_stop();
    } else {
        ESP_LOGI(TAG, "XiaoZhi MCP tool ignored: %s", name->valuestring);
    }

    if (xiaozhi_protocol_send_mcp_tool_result(id, name->valuestring, tool_ret) != ESP_OK) {
        ESP_LOGW(TAG, "XiaoZhi MCP tool result reply failed");
    }
}

static void xiaozhi_protocol_handle_mcp(const cJSON *root)
{
    cJSON *payload = cJSON_GetObjectItem(root, "payload");
    if (!cJSON_IsObject(payload)) {
        ESP_LOGW(TAG, "XiaoZhi MCP message missing payload");
        return;
    }

    cJSON *jsonrpc = cJSON_GetObjectItem(payload, "jsonrpc");
    cJSON *method = cJSON_GetObjectItem(payload, "method");
    cJSON *id = cJSON_GetObjectItem(payload, "id");
    if (!cJSON_IsString(jsonrpc) || strcmp(jsonrpc->valuestring, "2.0") != 0 ||
        !cJSON_IsString(method)) {
        ESP_LOGW(TAG, "XiaoZhi MCP message has invalid JSON-RPC fields");
        return;
    }
    if (strncmp(method->valuestring, "notifications/", strlen("notifications/")) == 0) {
        ESP_LOGI(TAG, "XiaoZhi MCP notification: %s", method->valuestring);
        return;
    }
    if (!cJSON_IsNumber(id)) {
        ESP_LOGW(TAG, "XiaoZhi MCP request missing id: %s", method->valuestring);
        return;
    }

    if (strcmp(method->valuestring, "initialize") == 0) {
        if (xiaozhi_protocol_send_mcp_initialize_result(id->valueint) != ESP_OK) {
            ESP_LOGW(TAG, "XiaoZhi MCP initialize reply failed");
        }
    } else if (strcmp(method->valuestring, "tools/list") == 0) {
        bool resume_after_tools_list = s_listening || xiaozhi_audio_stream_is_running();
        if (xiaozhi_audio_stream_is_running()) {
            esp_err_t stream_ret = xiaozhi_audio_stream_stop();
            ESP_LOGI(TAG, "XiaoZhi audio stream stopped before MCP tools/list: %s",
                     esp_err_to_name(stream_ret));
        }
        if (xiaozhi_protocol_send_mcp_tools_list_result(id->valueint) != ESP_OK) {
            ESP_LOGW(TAG, "XiaoZhi MCP tools/list reply failed");
        } else if (resume_after_tools_list) {
            xiaozhi_protocol_resume_audio_after_mcp_tools_list();
        }
    } else if (strcmp(method->valuestring, "tools/call") == 0) {
        xiaozhi_protocol_handle_mcp_tool_call(payload, id->valueint);
    } else {
        ESP_LOGI(TAG, "XiaoZhi MCP method ignored: %s", method->valuestring);
    }
}

static void xiaozhi_protocol_handle_json(const char *data, int data_len)
{
    cJSON *root = cJSON_ParseWithLength(data, data_len);
    if (root == NULL) {
        ESP_LOGW(TAG, "XiaoZhi JSON message is not valid JSON");
        return;
    }

    cJSON *type = cJSON_GetObjectItem(root, "type");
    if (cJSON_IsString(type) && strcmp(type->valuestring, "mcp") == 0) {
        xiaozhi_protocol_handle_mcp(root);
    } else if (cJSON_IsString(type) && strcmp(type->valuestring, "stt") == 0) {
        xiaozhi_protocol_handle_stt_text(root);
    } else if (cJSON_IsString(type) && strcmp(type->valuestring, "tts") == 0) {
        cJSON *state = cJSON_GetObjectItem(root, "state");
        if (cJSON_IsString(state) && strcmp(state->valuestring, "start") == 0) {
            if (s_local_music_active) {
                ESP_LOGI(TAG, "XiaoZhi TTS start ignored during local music");
            } else if (s_tts_interrupted) {
                ESP_LOGI(TAG, "XiaoZhi TTS start ignored after user interrupt");
            } else {
                if (xiaozhi_audio_stream_is_running()) {
                    esp_err_t stream_ret = xiaozhi_audio_stream_stop();
                    ESP_LOGI(TAG, "XiaoZhi audio stream stopped before TTS playback: %s",
                             esp_err_to_name(stream_ret));
                }
                if (s_listening) {
                    esp_err_t listen_ret = xiaozhi_protocol_send_listen("stop");
                    if (listen_ret == ESP_OK) {
                        s_listening = false;
                    }
                    ESP_LOGI(TAG, "XiaoZhi listen stopped before TTS playback: %s",
                             esp_err_to_name(listen_ret));
                }
                if (xiaozhi_tts_player_start() != ESP_OK) {
                    ESP_LOGW(TAG, "XiaoZhi TTS playback start failed");
                }
            }
        } else if (cJSON_IsString(state) && strcmp(state->valuestring, "stop") == 0) {
            if (xiaozhi_tts_player_stop() != ESP_OK) {
                ESP_LOGW(TAG, "XiaoZhi TTS playback stop failed");
            }
            if (xiaozhi_audio_stream_is_running()) {
                esp_err_t stream_ret = xiaozhi_audio_stream_stop();
                ESP_LOGI(TAG, "XiaoZhi audio stream stopped after TTS stop: %s",
                         esp_err_to_name(stream_ret));
            }
            if (s_listening) {
                esp_err_t listen_ret = xiaozhi_protocol_send_listen("stop");
                if (listen_ret == ESP_OK) {
                    s_listening = false;
                }
                ESP_LOGI(TAG, "XiaoZhi listen stopped after TTS stop: %s",
                         esp_err_to_name(listen_ret));
            }
            xiaozhi_protocol_clear_tts_interrupt("server tts stop");
            xiaozhi_post_event(APP_EVENT_AI_RESPONSE_READY, "XiaoZhi TTS done");
        }
    }

    cJSON_Delete(root);
}

static void xiaozhi_websocket_event_handler(void *handler_args,
                                            esp_event_base_t base,
                                            int32_t event_id,
                                            void *event_data)
{
    (void)handler_args;
    (void)base;

    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;
    switch (event_id) {
    case WEBSOCKET_EVENT_CONNECTED:
        s_connected = true;
        s_hello_received = false;
        s_websocket_stack_logged = false;
        ESP_LOGI(TAG, "XiaoZhi WebSocket connected");
        xiaozhi_post_event(APP_EVENT_AI_REQUEST_PENDING, "XiaoZhi connected");
        if (xiaozhi_protocol_send_hello() != ESP_OK) {
            ESP_LOGW(TAG, "XiaoZhi hello send failed");
            xiaozhi_post_event(APP_EVENT_AI_REQUEST_FAILED, "XiaoZhi hello failed");
        }
        break;
    case WEBSOCKET_EVENT_DATA:
        if (!s_websocket_stack_logged) {
            ESP_LOGI(TAG, "XiaoZhi websocket task stack free words=%u",
                     (unsigned)uxTaskGetStackHighWaterMark(NULL));
            s_websocket_stack_logged = true;
        }
        if (data != NULL && data->data_ptr != NULL && data->data_len > 0) {
            if (data->op_code == 0x1) {
                ESP_LOGI(TAG, "XiaoZhi response: %.*s", data->data_len, data->data_ptr);
                if (!s_hello_received &&
                    xiaozhi_data_contains(data->data_ptr, data->data_len, "hello")) {
                    s_hello_received = true;
                    xiaozhi_protocol_parse_server_hello(data->data_ptr, data->data_len);
                    xiaozhi_post_event(APP_EVENT_AI_RESPONSE_READY, "XiaoZhi ready");
                }
                xiaozhi_protocol_handle_json(data->data_ptr, data->data_len);
            } else if (data->op_code == 0x2) {
                if (s_local_music_active || s_tts_interrupted) {
                    if (!s_tts_drop_logged) {
                        ESP_LOGI(TAG, "XiaoZhi TTS audio frames dropped after user interrupt");
                        s_tts_drop_logged = true;
                    }
                    break;
                }
                ESP_LOGD(TAG, "XiaoZhi TTS audio frame: len=%d offset=%d payload=%d fin=%d",
                         data->data_len,
                         data->payload_offset,
                         data->payload_len,
                         data->fin ? 1 : 0);
                const uint8_t *payload = NULL;
                size_t payload_len = 0;
                esp_err_t ret = xiaozhi_extract_audio_payload((const uint8_t *)data->data_ptr,
                                                              (size_t)data->data_len,
                                                              &payload,
                                                              &payload_len);
                if (ret != ESP_OK) {
                    ESP_LOGW(TAG, "XiaoZhi TTS audio frame parse failed: %s",
                             esp_err_to_name(ret));
                    break;
                }
                if (xiaozhi_tts_player_write_opus(payload, payload_len) != ESP_OK) {
                    ESP_LOGW(TAG, "XiaoZhi TTS audio frame enqueue failed");
                }
            } else {
                ESP_LOGD(TAG, "XiaoZhi ignored websocket opcode=0x%02x len=%d",
                         data->op_code,
                         data->data_len);
            }
        }
        break;
    case WEBSOCKET_EVENT_DISCONNECTED:
    {
        bool hello_received = s_hello_received;
        xiaozhi_protocol_reset_connection_state();
        xiaozhi_protocol_clear_tts_interrupt("websocket disconnected");
        ESP_LOGW(TAG, "XiaoZhi WebSocket disconnected%s",
                 hello_received ? "" : " before server hello");
        break;
    }
    case WEBSOCKET_EVENT_ERROR:
        xiaozhi_protocol_reset_connection_state();
        xiaozhi_protocol_clear_tts_interrupt("websocket error");
        ESP_LOGW(TAG, "XiaoZhi WebSocket error");
        xiaozhi_post_event(APP_EVENT_AI_REQUEST_FAILED, "XiaoZhi failed");
        break;
    default:
        break;
    }
}

static esp_err_t xiaozhi_protocol_open_audio_channel_blocking(void)
{
    if (s_client != NULL) {
        (void)xiaozhi_protocol_destroy_client();
    }

    esp_err_t ret = xiaozhi_load_connection_config();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "XiaoZhi connection config failed: %s", esp_err_to_name(ret));
        return ret;
    }
    if (s_activation_required) {
        ESP_LOGW(TAG, "XiaoZhi audio channel blocked until activation is complete");
        return ESP_ERR_INVALID_STATE;
    }
    ret = xiaozhi_make_headers();
    if (ret != ESP_OK) {
        return ret;
    }

    ESP_LOGI(TAG, "XiaoZhi heap before TLS: free=%lu min=%lu",
             (unsigned long)esp_get_free_heap_size(),
             (unsigned long)esp_get_minimum_free_heap_size());

    esp_websocket_client_config_t config = {
        .uri = s_uri,
        .headers = s_headers,
        .task_stack = XIAOZHI_CLIENT_TASK_STACK,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .network_timeout_ms = CONFIG_SMART_SPEAKER_XIAOZHI_TIMEOUT_MS,
        .disable_auto_reconnect = true,
    };

    s_client = esp_websocket_client_init(&config);
    if (s_client == NULL) {
        return ESP_ERR_NO_MEM;
    }

    ret = esp_websocket_register_events(s_client,
                                        WEBSOCKET_EVENT_ANY,
                                        xiaozhi_websocket_event_handler,
                                        NULL);
    if (ret == ESP_OK) {
        ret = esp_websocket_client_start(s_client);
    }
    if (ret != ESP_OK) {
        (void)xiaozhi_protocol_destroy_client();
        return ret;
    }

    s_started = true;
    ESP_LOGI(TAG, "XiaoZhi audio channel open requested");
    return ESP_OK;
}

static void xiaozhi_protocol_open_task(void *arg)
{
    (void)arg;

    esp_err_t ret = xiaozhi_protocol_open_audio_channel_blocking();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "XiaoZhi background start failed: %s", esp_err_to_name(ret));
        xiaozhi_post_event(APP_EVENT_AI_REQUEST_FAILED, "XiaoZhi failed");
    }

    s_starting = false;
    s_open_task_handle = NULL;
    vTaskDelete(NULL);
}

esp_err_t xiaozhi_protocol_init(QueueHandle_t event_queue)
{
    if (event_queue == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    s_event_queue = event_queue;
    if (s_client_mutex == NULL) {
        s_client_mutex = xSemaphoreCreateMutex();
        if (s_client_mutex == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }
    s_open_task_handle = NULL;
    s_started = false;
    s_starting = false;
    s_connected = false;
    s_hello_received = false;
    s_listening = false;
    s_server_sample_rate = 24000;
    s_server_frame_duration = 60;

    esp_err_t ret = xiaozhi_make_ids();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "XiaoZhi device id init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "XiaoZhi client ready, enabled=%s auto=%s ota=%s direct=%s",
             XIAOZHI_ENABLED ? "on" : "off",
             XIAOZHI_AUTO_START ? "on" : "off",
             CONFIG_SMART_SPEAKER_XIAOZHI_OTA_URL,
             strlen(CONFIG_SMART_SPEAKER_XIAOZHI_DIRECT_WS_URL) > 0 ? "set" : "empty");
    return ESP_OK;
}

esp_err_t xiaozhi_protocol_open_audio_channel(void)
{
    if (!XIAOZHI_ENABLED || !XIAOZHI_AUTO_START) {
        ESP_LOGI(TAG, "XiaoZhi WebSocket disabled");
        return ESP_OK;
    }
    if (s_event_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (strlen(CONFIG_SMART_SPEAKER_XIAOZHI_OTA_URL) == 0 &&
        strlen(CONFIG_SMART_SPEAKER_XIAOZHI_DIRECT_WS_URL) == 0) {
        ESP_LOGI(TAG, "XiaoZhi OTA URL and direct WebSocket URL are empty");
        return ESP_OK;
    }
    if (!wifi_manager_is_connected()) {
        ESP_LOGW(TAG, "Cannot start XiaoZhi: Wi-Fi is not connected");
        return ESP_ERR_INVALID_STATE;
    }
    if (s_started || s_starting) {
        ESP_LOGI(TAG, "XiaoZhi WebSocket already started or starting");
        return ESP_OK;
    }

    s_starting = true;
    BaseType_t task_ret = xTaskCreate(xiaozhi_protocol_open_task,
                                      "xz_audio_open",
                                      XIAOZHI_OPEN_TASK_STACK,
                                      NULL,
                                      XIAOZHI_CLIENT_TASK_PRIORITY,
                                      &s_open_task_handle);
    if (task_ret != pdPASS) {
        s_starting = false;
        s_open_task_handle = NULL;
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "XiaoZhi background audio channel open task created");
    return ESP_OK;
}

esp_err_t xiaozhi_protocol_close_audio_channel(void)
{
    s_starting = false;
    esp_err_t ret = xiaozhi_protocol_destroy_client();
    ESP_LOGI(TAG, "XiaoZhi audio channel closed");
    return ret;
}

esp_err_t xiaozhi_protocol_send_text(const char *text)
{
    if (text == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!xiaozhi_protocol_lock_client(pdMS_TO_TICKS(1500))) {
        return ESP_ERR_TIMEOUT;
    }
    if (!xiaozhi_protocol_connection_is_alive()) {
        xiaozhi_protocol_reset_stale_connection("send text before connected");
        xiaozhi_protocol_unlock_client();
        return ESP_ERR_INVALID_STATE;
    }

    int len = (int)strlen(text);
    esp_websocket_client_handle_t client = s_client;
    int sent = esp_websocket_client_send_text(client,
                                              text,
                                              len,
                                              pdMS_TO_TICKS(XIAOZHI_SEND_TEXT_TIMEOUT_MS));
    if (sent < 0) {
        xiaozhi_protocol_reset_stale_connection("send text failed");
        xiaozhi_protocol_unlock_client();
        return ESP_FAIL;
    }
    xiaozhi_protocol_unlock_client();
    return ESP_OK;
}

esp_err_t xiaozhi_protocol_send_audio(const uint8_t *data, size_t len)
{
    if (data == NULL || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_local_music_active) {
        ESP_LOGI(TAG, "XiaoZhi audio frame skipped during local music");
        return ESP_ERR_INVALID_STATE;
    }
    if (!xiaozhi_protocol_lock_client(pdMS_TO_TICKS(XIAOZHI_SEND_AUDIO_LOCK_TIMEOUT_MS))) {
        return ESP_ERR_TIMEOUT;
    }
    if (!xiaozhi_protocol_connection_is_alive()) {
        xiaozhi_protocol_reset_stale_connection("send audio before connected");
        xiaozhi_protocol_unlock_client();
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t frame[XIAOZHI_AUDIO_BINARY_MAX_BYTES];
    const uint8_t *send_data = data;
    size_t send_len = len;

    if (s_protocol_version == 2) {
        send_len = XIAOZHI_BINARY_PROTOCOL2_HEADER_SIZE + len;
        if (send_len > sizeof(frame)) {
            xiaozhi_protocol_unlock_client();
            return ESP_ERR_NO_MEM;
        }
        xiaozhi_write_be16(frame, (uint16_t)s_protocol_version);
        xiaozhi_write_be16(frame + 2, XIAOZHI_BINARY_TYPE_OPUS);
        xiaozhi_write_be32(frame + 4, 0);
        xiaozhi_write_be32(frame + 8, (uint32_t)(esp_timer_get_time() / 1000));
        xiaozhi_write_be32(frame + 12, (uint32_t)len);
        memcpy(frame + XIAOZHI_BINARY_PROTOCOL2_HEADER_SIZE, data, len);
        send_data = frame;
    } else if (s_protocol_version == 3) {
        send_len = XIAOZHI_BINARY_PROTOCOL3_HEADER_SIZE + len;
        if (send_len > sizeof(frame)) {
            xiaozhi_protocol_unlock_client();
            return ESP_ERR_NO_MEM;
        }
        frame[0] = XIAOZHI_BINARY_TYPE_OPUS;
        frame[1] = 0;
        xiaozhi_write_be16(frame + 2, (uint16_t)len);
        memcpy(frame + XIAOZHI_BINARY_PROTOCOL3_HEADER_SIZE, data, len);
        send_data = frame;
    }

    if (!s_first_audio_frame_logged) {
        ESP_LOGI(TAG, "XiaoZhi first audio frame: protocol=%d opus=%u bytes send=%u bytes",
                 s_protocol_version,
                 (unsigned)len,
                 (unsigned)send_len);
        s_first_audio_frame_logged = true;
    }

    esp_websocket_client_handle_t client = s_client;
    int sent = esp_websocket_client_send_bin(client,
                                             (const char *)send_data,
                                             (int)send_len,
                                             pdMS_TO_TICKS(XIAOZHI_SEND_AUDIO_TIMEOUT_MS));
    if (sent != (int)send_len) {
        ESP_LOGW(TAG, "XiaoZhi audio frame send incomplete: sent=%d expected=%u frame=%lu",
                 sent,
                 (unsigned)send_len,
                 (unsigned long)s_audio_frame_count);
        if (xiaozhi_audio_stream_is_stopping()) {
            ESP_LOGI(TAG, "XiaoZhi audio frame send ignored during stream stop");
            xiaozhi_protocol_unlock_client();
            return ESP_ERR_INVALID_STATE;
        }
        xiaozhi_protocol_reset_stale_connection("send audio failed");
        xiaozhi_protocol_unlock_client();
        return ESP_FAIL;
    }
    s_audio_frame_count++;
    xiaozhi_protocol_unlock_client();
    return ESP_OK;
}

esp_err_t xiaozhi_protocol_start_listening(void)
{
    if (s_listening) {
        ESP_LOGI(TAG, "XiaoZhi already listening");
        return ESP_OK;
    }
    s_local_music_active = false;

    bool tts_was_playing = xiaozhi_tts_player_is_playing();
    esp_err_t stop_tts_ret = xiaozhi_tts_player_stop();
    if (stop_tts_ret != ESP_OK) {
        ESP_LOGW(TAG, "stop TTS before listen failed: %s", esp_err_to_name(stop_tts_ret));
    }
    if (tts_was_playing) {
        s_tts_interrupted = true;
        s_tts_drop_logged = false;
        esp_err_t abort_ret = xiaozhi_protocol_send_abort("wake_word_detected");
        if (abort_ret != ESP_OK) {
            ESP_LOGW(TAG, "abort TTS before listen failed: %s", esp_err_to_name(abort_ret));
        }
    } else {
        s_local_music_active = false;
        xiaozhi_protocol_clear_tts_interrupt("listen start");
    }

    esp_err_t ret = xiaozhi_protocol_send_listen("start");
    if (ret == ESP_OK) {
        s_listening = true;
        s_first_audio_frame_logged = false;
        s_audio_frame_count = 0;
        ESP_LOGI(TAG, "XiaoZhi listen started");
        xiaozhi_post_event(APP_EVENT_AI_REQUEST_PENDING, "XiaoZhi listening");
    }
    return ret;
}

esp_err_t xiaozhi_protocol_stop_listening(void)
{
    if (!s_listening) {
        ESP_LOGI(TAG, "XiaoZhi is not listening");
        return ESP_OK;
    }

    esp_err_t ret = xiaozhi_protocol_send_listen("stop");
    if (ret == ESP_OK) {
        s_listening = false;
        xiaozhi_protocol_clear_tts_interrupt("listen stop");
        ESP_LOGI(TAG, "XiaoZhi listen stopped");
        xiaozhi_post_event(APP_EVENT_AI_RESPONSE_READY, "XiaoZhi listen stopped");
    }
    return ret;
}

esp_err_t xiaozhi_protocol_interrupt_tts(void)
{
    s_tts_interrupted = true;
    s_tts_drop_logged = false;
    ESP_LOGI(TAG, "XiaoZhi TTS interrupted by user");

    esp_err_t abort_ret = xiaozhi_protocol_send_abort("wake_word_detected");
    if (abort_ret != ESP_OK) {
        ESP_LOGW(TAG, "XiaoZhi abort send failed: %s", esp_err_to_name(abort_ret));
    }

    esp_err_t stop_ret = xiaozhi_tts_player_stop();
    return stop_ret != ESP_OK ? stop_ret : abort_ret;
}

bool xiaozhi_protocol_is_audio_channel_open(void)
{
    if (!xiaozhi_protocol_connection_is_alive()) {
        if (s_connected) {
            xiaozhi_protocol_reset_stale_connection("connection health check");
        }
        return false;
    }
    return true;
}

bool xiaozhi_protocol_is_listening(void)
{
    return s_listening;
}
