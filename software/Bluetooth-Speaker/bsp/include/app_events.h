#pragma once

#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

#define APP_EVENT_QUEUE_LENGTH 8

typedef enum {
    APP_GESTURE_NONE = 0,
    APP_GESTURE_UP,
    APP_GESTURE_DOWN,
    APP_GESTURE_LEFT,
    APP_GESTURE_RIGHT,
    APP_GESTURE_FORWARD,
    APP_GESTURE_BACKWARD,
    APP_GESTURE_CLOCKWISE,
    APP_GESTURE_COUNTER_CLOCKWISE,
    APP_GESTURE_WAVE,
} app_gesture_t;

typedef enum {
    APP_BUTTON_NONE = 0,
    APP_BUTTON_MAIN,
    APP_BUTTON_MAIN_LONG,
    APP_BUTTON_BACK_MUTE,
    APP_BUTTON_VOLUME_UP,
    APP_BUTTON_VOLUME_UP_LONG,
    APP_BUTTON_VOLUME_DOWN,
    APP_BUTTON_VOLUME_DOWN_LONG,
} app_button_action_t;

typedef enum {
    APP_EVENT_BOOT = 0,
    APP_EVENT_I2C_READY,
    APP_EVENT_DISPLAY_READY,
    APP_EVENT_GESTURE_SENSOR_READY,
    APP_EVENT_MIC_READY,
    APP_EVENT_SPEAKER_READY,
    APP_EVENT_SPEAKER_VOLUME,
    APP_EVENT_PLAYBACK_STARTED,
    APP_EVENT_PLAYBACK_STOPPED,
    APP_EVENT_PLAYBACK_FAILED,
    APP_EVENT_AI_REQUEST_PENDING,
    APP_EVENT_AI_RESPONSE_READY,
    APP_EVENT_AI_REQUEST_FAILED,
    APP_EVENT_WIFI_CONNECTING,
    APP_EVENT_WIFI_CONNECTED,
    APP_EVENT_WIFI_DISCONNECTED,
    APP_EVENT_WIFI_PROVISIONING,
    APP_EVENT_WIFI_FAILED,
    APP_EVENT_STORAGE_READY,
    APP_EVENT_GESTURE,
    APP_EVENT_BUTTON,
    APP_EVENT_RECORDING_STARTED,
    APP_EVENT_RECORDING_STOPPED,
    APP_EVENT_VOICE_WAKEUP_READY,
    APP_EVENT_VOICE_WAKEUP,
    APP_EVENT_VOICE_WAKEUP_FAILED,
    APP_EVENT_ERROR,
} app_event_type_t;

typedef struct {
    app_event_type_t type;
    app_gesture_t gesture;
    app_button_action_t button;
    uint8_t speaker_volume;
    const char *message;
} app_event_t;

static inline QueueHandle_t app_event_queue_create(void)
{
    return xQueueCreate(APP_EVENT_QUEUE_LENGTH, sizeof(app_event_t));
}

const char *app_gesture_to_string(app_gesture_t gesture);
const char *app_button_action_to_string(app_button_action_t action);

#ifdef __cplusplus
}
#endif
