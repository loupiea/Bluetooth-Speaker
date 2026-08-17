#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "app_events.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    APP_STATUS_BOOTING = 0,
    APP_STATUS_IDLE,
    APP_STATUS_GESTURE,
    APP_STATUS_BUTTON,
    APP_STATUS_ERROR,
} app_status_t;

typedef enum {
    APP_PLAYBACK_IDLE = 0,
    APP_PLAYBACK_PLAYING,
    APP_PLAYBACK_ERROR,
} app_playback_status_t;

typedef enum {
    APP_AI_IDLE = 0,
    APP_AI_PENDING,
    APP_AI_READY,
    APP_AI_FAILED,
} app_ai_status_t;

typedef enum {
    APP_WIFI_IDLE = 0,
    APP_WIFI_CONNECTING,
    APP_WIFI_CONNECTED,
    APP_WIFI_DISCONNECTED,
    APP_WIFI_PROVISIONING,
    APP_WIFI_FAILED,
} app_wifi_status_t;

typedef struct {
    app_status_t status;
    app_gesture_t last_gesture;
    app_button_action_t last_button;
    bool i2c_ready;
    bool display_ready;
    bool gesture_ready;
    bool mic_ready;
    bool speaker_ready;
    bool storage_ready;
    bool voice_wakeup_ready;
    bool voice_wakeup_detected;
    bool recording;
    app_playback_status_t playback_status;
    app_ai_status_t ai_status;
    app_wifi_status_t wifi_status;
    uint8_t speaker_volume;
    char message[48];
} app_state_snapshot_t;

void app_state_init(void);
void app_state_handle_event(const app_event_t *event);
void app_state_get_snapshot(app_state_snapshot_t *snapshot);
const char *app_status_to_string(app_status_t status);
const char *app_playback_status_to_string(app_playback_status_t status);
const char *app_ai_status_to_string(app_ai_status_t status);
const char *app_wifi_status_to_string(app_wifi_status_t status);

#ifdef __cplusplus
}
#endif
