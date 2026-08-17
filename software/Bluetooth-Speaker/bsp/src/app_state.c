#include "app_state.h"

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static app_state_snapshot_t s_state;
static StaticSemaphore_t s_state_mutex_buffer;
static SemaphoreHandle_t s_state_mutex;

const char *app_gesture_to_string(app_gesture_t gesture)
{
    switch (gesture) {
    case APP_GESTURE_UP:
        return "UP";
    case APP_GESTURE_DOWN:
        return "DOWN";
    case APP_GESTURE_LEFT:
        return "LEFT";
    case APP_GESTURE_RIGHT:
        return "RIGHT";
    case APP_GESTURE_FORWARD:
        return "FORWARD";
    case APP_GESTURE_BACKWARD:
        return "BACKWARD";
    case APP_GESTURE_CLOCKWISE:
        return "CLOCKWISE";
    case APP_GESTURE_COUNTER_CLOCKWISE:
        return "COUNTER";
    case APP_GESTURE_WAVE:
        return "WAVE";
    case APP_GESTURE_NONE:
    default:
        return "NONE";
    }
}

const char *app_status_to_string(app_status_t status)
{
    switch (status) {
    case APP_STATUS_BOOTING:
        return "BOOTING";
    case APP_STATUS_IDLE:
        return "IDLE";
    case APP_STATUS_GESTURE:
        return "GESTURE";
    case APP_STATUS_BUTTON:
        return "BUTTON";
    case APP_STATUS_ERROR:
        return "ERROR";
    default:
        return "UNKNOWN";
    }
}

const char *app_playback_status_to_string(app_playback_status_t status)
{
    switch (status) {
    case APP_PLAYBACK_PLAYING:
        return "PLAY";
    case APP_PLAYBACK_ERROR:
        return "ERR";
    case APP_PLAYBACK_IDLE:
    default:
        return "IDLE";
    }
}

const char *app_ai_status_to_string(app_ai_status_t status)
{
    switch (status) {
    case APP_AI_PENDING:
        return "PEND";
    case APP_AI_READY:
        return "READY";
    case APP_AI_FAILED:
        return "FAIL";
    case APP_AI_IDLE:
    default:
        return "IDLE";
    }
}

const char *app_wifi_status_to_string(app_wifi_status_t status)
{
    switch (status) {
    case APP_WIFI_CONNECTING:
        return "CONN";
    case APP_WIFI_CONNECTED:
        return "OK";
    case APP_WIFI_DISCONNECTED:
        return "DISC";
    case APP_WIFI_PROVISIONING:
        return "SETUP";
    case APP_WIFI_FAILED:
        return "FAIL";
    case APP_WIFI_IDLE:
    default:
        return "IDLE";
    }
}

const char *app_button_action_to_string(app_button_action_t action)
{
    switch (action) {
    case APP_BUTTON_MAIN:
        return "MAIN";
    case APP_BUTTON_MAIN_LONG:
        return "MAIN LONG";
    case APP_BUTTON_BACK_MUTE:
        return "BACK/MUTE";
    case APP_BUTTON_VOLUME_UP:
        return "VOL+";
    case APP_BUTTON_VOLUME_UP_LONG:
        return "VOL+ LONG";
    case APP_BUTTON_VOLUME_DOWN:
        return "VOL-";
    case APP_BUTTON_VOLUME_DOWN_LONG:
        return "VOL- LONG";
    case APP_BUTTON_NONE:
    default:
        return "NONE";
    }
}

void app_state_init(void)
{
    s_state_mutex = xSemaphoreCreateMutexStatic(&s_state_mutex_buffer);
    memset(&s_state, 0, sizeof(s_state));
    s_state.status = APP_STATUS_BOOTING;
    s_state.last_gesture = APP_GESTURE_NONE;
    s_state.last_button = APP_BUTTON_NONE;
    s_state.playback_status = APP_PLAYBACK_IDLE;
    s_state.ai_status = APP_AI_IDLE;
    s_state.wifi_status = APP_WIFI_IDLE;
    s_state.speaker_volume = 0;
    snprintf(s_state.message, sizeof(s_state.message), "Starting");
}

