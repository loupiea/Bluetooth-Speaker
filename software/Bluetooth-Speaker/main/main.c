#include "app_events.h"
#include "app_state.h"
#include "ai_music_control.h"
#include <string.h>
#include "ai_voice.h"
#include "audio_input.h"
#include "audio_music_player.h"
#include "audio_output.h"
#include "audio_player.h"
#include "audio_prompt.h"
#include "audio_recorder.h"
#include "buttons.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "i2c_bus.h"
#include "oled_display.h"
#include "paj7620.h"
#include "storage_sd.h"
#include "voice_wakeup.h"
#include "wifi_manager.h"
#include "xiaozhi_audio_stream.h"
#include "xiaozhi_client.h"
#include "xiaozhi_tts_player.h"

static const char *TAG = "smart_speaker";
static bool s_suppress_next_playback_stop_prompt;
static bool s_gesture_control_enabled;
static bool s_pending_wakeup_listen;
static bool s_manual_music_stop_pending;
static bool s_music_auto_next_enabled;

static void log_memory_status(void)
{
    ESP_LOGI(TAG,
             "PSRAM status: initialized=%d size=%u free=%u largest=%u internal_free=%u internal_largest=%u",
             esp_psram_is_initialized(),
             (unsigned)esp_psram_get_size(),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
}

static void post_event(QueueHandle_t queue, app_event_type_t type, const char *message)
{
    app_event_t event = {
        .type = type,
        .gesture = APP_GESTURE_NONE,
        .button = APP_BUTTON_NONE,
        .message = message,
    };
    xQueueSend(queue, &event, 0);
}

static void post_speaker_event(QueueHandle_t queue, app_event_type_t type, uint8_t volume)
{
    app_event_t event = {
        .type = type,
        .gesture = APP_GESTURE_NONE,
        .button = APP_BUTTON_NONE,
        .speaker_volume = volume,
        .message = NULL,
    };
    xQueueSend(queue, &event, 0);
}

static bool stop_active_playback(bool manual_music_stop)
{
    bool stopped = false;
    bool xiaozhi_active = xiaozhi_audio_stream_is_running() ||
                          xiaozhi_client_is_listening() ||
                          xiaozhi_tts_player_is_playing();

    if (xiaozhi_audio_stream_is_running()) {
        esp_err_t stream_ret = xiaozhi_audio_stream_stop();
        ESP_LOGI(TAG, "XiaoZhi audio stream interrupt result: %s",
                 esp_err_to_name(stream_ret));
    }

    if (xiaozhi_client_is_listening()) {
        esp_err_t listen_ret = xiaozhi_client_stop_listening();
        ESP_LOGI(TAG, "XiaoZhi listen interrupt result: %s",
                 esp_err_to_name(listen_ret));
    }

    if (xiaozhi_active) {
        esp_err_t tts_ret = xiaozhi_client_interrupt_tts();
        ESP_LOGI(TAG, "XiaoZhi TTS interrupt result: %s",
                 esp_err_to_name(tts_ret));
        stopped = true;
    }

    if (audio_player_is_playing()) {
        s_suppress_next_playback_stop_prompt = true;
        esp_err_t player_ret = audio_player_stop();
        ESP_LOGI(TAG, "WAV playback interrupt result: %s",
                 esp_err_to_name(player_ret));
        stopped = true;
    }

    if (audio_music_player_is_playing()) {
        if (manual_music_stop) {
            s_manual_music_stop_pending = true;
            s_music_auto_next_enabled = false;
            s_pending_wakeup_listen = false;
            ESP_LOGI(TAG, "Manual HTTP music stop keeps XiaoZhi listen disabled");
        }
        esp_err_t music_ret = audio_music_player_stop();
        ESP_LOGI(TAG, "HTTP music playback interrupt result: %s",
                 esp_err_to_name(music_ret));
        stopped = true;
    }

    return stopped;
}

static esp_err_t start_xiaozhi_listening(void)
{
    if (!xiaozhi_client_is_enabled()) {
        s_pending_wakeup_listen = false;
        return ESP_ERR_INVALID_STATE;
    }
    if (xiaozhi_client_is_listening() || xiaozhi_audio_stream_is_running()) {
        s_pending_wakeup_listen = false;
        return ESP_OK;
    }
    if (!xiaozhi_client_is_connected()) {
        (void)voice_wakeup_stop();
        esp_err_t start_ret = xiaozhi_client_start();
        if (start_ret == ESP_OK) {
            s_pending_wakeup_listen = false;
            ESP_LOGI(TAG, "XiaoZhi reconnect requested; WakeNet will rearm after ready");
        }
        return start_ret;
    }

    s_pending_wakeup_listen = false;
    (void)voice_wakeup_stop();

    esp_err_t listen_ret = xiaozhi_client_start_listening();
    if (listen_ret != ESP_OK) {
        return listen_ret;
    }

    listen_ret = xiaozhi_audio_stream_init();
    if (listen_ret != ESP_OK) {
        esp_err_t stop_ret = xiaozhi_client_stop_listening();
        ESP_LOGW(TAG, "XiaoZhi listen rollback after stream init failed: %s",
                 esp_err_to_name(stop_ret));
        return listen_ret;
    }

    listen_ret = xiaozhi_audio_stream_start();
    if (listen_ret != ESP_OK) {
        esp_err_t stop_ret = xiaozhi_client_stop_listening();
        ESP_LOGW(TAG, "XiaoZhi listen rollback result: %s",
                 esp_err_to_name(stop_ret));
    }
    return listen_ret;
}

static void handle_gesture_control(QueueHandle_t event_queue, app_gesture_t gesture)
{
    if (!s_gesture_control_enabled) {
        ESP_LOGI(TAG, "Gesture ignored while control mode is disabled");
        return;
    }

    esp_err_t ret = ESP_OK;
    switch (gesture) {
    case APP_GESTURE_FORWARD:
        ESP_LOGI(TAG, "Gesture FORWARD pauses playback");
        if (!stop_active_playback(true)) {
            ESP_LOGI(TAG, "Gesture FORWARD ignored; no active playback");
        }
        break;
    case APP_GESTURE_CLOCKWISE:
        ESP_LOGI(TAG, "Gesture CLOCKWISE volume up");
        ret = audio_output_volume_up();
        if (ret == ESP_OK) {
            post_speaker_event(event_queue, APP_EVENT_SPEAKER_VOLUME,
                               audio_output_get_volume());
            if (!audio_player_is_playing() && !audio_music_player_is_playing()) {
                ret = audio_prompt_play(AUDIO_PROMPT_VOLUME);
            }
        }
        break;
    case APP_GESTURE_COUNTER_CLOCKWISE:
        ESP_LOGI(TAG, "Gesture COUNTER_CLOCKWISE volume down");
        ret = audio_output_volume_down();
        if (ret == ESP_OK) {
            post_speaker_event(event_queue, APP_EVENT_SPEAKER_VOLUME,
                               audio_output_get_volume());
            if (!audio_player_is_playing() && !audio_music_player_is_playing()) {
                ret = audio_prompt_play(AUDIO_PROMPT_VOLUME);
            }
        }
        break;
    default:
        return;
    }

    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Gesture control failed: %s", esp_err_to_name(ret));
        audio_prompt_play(AUDIO_PROMPT_ERROR);
    }
}