void app_state_handle_event(const app_event_t *event)
{
    if (event == NULL || s_state_mutex == NULL) {
        return;
    }

    xSemaphoreTake(s_state_mutex, portMAX_DELAY);
    switch (event->type) {
    case APP_EVENT_BOOT:
        s_state.status = APP_STATUS_BOOTING;
        snprintf(s_state.message, sizeof(s_state.message), "Booting");
        break;
    case APP_EVENT_I2C_READY:
        s_state.i2c_ready = true;
        if (s_state.status != APP_STATUS_ERROR) {
            s_state.status = APP_STATUS_IDLE;
        }
        snprintf(s_state.message, sizeof(s_state.message), "I2C ready");
        break;
    case APP_EVENT_DISPLAY_READY:
        s_state.display_ready = true;
        if (s_state.status != APP_STATUS_ERROR) {
            s_state.status = APP_STATUS_IDLE;
        }
        snprintf(s_state.message, sizeof(s_state.message), "OLED ready");
        break;
    case APP_EVENT_GESTURE_SENSOR_READY:
        s_state.gesture_ready = true;
        if (s_state.status != APP_STATUS_ERROR) {
            s_state.status = APP_STATUS_IDLE;
        }
        snprintf(s_state.message, sizeof(s_state.message), "PAJ7620 ready");
        break;
    case APP_EVENT_MIC_READY:
        s_state.mic_ready = true;
        if (s_state.status != APP_STATUS_ERROR) {
            s_state.status = APP_STATUS_IDLE;
        }
        snprintf(s_state.message, sizeof(s_state.message), "INMP441 ready");
        break;
    case APP_EVENT_SPEAKER_READY:
        s_state.speaker_ready = true;
        s_state.speaker_volume = event->speaker_volume;
        if (s_state.status != APP_STATUS_ERROR) {
            s_state.status = APP_STATUS_IDLE;
        }
        snprintf(s_state.message, sizeof(s_state.message), "MAX98357A ready");
        break;
    case APP_EVENT_SPEAKER_VOLUME:
        s_state.speaker_volume = event->speaker_volume;
        if (s_state.status != APP_STATUS_ERROR) {
            s_state.status = APP_STATUS_IDLE;
        }
        snprintf(s_state.message, sizeof(s_state.message), "Volume %u",
                 (unsigned int)event->speaker_volume);
        break;
    case APP_EVENT_PLAYBACK_STARTED:
        s_state.playback_status = APP_PLAYBACK_PLAYING;
        if (s_state.status != APP_STATUS_ERROR) {
            s_state.status = APP_STATUS_IDLE;
        }
        snprintf(s_state.message, sizeof(s_state.message), "Playing");
        break;
    case APP_EVENT_PLAYBACK_STOPPED:
        s_state.playback_status = APP_PLAYBACK_IDLE;
        if (s_state.status != APP_STATUS_ERROR) {
            s_state.status = APP_STATUS_IDLE;
        }
        snprintf(s_state.message, sizeof(s_state.message), "Play done");
        break;
    case APP_EVENT_PLAYBACK_FAILED:
        s_state.playback_status = APP_PLAYBACK_ERROR;
        if (s_state.status != APP_STATUS_ERROR) {
            s_state.status = APP_STATUS_IDLE;
        }
        snprintf(s_state.message, sizeof(s_state.message), "Play failed");
        break;
    case APP_EVENT_AI_REQUEST_PENDING:
        s_state.ai_status = APP_AI_PENDING;
        if (s_state.status != APP_STATUS_ERROR) {
            s_state.status = APP_STATUS_IDLE;
        }
        snprintf(s_state.message, sizeof(s_state.message), "AI pending");
        break;
    case APP_EVENT_AI_RESPONSE_READY:
        s_state.ai_status = APP_AI_READY;
        if (s_state.status != APP_STATUS_ERROR) {
            s_state.status = APP_STATUS_IDLE;
        }
        snprintf(s_state.message, sizeof(s_state.message), "AI ready");
        break;
    case APP_EVENT_AI_REQUEST_FAILED:
        s_state.ai_status = APP_AI_FAILED;
        if (s_state.status != APP_STATUS_ERROR) {
            s_state.status = APP_STATUS_IDLE;
        }
        snprintf(s_state.message, sizeof(s_state.message), "AI failed");
        break;
    case APP_EVENT_WIFI_CONNECTING:
        s_state.wifi_status = APP_WIFI_CONNECTING;
        if (s_state.status != APP_STATUS_ERROR) {
            s_state.status = APP_STATUS_IDLE;
        }
        snprintf(s_state.message, sizeof(s_state.message), "WiFi connecting");
        break;
    case APP_EVENT_WIFI_CONNECTED:
        s_state.wifi_status = APP_WIFI_CONNECTED;
        if (s_state.status != APP_STATUS_ERROR) {
            s_state.status = APP_STATUS_IDLE;
        }
        snprintf(s_state.message, sizeof(s_state.message), "WiFi connected");
        break;
    case APP_EVENT_WIFI_DISCONNECTED:
        s_state.wifi_status = APP_WIFI_DISCONNECTED;
        if (s_state.status != APP_STATUS_ERROR) {
            s_state.status = APP_STATUS_IDLE;
        }
        snprintf(s_state.message, sizeof(s_state.message), "WiFi disconnected");
        break;
    case APP_EVENT_WIFI_PROVISIONING:
        s_state.wifi_status = APP_WIFI_PROVISIONING;
        if (s_state.status != APP_STATUS_ERROR) {
            s_state.status = APP_STATUS_IDLE;
        }
        snprintf(s_state.message, sizeof(s_state.message), "WiFi setup");
        break;
    case APP_EVENT_WIFI_FAILED:
        s_state.wifi_status = APP_WIFI_FAILED;
        if (s_state.status != APP_STATUS_ERROR) {
            s_state.status = APP_STATUS_IDLE;
        }
        snprintf(s_state.message, sizeof(s_state.message), "WiFi failed");
        break;
    case APP_EVENT_STORAGE_READY:
        s_state.storage_ready = true;
        if (s_state.status != APP_STATUS_ERROR) {
            s_state.status = APP_STATUS_IDLE;
        }
        snprintf(s_state.message, sizeof(s_state.message), "SD ready");
        break;
    case APP_EVENT_GESTURE:
        s_state.status = APP_STATUS_GESTURE;
        s_state.last_gesture = event->gesture;
        snprintf(s_state.message, sizeof(s_state.message), "Gesture %s",
                 app_gesture_to_string(event->gesture));
        break;
    case APP_EVENT_BUTTON:
        s_state.status = APP_STATUS_BUTTON;
        s_state.last_button = event->button;
        snprintf(s_state.message, sizeof(s_state.message), "Button %s",
                 app_button_action_to_string(event->button));
        break;
    case APP_EVENT_RECORDING_STARTED:
        s_state.recording = true;
        if (s_state.status != APP_STATUS_ERROR) {
            s_state.status = APP_STATUS_IDLE;
        }
        snprintf(s_state.message, sizeof(s_state.message), "Recording");
        break;
    case APP_EVENT_RECORDING_STOPPED:
        s_state.recording = false;
        if (s_state.status != APP_STATUS_ERROR) {
            s_state.status = APP_STATUS_IDLE;
        }
        snprintf(s_state.message, sizeof(s_state.message), "Rec saved");
        break;
    case APP_EVENT_VOICE_WAKEUP_READY:
        s_state.voice_wakeup_ready = true;
        s_state.voice_wakeup_detected = false;
        if (s_state.status != APP_STATUS_ERROR) {
            s_state.status = APP_STATUS_IDLE;
        }
        snprintf(s_state.message, sizeof(s_state.message), "WakeNet ready");
        break;
    case APP_EVENT_VOICE_WAKEUP:
        s_state.voice_wakeup_ready = true;
        s_state.voice_wakeup_detected = true;
        if (s_state.status != APP_STATUS_ERROR) {
            s_state.status = APP_STATUS_IDLE;
        }
        snprintf(s_state.message, sizeof(s_state.message), "Wake word");
        break;
    case APP_EVENT_VOICE_WAKEUP_FAILED:
        s_state.voice_wakeup_ready = false;
        s_state.voice_wakeup_detected = false;
        if (s_state.status != APP_STATUS_ERROR) {
            s_state.status = APP_STATUS_IDLE;
        }
        snprintf(s_state.message, sizeof(s_state.message), "WakeNet failed");
        break;
    case APP_EVENT_ERROR:
        s_state.status = APP_STATUS_ERROR;
        snprintf(s_state.message, sizeof(s_state.message), "%s",
                 event->message != NULL ? event->message : "Error");
        break;
    default:
        break;
    }
    xSemaphoreGive(s_state_mutex);
}

void app_state_get_snapshot(app_state_snapshot_t *snapshot)
{
    if (snapshot == NULL || s_state_mutex == NULL) {
        return;
    }

    xSemaphoreTake(s_state_mutex, portMAX_DELAY);
    *snapshot = s_state;
    xSemaphoreGive(s_state_mutex);
}