void app_main(void)
{
    log_memory_status();

    app_state_init();

    QueueHandle_t event_queue = app_event_queue_create();
    if (event_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create app event queue");
        return;
    }

    post_event(event_queue, APP_EVENT_BOOT, "Boot");

    esp_err_t button_ret = buttons_init(event_queue);
    if (button_ret != ESP_OK) {
        ESP_LOGE(TAG, "Button init failed: %s", esp_err_to_name(button_ret));
        post_event(event_queue, APP_EVENT_ERROR, "Button error");
    }

    esp_err_t mic_ret = audio_input_init();
    if (mic_ret == ESP_OK) {
        post_event(event_queue, APP_EVENT_MIC_READY, "INMP441 ready");
    } else {
        ESP_LOGE(TAG, "INMP441 init failed: %s", esp_err_to_name(mic_ret));
        post_event(event_queue, APP_EVENT_ERROR, "INMP441 error");
    }

    esp_err_t speaker_ret = audio_output_init();
    if (speaker_ret == ESP_OK) {
        post_speaker_event(event_queue, APP_EVENT_SPEAKER_READY, audio_output_get_volume());
    } else {
        ESP_LOGE(TAG, "MAX98357A init failed: %s", esp_err_to_name(speaker_ret));
        post_event(event_queue, APP_EVENT_ERROR, "MAX98357A error");
    }

    esp_err_t storage_ret = storage_sd_init();
    if (storage_ret == ESP_OK) {
        post_event(event_queue, APP_EVENT_STORAGE_READY, "SD ready");
    } else {
        ESP_LOGW(TAG, "SD storage init skipped or failed: %s", esp_err_to_name(storage_ret));
    }

    esp_err_t recorder_ret = audio_recorder_init(event_queue);
    if (recorder_ret != ESP_OK) {
        ESP_LOGE(TAG, "Recorder init failed: %s", esp_err_to_name(recorder_ret));
        post_event(event_queue, APP_EVENT_ERROR, "Recorder error");
    }

    esp_err_t player_ret = audio_player_init(event_queue);
    if (player_ret != ESP_OK) {
        ESP_LOGE(TAG, "WAV player init failed: %s", esp_err_to_name(player_ret));
        post_event(event_queue, APP_EVENT_ERROR, "WAV player error");
    }

    esp_err_t music_player_ret = audio_music_player_init(event_queue);
    if (music_player_ret != ESP_OK) {
        ESP_LOGE(TAG, "HTTP music player init failed: %s", esp_err_to_name(music_player_ret));
        post_event(event_queue, APP_EVENT_ERROR, "HTTP music player error");
    }

    esp_err_t ai_ret = ai_voice_init(event_queue);
    if (ai_ret != ESP_OK) {
        ESP_LOGE(TAG, "AI voice init failed: %s", esp_err_to_name(ai_ret));
        post_event(event_queue, APP_EVENT_ERROR, "AI voice error");
    }

    esp_err_t wakeup_ret = voice_wakeup_init(event_queue);
    if (wakeup_ret != ESP_OK) {
        ESP_LOGE(TAG, "WakeNet init failed: %s", esp_err_to_name(wakeup_ret));
        post_event(event_queue, APP_EVENT_VOICE_WAKEUP_FAILED, "WakeNet init");
    } else {
        wakeup_ret = voice_wakeup_start();
        if (wakeup_ret != ESP_OK) {
            ESP_LOGE(TAG, "WakeNet start failed: %s", esp_err_to_name(wakeup_ret));
            post_event(event_queue, APP_EVENT_VOICE_WAKEUP_FAILED, "WakeNet start");
        }
    }

    esp_err_t xiaozhi_ret = xiaozhi_client_init(event_queue);
    if (xiaozhi_ret != ESP_OK) {
        ESP_LOGE(TAG, "XiaoZhi client init failed: %s", esp_err_to_name(xiaozhi_ret));
        post_event(event_queue, APP_EVENT_ERROR, "XiaoZhi error");
    }

    esp_err_t xiaozhi_tts_ret = xiaozhi_tts_player_init();
    if (xiaozhi_tts_ret != ESP_OK) {
        ESP_LOGE(TAG, "XiaoZhi TTS player init failed: %s",
                 esp_err_to_name(xiaozhi_tts_ret));
        post_event(event_queue, APP_EVENT_ERROR, "XiaoZhi TTS error");
    }

    esp_err_t wifi_ret = wifi_manager_init(event_queue);
    if (wifi_ret == ESP_OK) {
        wifi_ret = wifi_manager_start_auto_connect();
    }
    if (wifi_ret != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi manager start failed: %s", esp_err_to_name(wifi_ret));
        post_event(event_queue, APP_EVENT_WIFI_FAILED, "WiFi error");
    }

    esp_err_t ret = i2c_bus_init();
    if (ret == ESP_OK) {
        post_event(event_queue, APP_EVENT_I2C_READY, "I2C ready");
    } else {
        ESP_LOGE(TAG, "I2C init failed: %s", esp_err_to_name(ret));
        post_event(event_queue, APP_EVENT_ERROR, "I2C error");
    }

    if (ret == ESP_OK) {
        esp_err_t gesture_ret = paj7620_init();
        if (gesture_ret == ESP_OK) {
            xTaskCreate(paj7620_task, "paj7620", 4096, event_queue, 5, NULL);
        } else {
            ESP_LOGE(TAG, "PAJ7620 device setup failed: %s", esp_err_to_name(gesture_ret));
            post_event(event_queue, APP_EVENT_ERROR, "PAJ7620 error");
        }

        esp_err_t display_ret = oled_display_init();
        if (display_ret == ESP_OK) {
            post_event(event_queue, APP_EVENT_DISPLAY_READY, "OLED ready");
            xTaskCreate(oled_display_task, "oled_display", 4096, NULL, 4, NULL);
        } else {
            ESP_LOGW(TAG, "OLED init skipped or failed: %s", esp_err_to_name(display_ret));
        }
    }

    while (true) {
        app_event_t event;
        if (xQueueReceive(event_queue, &event, portMAX_DELAY) == pdTRUE) {
            app_state_handle_event(&event);
            if (event.type == APP_EVENT_GESTURE) {
                ESP_LOGI(TAG, "State updated from gesture: %s",
                         app_gesture_to_string(event.gesture));
                handle_gesture_control(event_queue, event.gesture);
            } else if (event.type == APP_EVENT_BUTTON) {
                ESP_LOGI(TAG, "Button action: %s",
                         app_button_action_to_string(event.button));
                if (event.button == APP_BUTTON_MAIN_LONG) {
                    ESP_LOGI(TAG, "Restarting BLE provisioning");
                    esp_err_t prov_ret = wifi_manager_clear_credentials();
                    if (prov_ret == ESP_OK) {
                        prov_ret = wifi_manager_start_provisioning();
                    }
                    if (prov_ret != ESP_OK) {
                        ESP_LOGW(TAG, "Restart BLE provisioning failed: %s",
                                 esp_err_to_name(prov_ret));
                        audio_prompt_play(AUDIO_PROMPT_ERROR);
                    }
                } else if (event.button == APP_BUTTON_MAIN) {
                    s_gesture_control_enabled = !s_gesture_control_enabled;
                    if (s_gesture_control_enabled) {
                        ESP_LOGI(TAG, "GPIO4 toggled gesture control mode: enabled");
                    } else {
                        ESP_LOGI(TAG, "GPIO4 toggled gesture control mode: disabled");
                    }
                    audio_prompt_play(AUDIO_PROMPT_VOLUME);
                } else if (event.button == APP_BUTTON_BACK_MUTE) {
                    esp_err_t play_ret = ESP_OK;
                    if (stop_active_playback(true)) {
                        ESP_LOGI(TAG, "BACK/MUTE interrupted active playback");
                    } else if (audio_recorder_is_recording()) {
                        ESP_LOGW(TAG, "Recording busy, playback blocked");
                    } else {
                        audio_prompt_play(AUDIO_PROMPT_PLAYBACK_START);
                        play_ret = audio_music_player_play_url_async(CONFIG_SMART_SPEAKER_MUSIC_DEFAULT_URL);
                    }
                    if (play_ret != ESP_OK) {
                        ESP_LOGW(TAG, "Playback control failed: %s",
                                 esp_err_to_name(play_ret));
                        audio_prompt_play(AUDIO_PROMPT_ERROR);
                    }
                } else if (event.button == APP_BUTTON_VOLUME_UP) {
                    esp_err_t volume_ret = audio_output_volume_up();
                    if (volume_ret == ESP_OK) {
                        post_speaker_event(event_queue, APP_EVENT_SPEAKER_VOLUME,
                                           audio_output_get_volume());
                        if (!audio_player_is_playing() && !audio_music_player_is_playing()) {
                            volume_ret = audio_prompt_play(AUDIO_PROMPT_VOLUME);
                        }
                    }
                    if (volume_ret != ESP_OK) {
                        ESP_LOGW(TAG, "Volume up failed: %s", esp_err_to_name(volume_ret));
                    }
                } else if (event.button == APP_BUTTON_VOLUME_DOWN) {
                    esp_err_t volume_ret = audio_output_volume_down();
                    if (volume_ret == ESP_OK) {
                        post_speaker_event(event_queue, APP_EVENT_SPEAKER_VOLUME,
                                           audio_output_get_volume());
                        if (!audio_player_is_playing() && !audio_music_player_is_playing()) {
                            volume_ret = audio_prompt_play(AUDIO_PROMPT_VOLUME);
                        }
                    }
                    if (volume_ret != ESP_OK) {
                        ESP_LOGW(TAG, "Volume down failed: %s", esp_err_to_name(volume_ret));
                    }
                }
            } else if (event.type == APP_EVENT_RECORDING_STARTED) {
                ESP_LOGI(TAG, "Recording state: started");
            } else if (event.type == APP_EVENT_RECORDING_STOPPED) {
                ESP_LOGI(TAG, "Recording state: stopped");
                audio_prompt_play(AUDIO_PROMPT_RECORD_STOP);
                esp_err_t ai_submit_ret = ai_voice_submit_latest_recording();
                if (ai_submit_ret != ESP_OK) {
                    ESP_LOGW(TAG, "AI request failed: %s", esp_err_to_name(ai_submit_ret));
                    audio_prompt_play(AUDIO_PROMPT_ERROR);
                }
                esp_err_t wake_ret = voice_wakeup_start();
                if (wake_ret != ESP_OK) {
                    ESP_LOGW(TAG, "WakeNet restart after recording failed: %s",
                             esp_err_to_name(wake_ret));
                }
            } else if (event.type == APP_EVENT_PLAYBACK_STARTED) {
                ESP_LOGI(TAG, "Playback state: started");
                if (event.message != NULL && strcmp(event.message, "http_music") == 0) {
                    s_music_auto_next_enabled = !s_manual_music_stop_pending;
                }
            } else if (event.type == APP_EVENT_PLAYBACK_STOPPED) {
                ESP_LOGI(TAG, "Playback state: stopped");
                bool is_http_music_event = event.message != NULL && strcmp(event.message, "http_music") == 0;
                if (is_http_music_event && s_manual_music_stop_pending) {
                    s_manual_music_stop_pending = false;
                    s_music_auto_next_enabled = false;
                    s_suppress_next_playback_stop_prompt = false;
                    esp_err_t wake_ret = voice_wakeup_start();
                    if (wake_ret == ESP_OK) {
                        ESP_LOGI(TAG, "WakeNet rearmed after manual HTTP music stop");
                    } else {
                        ESP_LOGW(TAG, "WakeNet rearm after manual HTTP music stop failed: %s",
                                 esp_err_to_name(wake_ret));
                    }
                    ESP_LOGI(TAG, "Manual HTTP music stop completed; XiaoZhi listen remains disabled; WakeNet rearmed");
                } else if (is_http_music_event &&
                           s_music_auto_next_enabled &&
                           !s_suppress_next_playback_stop_prompt) {
                    ESP_LOGI(TAG, "HTTP music finished, auto-playing next track");
                    esp_err_t next_ret = ai_music_control_play_next();
                    if (next_ret != ESP_OK) {
                        s_music_auto_next_enabled = false;
                        ESP_LOGW(TAG, "HTTP music auto-next failed: %s",
                                 esp_err_to_name(next_ret));
                    }
                } else if (s_suppress_next_playback_stop_prompt) {
                    s_suppress_next_playback_stop_prompt = false;
                } else {
                    audio_prompt_play(AUDIO_PROMPT_PLAYBACK_STOP);
                }
            } else if (event.type == APP_EVENT_PLAYBACK_FAILED) {
                ESP_LOGW(TAG, "Playback state: failed");
                s_suppress_next_playback_stop_prompt = false;
                ESP_LOGW(TAG, "Playback error prompt skipped while audio output recovers");
            } else if (event.type == APP_EVENT_AI_REQUEST_PENDING) {
                ESP_LOGI(TAG, "AI state: pending");
            } else if (event.type == APP_EVENT_AI_RESPONSE_READY) {
                ESP_LOGI(TAG, "AI state: response ready");
                s_pending_wakeup_listen = false;
                if (!s_manual_music_stop_pending &&
                           !xiaozhi_client_is_listening() &&
                           !xiaozhi_audio_stream_is_running() &&
                           !xiaozhi_tts_player_is_playing() &&
                           !audio_player_is_playing() &&
                           !audio_music_player_is_playing() &&
                           !voice_wakeup_is_running()) {
                    if (xiaozhi_client_is_connected()) {
                        esp_err_t close_ret = xiaozhi_client_stop();
                        if (close_ret == ESP_OK) {
                            ESP_LOGI(TAG, "XiaoZhi channel closed before WakeNet rearm");
                        } else {
                            ESP_LOGW(TAG, "XiaoZhi channel close before WakeNet rearm failed: %s",
                                     esp_err_to_name(close_ret));
                        }
                    }
                    esp_err_t wake_ret = voice_wakeup_start();
                    if (wake_ret == ESP_OK) {
                        ESP_LOGI(TAG, "XiaoZhi ready keeps WakeNet armed; waiting for wake word");
                        ESP_LOGI(TAG, "XiaoZhi response finished, WakeNet rearmed");
                    } else {
                        ESP_LOGW(TAG, "WakeNet restart after XiaoZhi response failed: %s",
                                 esp_err_to_name(wake_ret));
                    }
                }
            } else if (event.type == APP_EVENT_AI_REQUEST_FAILED) {
                ESP_LOGW(TAG, "AI state: failed");
                s_pending_wakeup_listen = false;
                if (!audio_player_is_playing() && !audio_music_player_is_playing()) {
                    audio_prompt_play(AUDIO_PROMPT_ERROR);
                }
                if (!s_manual_music_stop_pending) {
                    esp_err_t wake_ret = voice_wakeup_start();
                    if (wake_ret != ESP_OK) {
                        ESP_LOGW(TAG, "WakeNet restart after AI failure failed: %s",
                                 esp_err_to_name(wake_ret));
                    }
                }
            } else if (event.type == APP_EVENT_VOICE_WAKEUP_READY) {
                ESP_LOGI(TAG, "WakeNet state: ready");
            } else if (event.type == APP_EVENT_VOICE_WAKEUP) {
                ESP_LOGI(TAG, "WakeNet state: wake word detected");
                if (audio_player_is_playing() || audio_music_player_is_playing()) {
                    (void)stop_active_playback(false);
                }
                esp_err_t listen_ret = start_xiaozhi_listening();
                if (listen_ret != ESP_OK) {
                    ESP_LOGW(TAG, "WakeNet XiaoZhi listen start failed: %s",
                             esp_err_to_name(listen_ret));
                    audio_prompt_play(AUDIO_PROMPT_ERROR);
                    (void)voice_wakeup_start();
                }
            } else if (event.type == APP_EVENT_VOICE_WAKEUP_FAILED) {
                ESP_LOGW(TAG, "WakeNet state: failed");
            } else if (event.type == APP_EVENT_WIFI_CONNECTING) {
                ESP_LOGI(TAG, "Wi-Fi state: connecting");
            } else if (event.type == APP_EVENT_WIFI_CONNECTED) {
                ESP_LOGI(TAG, "Wi-Fi state: connected");
                ESP_LOGI(TAG, "XiaoZhi auto listen skipped; WakeNet stays armed");
                esp_err_t warm_ret = xiaozhi_client_start();
                if (warm_ret == ESP_OK) {
                    ESP_LOGI(TAG, "XiaoZhi channel warming in background without listen");
                } else {
                    ESP_LOGW(TAG, "XiaoZhi channel warm-up failed: %s",
                             esp_err_to_name(warm_ret));
                }
            } else if (event.type == APP_EVENT_WIFI_DISCONNECTED) {
                ESP_LOGW(TAG, "Wi-Fi state: disconnected");
            } else if (event.type == APP_EVENT_WIFI_PROVISIONING) {
                ESP_LOGI(TAG, "Wi-Fi state: provisioning");
            } else if (event.type == APP_EVENT_WIFI_FAILED) {
                ESP_LOGW(TAG, "Wi-Fi state: failed");
            }
        }
    }
}
