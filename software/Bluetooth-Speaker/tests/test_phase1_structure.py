from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def test_phase1_modules_and_entrypoint_are_present():
    expected_files = [
        "bsp/include/app_events.h",
        "bsp/include/app_state.h",
        "bsp/src/app_state.c",
        "ai/include/ai_voice.h",
        "ai/src/ai_voice.c",
        "ai/include/ai_music_control.h",
        "ai/src/ai_music_control.c",
        "ai/include/ai_music_library.h",
        "ai/src/ai_music_library.c",
        "ai/include/xiaozhi_client.h",
        "ai/src/xiaozhi_client.c",
        "wifi/include/wifi_manager.h",
        "wifi/src/wifi_manager.c",
        "bsp/include/audio_input.h",
        "bsp/src/audio_input.c",
        "bsp/include/audio_output.h",
        "bsp/src/audio_output.c",
        "bsp/include/audio_opus.h",
        "bsp/src/audio_opus.c",
        "ai/include/xiaozhi_tts_player.h",
        "ai/src/xiaozhi_tts_player.c",
        "bsp/include/audio_player.h",
        "bsp/src/audio_player.c",
        "bsp/include/audio_prompt.h",
        "bsp/src/audio_prompt.c",
        "bsp/include/audio_recorder.h",
        "bsp/src/audio_recorder.c",
        "ai/include/xiaozhi_audio_stream.h",
        "ai/src/xiaozhi_audio_stream.c",
        "bsp/include/buttons.h",
        "bsp/src/buttons.c",
        "bsp/include/i2c_bus.h",
        "bsp/src/i2c_bus.c",
        "bsp/include/oled_display.h",
        "bsp/src/oled_display.c",
        "bsp/include/paj7620.h",
        "bsp/src/paj7620.c",
        "bsp/include/storage_sd.h",
        "bsp/src/storage_sd.c",
        "bsp/include/voice_wakeup.h",
        "bsp/src/voice_wakeup.c",
        "bsp/CMakeLists.txt",
        "bsp/Kconfig.projbuild",
        "main/main.c",
        "main/CMakeLists.txt",
    ]

    missing = [path for path in expected_files if not (ROOT / path).exists()]
    assert missing == []

    main = read("main/main.c")
    wifi = read("wifi/src/wifi_manager.c")
    assert "app_event_queue_create" in main
    assert "ai_voice_init" in main
    assert "ai_voice_submit_latest_recording" in main
    assert "wifi_manager_init" in main
    assert "wifi_manager_start_auto_connect" in main
    assert "buttons_init" in main
    assert "audio_input_init" in main
    assert "audio_input_task" not in main
    assert "audio_output_init" in main
    assert "audio_output_volume_up" in main
    assert "audio_output_volume_down" in main
    assert "audio_prompt_play" in main
    assert "audio_music_player_play_url_async" in main
    assert "audio_recorder_init" in main
    assert "audio_recorder_toggle" not in main
    assert "storage_sd_init" in main
    assert "oled_display_task" in main
    assert "paj7620_task" in main
    assert "esp_wifi_set_ps(WIFI_PS_NONE)" in wifi
    assert "Wi-Fi power save disabled for local audio streaming" in wifi


def test_phase1_configuration_symbols_are_declared():
    kconfig = read("bsp/Kconfig.projbuild")
    for symbol in [
        "SMART_SPEAKER_I2C_SDA_GPIO",
        "SMART_SPEAKER_I2C_SCL_GPIO",
        "SMART_SPEAKER_BUTTON_MAIN_GPIO",
        "SMART_SPEAKER_BUTTON_BACK_MUTE_GPIO",
        "SMART_SPEAKER_BUTTON_VOLUME_UP_GPIO",
        "SMART_SPEAKER_BUTTON_VOLUME_DOWN_GPIO",
        "SMART_SPEAKER_BUTTON_GUARD_MS",
        "SMART_SPEAKER_BUTTON_CONFIRM_MS",
        "SMART_SPEAKER_BUTTON_LONG_PRESS_MS",
        "SMART_SPEAKER_OLED_I2C_ADDR",
        "SMART_SPEAKER_INMP441_BCLK_GPIO",
        "SMART_SPEAKER_INMP441_LRCLK_GPIO",
        "SMART_SPEAKER_INMP441_DIN_GPIO",
        "SMART_SPEAKER_INMP441_SAMPLE_RATE_HZ",
        "SMART_SPEAKER_MAX98357A_BCLK_GPIO",
        "SMART_SPEAKER_MAX98357A_LRCLK_GPIO",
        "SMART_SPEAKER_MAX98357A_DOUT_GPIO",
        "SMART_SPEAKER_AUDIO_OUTPUT_SAMPLE_RATE_HZ",
        "SMART_SPEAKER_AUDIO_OUTPUT_DEFAULT_VOLUME",
        "SMART_SPEAKER_AUDIO_OUTPUT_TEST_TONE_HZ",
        "SMART_SPEAKER_RECORDER_SAMPLE_RATE_HZ",
        "SMART_SPEAKER_RECORDER_MAX_SECONDS",
        "SMART_SPEAKER_SDMMC_CLK_GPIO",
        "SMART_SPEAKER_SDMMC_CMD_GPIO",
        "SMART_SPEAKER_SDMMC_D0_GPIO",
        "SMART_SPEAKER_SDMMC_CD_GPIO",
        "SMART_SPEAKER_SDMMC_MOUNT_POINT",
        "SMART_SPEAKER_PAJ7620_I2C_ADDR",
        "SMART_SPEAKER_PAJ7620_I2C_FREQ_HZ",
        "SMART_SPEAKER_PAJ7620_INT_GPIO",
        "SMART_SPEAKER_PAJ7620_POWER_ON_DELAY_MS",
        "SMART_SPEAKER_PAJ7620_RECOVERY_RETRY_MS",
        "SMART_SPEAKER_OLED_REFRESH_MS",
    ]:
        assert f"config {symbol}" in kconfig

    sdkconfig = read("sdkconfig")
    assert "CONFIG_SMART_SPEAKER_I2C_SDA_GPIO=8" in sdkconfig
    assert "CONFIG_SMART_SPEAKER_I2C_SCL_GPIO=9" in sdkconfig
    assert "CONFIG_SMART_SPEAKER_BUTTON_MAIN_GPIO=4" in sdkconfig
    assert "CONFIG_SMART_SPEAKER_BUTTON_BACK_MUTE_GPIO=5" in sdkconfig
    assert "CONFIG_SMART_SPEAKER_BUTTON_VOLUME_UP_GPIO=6" in sdkconfig
    assert "CONFIG_SMART_SPEAKER_BUTTON_VOLUME_DOWN_GPIO=7" in sdkconfig
    assert "CONFIG_SMART_SPEAKER_BUTTON_GUARD_MS=120" in sdkconfig
    assert "CONFIG_SMART_SPEAKER_BUTTON_CONFIRM_MS=20" in sdkconfig
    assert "CONFIG_SMART_SPEAKER_BUTTON_LONG_PRESS_MS=2000" in sdkconfig
    assert "CONFIG_SMART_SPEAKER_OLED_I2C_ADDR=0x3C" in sdkconfig
    assert "CONFIG_SMART_SPEAKER_INMP441_BCLK_GPIO=16" in sdkconfig
    assert "CONFIG_SMART_SPEAKER_INMP441_LRCLK_GPIO=17" in sdkconfig
    assert "CONFIG_SMART_SPEAKER_INMP441_DIN_GPIO=18" in sdkconfig
    assert "CONFIG_SMART_SPEAKER_INMP441_SAMPLE_RATE_HZ=16000" in sdkconfig
    assert "CONFIG_SMART_SPEAKER_MIC_LEVEL_PERIOD_MS" not in sdkconfig
    assert "CONFIG_SMART_SPEAKER_MAX98357A_BCLK_GPIO=11" in sdkconfig
    assert "CONFIG_SMART_SPEAKER_MAX98357A_LRCLK_GPIO=12" in sdkconfig
    assert "CONFIG_SMART_SPEAKER_MAX98357A_DOUT_GPIO=10" in sdkconfig
    assert "CONFIG_SMART_SPEAKER_AUDIO_OUTPUT_SAMPLE_RATE_HZ=16000" in sdkconfig
    assert "CONFIG_SMART_SPEAKER_AUDIO_OUTPUT_DEFAULT_VOLUME=5" in sdkconfig
    assert "CONFIG_SMART_SPEAKER_AUDIO_OUTPUT_TEST_TONE_HZ=1000" in sdkconfig
    assert "CONFIG_SMART_SPEAKER_RECORDER_SAMPLE_RATE_HZ=16000" in sdkconfig
    assert "CONFIG_SMART_SPEAKER_RECORDER_MAX_SECONDS=30" in sdkconfig
    assert "CONFIG_FATFS_LFN_HEAP=y" in sdkconfig
    assert "CONFIG_SMART_SPEAKER_SDMMC_CLK_GPIO=14" in sdkconfig
    assert "CONFIG_SMART_SPEAKER_SDMMC_CMD_GPIO=21" in sdkconfig
    assert "CONFIG_SMART_SPEAKER_SDMMC_D0_GPIO=13" in sdkconfig
    assert "CONFIG_SMART_SPEAKER_SDMMC_CD_GPIO=38" in sdkconfig
    assert 'CONFIG_SMART_SPEAKER_SDMMC_MOUNT_POINT="/sdcard"' in sdkconfig
    assert "CONFIG_SMART_SPEAKER_AI_HTTP" not in sdkconfig
    assert "CONFIG_SMART_SPEAKER_PAJ7620_I2C_ADDR=0x73" in sdkconfig
    assert "CONFIG_SMART_SPEAKER_PAJ7620_I2C_FREQ_HZ=100000" in sdkconfig
    assert "CONFIG_SMART_SPEAKER_PAJ7620_INT_GPIO=15" in sdkconfig
    assert "CONFIG_SMART_SPEAKER_PAJ7620_POWER_ON_DELAY_MS=100" in sdkconfig
    assert "CONFIG_SMART_SPEAKER_PAJ7620_RECOVERY_RETRY_MS=500" in sdkconfig
    assert "CONFIG_ESP_MAIN_TASK_STACK_SIZE=8192" in sdkconfig
    assert "CONFIG_MAIN_TASK_STACK_SIZE=8192" in sdkconfig
    assert "CONFIG_SMART_SPEAKER_PAJ7620_INIT_RETRY_COUNT" not in sdkconfig
    assert "CONFIG_SMART_SPEAKER_PAJ7620_INIT_RETRY_DELAY_MS" not in sdkconfig
    assert "CONFIG_SMART_SPEAKER_GESTURE_SCAN_MS" not in sdkconfig


def test_oled_status_page_renders_core_fields():
    oled = read("bsp/src/oled_display.c")
    for text in [
        "XIAOZHI",
        "framebuffer_icon_wifi",
        "framebuffer_icon_storage",
        "framebuffer_icon_ai",
        "framebuffer_icon_microphone",
        "framebuffer_icon_play",
        "framebuffer_icon_alert",
        "framebuffer_volume_bar",
        "oled_render_status_bar",
        "oled_render_hero",
        "oled_render_interaction_bar",
        "G:%s B:%s",
        "VOL",
    ]:
        assert text in oled

    assert "app_button_action_to_string(snapshot->last_button)" in oled
    assert "app_gesture_to_string(snapshot->last_gesture)" in oled
    assert "// +" in oled
    for old_label in [
        "SMART SPEAKER",
        "STAT %s",
        "GEST %s",
        "BTN  %s",
        "WIFI %s SD %s",
        "AI %s",
        "SPK %s %s V%3u",
    ]:
        assert old_label not in oled


def test_oled_uses_page_addressing_for_page_flush():
    oled = read("bsp/src/oled_display.c")
    assert "0x20, 0x02" in oled
    assert "oled_command(0xB0 + page)" in oled


def test_gesture_control_uses_music_pause_and_volume_only():
    main = read("main/main.c")
    handler = main[
        main.index("static void handle_gesture_control(QueueHandle_t event_queue, app_gesture_t gesture)") :
        main.index("void app_main(void)")
    ]

    assert "case APP_GESTURE_FORWARD:" in handler
    assert "Gesture FORWARD pauses playback" in handler
    assert "stop_active_playback(true)" in handler

    assert "case APP_GESTURE_CLOCKWISE:" in handler
    assert "Gesture CLOCKWISE volume up" in handler
    assert "audio_output_volume_up()" in handler

    assert "case APP_GESTURE_COUNTER_CLOCKWISE:" in handler
    assert "Gesture COUNTER_CLOCKWISE volume down" in handler
    assert "audio_output_volume_down()" in handler

    assert "APP_GESTURE_BACKWARD" not in handler
    assert "starts XiaoZhi listening" not in handler
    assert "stops XiaoZhi listening" not in handler
    assert "plays next track" not in handler
    assert "ai_music_control_play_next()" not in handler


def test_oled_uses_conservative_i2c_transfer_settings():
    oled = read("bsp/src/oled_display.c")
    i2c_bus = read("bsp/src/i2c_bus.c")
    assert "#define OLED_I2C_FREQ_HZ 100000" in oled
    assert "#define OLED_DATA_CHUNK_SIZE 16" in oled
    assert "#define OLED_WRITE_RETRY_COUNT 3" in oled
    assert "while (offset < len)" in oled
    assert "xSemaphoreCreateMutex" in i2c_bus
    assert "i2c_bus_lock" in oled
    assert "i2c_bus_unlock" in oled


def test_oled_turns_display_on_after_initial_clear():
    oled = read("bsp/src/oled_display.c")
    assert "0xAF" not in oled.split("const uint8_t init_commands[] = {", 1)[1].split("};", 1)[0]
    assert "oled_flush()" in oled
    assert "oled_command(0xAF)" in oled


def test_gesture_sensor_starts_before_oled_display():
    main = read("main/main.c")
    assert main.index("paj7620_init()") < main.index("oled_display_init()")


def test_gesture_sensor_uses_falling_edge_interrupt():
    paj7620 = read("bsp/src/paj7620.c")
    assert ".pin_bit_mask = 1ULL << CONFIG_SMART_SPEAKER_PAJ7620_INT_GPIO" in paj7620
    assert ".intr_type = GPIO_INTR_NEGEDGE" in paj7620
    assert "gpio_install_isr_service(0)" in paj7620
    assert "gpio_isr_handler_add(CONFIG_SMART_SPEAKER_PAJ7620_INT_GPIO" in paj7620
    assert "paj7620_isr_handler" in paj7620
    assert "xTaskNotifyFromISR" in paj7620
    assert "ulTaskNotifyTake(pdTRUE, portMAX_DELAY)" in paj7620
    assert "Gesture interrupt started" in paj7620
    assert "vTaskDelay(pdMS_TO_TICKS(CONFIG_SMART_SPEAKER_GESTURE_SCAN_MS))" not in paj7620
    assert "i2c_bus_lock" in paj7620
    assert "i2c_bus_unlock" in paj7620


def test_gesture_sensor_uses_background_recovery_init():
    paj7620 = read("bsp/src/paj7620.c")
    main = read("main/main.c")
    assert "CONFIG_SMART_SPEAKER_PAJ7620_POWER_ON_DELAY_MS" in paj7620
    assert "CONFIG_SMART_SPEAKER_PAJ7620_RECOVERY_RETRY_MS" in paj7620
    assert "paj7620_do_init" in paj7620
    assert "while (!s_paj_ready)" in paj7620
    assert "PAJ7620 not ready, retry" in paj7620
    assert "vTaskDelay(pdMS_TO_TICKS(CONFIG_SMART_SPEAKER_PAJ7620_RECOVERY_RETRY_MS))" in paj7620
    assert "APP_EVENT_GESTURE_SENSOR_READY" in paj7620
    assert "post_event(event_queue, APP_EVENT_GESTURE_SENSOR_READY" not in main
    assert "CONFIG_SMART_SPEAKER_PAJ7620_INIT_RETRY_COUNT" not in paj7620
    assert "CONFIG_SMART_SPEAKER_PAJ7620_INIT_RETRY_DELAY_MS" not in paj7620
    assert "PAJ7620 init attempt" not in paj7620


def test_audio_input_uses_inmp441_i2s_rx_settings():
    audio_input = read("bsp/src/audio_input.c")
    assert "I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER)" in audio_input
    assert "I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO" not in audio_input
    assert "i2s_channel_init_std_mode" in audio_input
    assert "i2s_channel_read" in audio_input
    assert "I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT" in audio_input
    assert "slot_cfg.slot_mask = I2S_STD_SLOT_LEFT" in audio_input
    assert ".bclk = CONFIG_SMART_SPEAKER_INMP441_BCLK_GPIO" in audio_input
    assert ".ws = CONFIG_SMART_SPEAKER_INMP441_LRCLK_GPIO" in audio_input
    assert ".din = CONFIG_SMART_SPEAKER_INMP441_DIN_GPIO" in audio_input
    assert "pdTICKS_TO_MS(timeout_ticks)" in audio_input
    assert "audio_input_read_samples" in audio_input
    assert "INMP441 ready: i2s=0" in audio_input
    assert "audio_input_task" not in audio_input
    assert "audio_input_calculate_level" not in audio_input
    assert "audio_input_set_level_reporting_enabled" not in audio_input


def test_opus_encoder_prepares_inmp441_pcm_for_xiaozhi():
    opus_header = read("bsp/include/audio_opus.h")
    opus = read("bsp/src/audio_opus.c")
    stream_header = read("ai/include/xiaozhi_audio_stream.h")
    stream = read("ai/src/xiaozhi_audio_stream.c")
    client_header = read("ai/include/xiaozhi_client.h")
    client = read("ai/src/xiaozhi_client.c")
    cmake = read("bsp/CMakeLists.txt")
    manifest = read("bsp/idf_component.yml")
    main = read("main/main.c")

    assert "audio_opus_init" in opus_header
    assert "audio_opus_deinit" in opus_header
    assert "audio_opus_encode_frame" in opus_header
    assert "AUDIO_OPUS_FRAME_DURATION_MS 60" in opus_header
    assert "AUDIO_OPUS_SAMPLE_RATE_HZ 16000" in opus_header
    assert "AUDIO_OPUS_FRAME_SAMPLES" in opus_header
    assert "esp_opus_enc.h" in opus
    assert "esp_audio_enc.h" in opus
    assert "esp_opus_enc_open" in opus
    assert "esp_opus_enc_close" in opus
    assert "esp_opus_enc_get_frame_size" in opus
    assert "esp_opus_enc_process" in opus
    assert "ESP_OPUS_ENC_FRAME_DURATION_60_MS" in opus
    assert "ESP_OPUS_ENC_APPLICATION_AUDIO" in opus
    assert "ESP_AUDIO_SAMPLE_RATE_16K" in opus
    assert "ESP_AUDIO_MONO" in opus
    assert "ESP_AUDIO_BIT16" in opus

    assert "xiaozhi_audio_stream_init" in stream_header
    assert "xiaozhi_audio_stream_start" in stream_header
    assert "xiaozhi_audio_stream_stop" in stream_header
    assert "audio_input_read_samples" in stream
    assert "audio_opus_encode_frame" in stream
    assert "audio_opus_deinit()" in stream
    assert "xiaozhi_client_send_audio" in stream
    assert "xiaozhi_client_is_listening" in stream
    assert "XIAOZHI_AUDIO_STREAM_TASK_STACK 24576" in stream
    assert "XIAOZHI_AUDIO_SEND_TASK_STACK 8192" in stream
    assert "XIAOZHI_AUDIO_SEND_QUEUE_LENGTH 6" in stream
    assert "XIAOZHI_AUDIO_SEND_QUEUE_FULL_LIMIT 3" in stream
    assert "XiaoZhi audio send queue blocked, stopping listen" in stream
    assert "xiaozhi_client_stop_listening(" not in stream
    assert "XiaoZhi audio stream stack free words" in stream
    assert "XiaoZhi audio send stack free words" in stream
    assert "XIAOZHI_AUDIO_STREAM_READ_CHUNK_SAMPLES 256" in stream
    assert "xTaskCreateStatic(xiaozhi_audio_stream_task" in stream
    assert "xTaskCreateStatic(xiaozhi_audio_send_task" in stream
    assert "s_stream_task_stack" in stream
    assert "s_send_task_stack" in stream
    assert "XiaoZhi audio stream init heap" in stream
    assert "XiaoZhi audio stream encoder released" in stream
    assert "heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)" in stream
    assert "samples_to_read = AUDIO_OPUS_FRAME_SAMPLES - total_read" in stream
    assert "samples_to_read > XIAOZHI_AUDIO_STREAM_READ_CHUNK_SAMPLES" in stream
    assert "raw_samples[i] >> 16" in stream

    assert "xiaozhi_client_send_audio" in client_header
    assert "xiaozhi_client_is_enabled" in client_header
    assert "xiaozhi_protocol_send_audio" in client
    assert "xiaozhi_client_is_enabled" in client
    assert '"src/audio_opus.c"' in cmake
    assert '"../ai/src/xiaozhi_audio_stream.c"' in cmake
    assert "esp_audio_codec" in cmake
    assert "espressif/esp_audio_codec" in manifest
    assert "xiaozhi_audio_stream_init" in main
    assert "xiaozhi_audio_stream_start" in main
    assert "xiaozhi_audio_stream_stop" in main
    assert "XiaoZhi listen rollback after stream init failed" in main


def test_xiaozhi_tts_player_decodes_opus_to_max98357a():
    header = read("ai/include/xiaozhi_tts_player.h")
    player = read("ai/src/xiaozhi_tts_player.c")
    client_header = read("ai/include/xiaozhi_client.h")
    client = read("ai/src/xiaozhi_client.c")
    protocol = read("ai/src/xiaozhi_protocol.c")
    cmake = read("bsp/CMakeLists.txt")
    main = read("main/main.c")

    assert "xiaozhi_tts_player_init" in header
    assert "xiaozhi_tts_player_start" in header
    assert "xiaozhi_tts_player_write_opus" in header
    assert "xiaozhi_tts_player_stop" in header
    assert "xiaozhi_client_interrupt_tts" in client_header
    assert "xiaozhi_protocol_interrupt_tts" in client
    assert "xiaozhi_protocol_send_abort" in protocol
    assert '\\"type\\":\\"abort\\"' in protocol
    assert '\\"reason\\":\\"%s\\"' in protocol
    assert '"wake_word_detected"' in protocol
    assert "esp_opus_dec.h" in player
    assert "esp_opus_dec_open" in player
    assert "esp_opus_dec_close" in player
    assert "esp_opus_dec_decode" in player
    assert "ESP_OPUS_DEC_FRAME_DURATION_60_MS" in player
    assert "ESP_AUDIO_SAMPLE_RATE_24K" in player
    assert "ESP_AUDIO_MONO" in player
    assert "XIAOZHI_TTS_INPUT_SAMPLE_RATE_HZ 24000" in player
    assert "XIAOZHI_TTS_OUTPUT_SAMPLE_RATE_HZ 16000" in player
    assert "xiaozhi_tts_resample_24k_to_16k" in player
    assert "audio_output_start" in player
    assert "audio_output_write_pcm" in player
    assert "audio_output_stop" in player
    decode_fn = player.split("static esp_err_t xiaozhi_tts_decode_and_play", 1)[1].split(
        "static void xiaozhi_tts_player_task", 1
    )[0]
    assert "audio_output_start()" not in decode_fn
    assert "xQueueCreate" in player
    assert "xSemaphoreCreateBinary" in player
    assert "xSemaphoreTake" in player
    assert "xSemaphoreGive" in player
    assert "xTaskCreate(xiaozhi_tts_player_task" in player
    assert "xQueueReset(s_queue)" in player
    assert "xQueueSendToFront" in player
    assert "XiaoZhi TTS decoder released" in player
    assert "if (s_playing) {" in player
    assert "stop speaker after TTS failed" in player
    assert "static bool s_discarding" in player
    assert "xiaozhi_tts_abort_output_after_error" in player
    assert "XiaoZhi TTS output disabled until stop" in player
    assert "if (s_discarding)" in player
    assert "s_discarding = false" in player
    assert "xiaozhi_tts_decoder_init() != ESP_OK" not in player.split("esp_err_t xiaozhi_tts_player_init", 1)[1].split("BaseType_t ok", 1)[0]
    assert '"../ai/src/xiaozhi_tts_player.c"' in cmake
    assert "xiaozhi_tts_player_init" in main
    assert "xiaozhi_tts_player_write_opus" in protocol
    assert "xiaozhi_tts_player_start" in protocol
    assert "xiaozhi_tts_player_stop" in protocol
    assert "s_tts_interrupted" in protocol
    assert "xiaozhi_protocol_clear_tts_interrupt" in protocol
    assert "XiaoZhi TTS interrupt cleared" in protocol
    assert "xiaozhi_protocol_destroy_client" in protocol
    assert "esp_websocket_client_stop" in protocol
    assert ".disable_auto_reconnect = true" in protocol
    assert "stop TTS before listen failed" in protocol
    assert "abort TTS before listen failed" in protocol
    assert "xiaozhi_protocol_clear_tts_interrupt(\"listen start\")" in protocol
    assert "xiaozhi_protocol_clear_tts_interrupt(\"listen stop\")" in protocol
    assert "XiaoZhi TTS audio frames dropped after user interrupt" in protocol


def test_audio_output_uses_max98357a_i2s_tx_settings():
    audio_output = read("bsp/src/audio_output.c")
    app_events = read("bsp/include/app_events.h")
    app_state_h = read("bsp/include/app_state.h")
    app_state = read("bsp/src/app_state.c")
    oled = read("bsp/src/oled_display.c")
    main = read("main/main.c")
    cmake = read("bsp/CMakeLists.txt")
    header = read("bsp/include/audio_output.h")
    assert '"src/audio_output.c"' in cmake
    assert "audio_output_init" in header
    assert "audio_output_write_pcm" in header
    assert "audio_output_play_test_tone" in header
    assert "audio_output_volume_up" in header
    assert "audio_output_volume_down" in header
    assert "I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER)" in audio_output
    assert "I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO" not in audio_output
    assert "chan_cfg.auto_clear = true" in audio_output
    assert "i2s_new_channel(&chan_cfg, &s_tx_chan, NULL)" in audio_output
    assert "i2s_channel_init_std_mode" in audio_output
    assert "i2s_channel_write" in audio_output
    assert "i2s_channel_enable" in audio_output
    assert "i2s_channel_disable" in audio_output
    assert "I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT" in audio_output
    assert ".bclk = CONFIG_SMART_SPEAKER_MAX98357A_BCLK_GPIO" in audio_output
    assert ".ws = CONFIG_SMART_SPEAKER_MAX98357A_LRCLK_GPIO" in audio_output
    assert ".dout = CONFIG_SMART_SPEAKER_MAX98357A_DOUT_GPIO" in audio_output
    assert "CONFIG_SMART_SPEAKER_AUDIO_OUTPUT_TEST_TONE_HZ" in audio_output
    assert "CONFIG_SMART_SPEAKER_AUDIO_OUTPUT_DEFAULT_VOLUME" in audio_output
    assert "audio_output_write_silence" in audio_output
    assert "audio_output_write_pcm_chunk" in audio_output
    assert "audio_output_write_pcm_chunk(samples" in audio_output
    assert "AUDIO_OUTPUT_SILENCE_FRAMES" in audio_output
    assert "#define AUDIO_OUTPUT_PCM_FRAMES 512" in audio_output
    assert "#define AUDIO_OUTPUT_SILENCE_FRAMES 512" in audio_output
    assert "#define AUDIO_OUTPUT_WRITE_TIMEOUT_MS 1000" in audio_output
    assert "pdMS_TO_TICKS(AUDIO_OUTPUT_WRITE_TIMEOUT_MS)" in audio_output
    assert "static SemaphoreHandle_t s_output_mutex" in audio_output
    assert "static uint32_t s_tx_users" in audio_output
    assert "audio_output_reset_tx_locked" in audio_output
    assert "audio_output_disable_tx_locked" in audio_output
    assert 'audio_output_reset_tx_locked("stop/disable failure")' in audio_output
    assert "audio_output_lock" in audio_output
    assert "static int16_t s_pcm_frames" in audio_output
    assert "static int16_t s_silence_frames" in audio_output
    assert "chan_cfg.dma_desc_num = 8" in audio_output
    assert "chan_cfg.dma_frame_num = AUDIO_OUTPUT_DMA_FRAMES" in audio_output
    assert "MAX98357A ready: i2s=1" in audio_output
    assert "APP_EVENT_SPEAKER_READY" in app_events
    assert "APP_EVENT_SPEAKER_VOLUME" in app_events
    assert "APP_EVENT_PLAYBACK_STARTED" in app_events
    assert "APP_EVENT_PLAYBACK_STOPPED" in app_events
    assert "APP_EVENT_PLAYBACK_FAILED" in app_events
    assert "APP_EVENT_AI_REQUEST_PENDING" in app_events
    assert "APP_EVENT_AI_RESPONSE_READY" in app_events
    assert "APP_EVENT_AI_REQUEST_FAILED" in app_events
    assert "APP_EVENT_WIFI_CONNECTING" in app_events
    assert "APP_EVENT_WIFI_CONNECTED" in app_events
    assert "APP_EVENT_WIFI_DISCONNECTED" in app_events
    assert "APP_EVENT_WIFI_PROVISIONING" in app_events
    assert "APP_EVENT_WIFI_FAILED" in app_events
    assert "uint8_t speaker_volume" in app_events
    assert "bool speaker_ready" in app_state_h
    assert "uint8_t speaker_volume" in app_state_h
    assert "app_playback_status_t playback_status" in app_state_h
    assert "app_ai_status_t ai_status" in app_state_h
    assert "app_wifi_status_t wifi_status" in app_state_h
    assert "APP_PLAYBACK_PLAYING" in app_state
    assert "APP_PLAYBACK_ERROR" in app_state
    assert "APP_AI_PENDING" in app_state
    assert "APP_AI_FAILED" in app_state
    assert "APP_WIFI_PROVISIONING" in app_state
    assert "APP_WIFI_FAILED" in app_state
    assert "app_wifi_status_to_string" in oled
    assert "s_state.speaker_ready = true" in app_state
    assert "s_state.speaker_volume = event->speaker_volume" in app_state
    assert "snapshot->playback_status" in oled
    assert "snapshot->ai_status" in oled
    assert "snapshot->wifi_status" in oled
    assert "snapshot->speaker_volume" in oled
    assert "framebuffer_icon_play" in oled
    assert "framebuffer_icon_ai" in oled
    assert "framebuffer_volume_bar" in oled
    assert "post_speaker_event(event_queue, APP_EVENT_SPEAKER_READY" in main
    assert "post_speaker_event(event_queue, APP_EVENT_SPEAKER_VOLUME" in main
    assert "Playback state: started" in main
    assert "Playback state: stopped" in main
    assert "Playback state: failed" in main
    assert "AI state: pending" in main
    assert "AI state: response ready" in main
    assert "AI state: failed" in main
    assert "Wi-Fi state: connecting" in main
    assert "Wi-Fi state: connected" in main
    assert "Wi-Fi state: provisioning" in main
    assert "Wi-Fi state: failed" in main
    assert "event.button == APP_BUTTON_VOLUME_UP" in main
    assert "event.button == APP_BUTTON_VOLUME_DOWN" in main
    assert "event.button == APP_BUTTON_VOLUME_UP_LONG" not in main
    assert "event.button == APP_BUTTON_VOLUME_DOWN_LONG" not in main
    assert "event.button == APP_BUTTON_MAIN" in main
    assert "event.button == APP_BUTTON_BACK_MUTE" in main
    assert "stop_active_playback" in main
    assert "BACK/MUTE interrupted active playback" in main
    assert "s_manual_music_stop_pending" in main
    assert "s_music_auto_next_enabled" in main
    assert "Manual HTTP music stop keeps XiaoZhi listen disabled" in main
    assert "WakeNet rearmed after manual HTTP music stop" in main
    assert "Manual HTTP music stop completed; XiaoZhi listen remains disabled; WakeNet rearmed" in main
    assert "HTTP music finished, auto-playing next track" in main
    assert 'event.message != NULL && strcmp(event.message, "http_music") == 0' in main
    assert "xiaozhi_client_interrupt_tts" in main
    assert "s_gesture_control_enabled" in main
    assert "s_gesture_control_enabled = !s_gesture_control_enabled" in main
    assert "GPIO4 toggled gesture control mode: enabled" in main
    assert "GPIO4 toggled gesture control mode: disabled" in main


def test_gesture_control_mode_maps_gestures_to_actions():
    main = read("main/main.c")
    handler = main[
        main.index("static void handle_gesture_control(QueueHandle_t event_queue, app_gesture_t gesture)") :
        main.index("void app_main(void)")
    ]
    music_header = read("ai/include/ai_music_control.h")
    music = read("ai/src/ai_music_control.c")
    library_header = read("ai/include/ai_music_library.h")

    assert "ai_music_control_play_next" in music_header
    assert "ai_music_library_count" in library_header
    assert "ai_music_library_get" in library_header
    assert "s_next_track_index" in music
    assert "ai_music_control_set_next_after_track" in music
    assert "ai_music_control_set_next_after_url" in music
    assert "ai_music_control_play_next" in music
    assert "audio_music_player_stop()" in music
    assert "audio_output_is_active()" in music
    assert "ai_music_control_wait_output_idle" in music
    assert "Gesture ignored while control mode is disabled" in handler
    assert "handle_gesture_control" in main
    assert "APP_GESTURE_FORWARD" in handler
    assert "APP_GESTURE_CLOCKWISE" in handler
    assert "APP_GESTURE_COUNTER_CLOCKWISE" in handler
    assert "Gesture FORWARD pauses playback" in handler
    assert "Gesture CLOCKWISE volume up" in handler
    assert "Gesture COUNTER_CLOCKWISE volume down" in handler
    assert "Gesture FORWARD starts XiaoZhi listening" not in handler
    assert "Gesture BACKWARD stops XiaoZhi listening" not in handler
    assert "Gesture CLOCKWISE plays next track" not in handler
    assert "ai_music_control_play_next()" not in handler
    clockwise_block = handler.split("case APP_GESTURE_CLOCKWISE:", 1)[1].split("case APP_GESTURE_COUNTER_CLOCKWISE:", 1)[0]
    assert "audio_output_volume_up()" in clockwise_block
    counter_clockwise_block = handler.split("case APP_GESTURE_COUNTER_CLOCKWISE:", 1)[1].split("default:", 1)[0]
    assert "audio_output_volume_down()" in counter_clockwise_block


def test_audio_player_reads_wav_from_sd_and_uses_pcm_output():
    audio_player = read("bsp/src/audio_player.c")
    header = read("bsp/include/audio_player.h")
    cmake = read("bsp/CMakeLists.txt")
    main = read("main/main.c")
    assert '"src/audio_player.c"' in cmake
    assert "audio_player_play_file" in header
    assert "audio_player_play_recording" in header
    assert "audio_player_play_latest_recording" in header
    assert "audio_player_init" in header
    assert "audio_player_play_latest_recording_async" in header
    assert "audio_player_stop" in header
    assert "audio_player_is_playing" in header
    assert "RIFF" in audio_player
    assert "WAVE" in audio_player
    assert "fmt " in audio_player
    assert "data" in audio_player
    assert 'CONFIG_SMART_SPEAKER_SDMMC_MOUNT_POINT "/recordings/rec_%03u.wav"' in audio_player
    assert "fopen(path, \"rb\")" in audio_player
    assert "fread" in audio_player
    assert "opendir(RECORDINGS_DIR)" in audio_player
    assert "readdir(dir)" in audio_player
    assert "sscanf(entry->d_name" in audio_player
    assert '"rec_%3u.wav%c"' in audio_player
    assert "latest_index" in audio_player
    assert "audio_output_start" in audio_player
    assert "audio_output_write_pcm" in audio_player
    assert "audio_output_stop" in audio_player
    assert "audio_player_task" in audio_player
    assert "xTaskCreate(audio_player_task" in audio_player
    assert "ulTaskNotifyTake(pdTRUE, portMAX_DELAY)" in audio_player
    assert "s_stop_requested" in audio_player
    assert "while (ret == ESP_OK && !s_stop_requested && bytes_remaining > 0)" in audio_player
    assert "APP_EVENT_PLAYBACK_STARTED" in audio_player
    assert "APP_EVENT_PLAYBACK_STOPPED" in audio_player
    assert "APP_EVENT_PLAYBACK_FAILED" in audio_player
    assert "storage_sd_is_mounted" in audio_player
    assert "audio_player_init(event_queue)" in main
    assert "audio_player_is_playing()" in main
    assert "audio_player_stop()" in main
    assert "s_suppress_next_playback_stop_prompt" in main
    assert "Playback busy, recording blocked" not in main
    assert "Recording busy, playback blocked" in main
    assert "audio_recorder_is_recording()" in main
    assert "audio_player_play_latest_recording()" not in main
    assert "set_playback_state(APP_EVENT_PLAYBACK_STARTED)" not in main


def test_audio_http_player_streams_mp3_from_url():
    header = read("bsp/include/audio_http_player.h")
    player = read("bsp/src/audio_http_player.c")
    cmake = read("bsp/CMakeLists.txt")
    main = read("main/main.c")
    sdkconfig = read("sdkconfig")

    assert '"src/audio_http_player.c"' in cmake
    assert "esp_ringbuf" in cmake
    assert "audio_http_player_init" in header
    assert "audio_http_player_play_url_async" in header
    assert "audio_http_player_stop" in header
    assert "audio_http_player_is_playing" in header
    assert "esp_http_client.h" in player
    assert "freertos/ringbuf.h" in player
    assert "esp_http_client_open" in player
    assert "AUDIO_HTTP_PLAYER_OPEN_RETRIES" in player
    assert "audio_http_player_open_http" in player
    assert "retry open MP3 URL" in player
    assert "esp_http_client_read" in player
    assert "audio_http_player_id3v2_tag_size" in player
    assert "audio_http_player_skip_id3v2" in player
    assert "ID3v2 tag skipped" in player
    assert "MP3 sync aligned" in player
    assert "synchsafe" in player
    assert "AUDIO_HTTP_PLAYER_TASK_PRIORITY 6" in player
    assert "esp_heap_caps.h" in player
    assert "AUDIO_HTTP_PLAYER_TASK_STACK 12288" in player
    assert "HTTP MP3 player stack free words" in player
    assert "AUDIO_HTTP_PLAYER_HTTP_BUFFER_BYTES 8192" in player
    assert "AUDIO_HTTP_PLAYER_IN_BYTES 32768" in player
    assert "AUDIO_HTTP_PLAYER_OUT_BYTES 8192" in player
    assert "AUDIO_HTTP_PLAYER_PCM_CHUNK 512" in player
    assert "AUDIO_HTTP_PLAYER_PREFETCH_BYTES 262144" in player
    assert "AUDIO_HTTP_PLAYER_PREFETCH_TASK_STACK 6144" in player
    assert "AUDIO_HTTP_PLAYER_READ_RETRIES" in player
    assert "AUDIO_HTTP_PLAYER_READ_RETRY_DELAY_MS" in player
    assert "audio_http_player_read_more" in player
    assert "audio_http_player_prefetch_task" in player
    assert "audio_http_player_prefetch_read" in player
    assert "xRingbufferCreateWithCaps" in player
    assert "RINGBUF_TYPE_BYTEBUF" in player
    assert "HTTP MP3 prefetch started" in player
    assert "HTTP MP3 prefetch done" in player
    assert "retry read MP3 HTTP stream" in player
    assert ".buffer_size = AUDIO_HTTP_PLAYER_HTTP_BUFFER_BYTES" in player
    assert "HTTP MP3 heap before decoder" in player
    assert "heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)" in player
    assert "MP3 info frame" not in player
    assert "pending_len" in player
    assert "memmove(in_buf, raw.buffer, raw.len)" in player
    assert "esp_audio_dec_register_default" in player
    assert "esp_audio_dec_open" in player
    assert "ESP_AUDIO_TYPE_MP3" in player
    assert "esp_audio_dec_process" in player
    assert "ESP_AUDIO_ERR_BUFF_NOT_ENOUGH" in player
    assert "ESP_AUDIO_ERR_DATA_LACK" in player
    assert "MP3 data lack, waiting for more HTTP data" in player
    assert "audio_http_player_consume_raw" in player
    assert "MP3 decoder consumed overflow" in player
    assert "esp_audio_dec_get_info" in player
    assert "esp_audio_simple_dec" not in player
    assert "audio_http_player_write_pcm" in player
    assert "audio_output_start" in player
    assert "audio_output_write_pcm" in player
    assert "audio_output_stop" in player
    assert "bool output_started = false" in player
    assert "if (!*output_started)" in player
    assert "audio_output_start(), TAG, \"start decoded MP3 output failed\"" in player
    assert "if (output_started)" in player
    assert "APP_EVENT_PLAYBACK_STARTED" in player
    assert "APP_EVENT_PLAYBACK_STOPPED" in player
    assert "APP_EVENT_PLAYBACK_FAILED" in player
    assert '.message = "http_music"' in player
    assert "CONFIG_AUDIO_DECODER_MP3_SUPPORT=y" in sdkconfig
    assert "CONFIG_AUDIO_DECODER_OPUS_SUPPORT=y" in sdkconfig
    for disabled_codec in [
        "AAC", "G711", "AMRNB", "AMRWB", "FLAC", "VORBIS",
        "ADPCM", "ALAC", "PCM", "SBC", "LC3",
    ]:
        assert f"# CONFIG_AUDIO_DECODER_{disabled_codec}_SUPPORT is not set" in sdkconfig
    for disabled_encoder in [
        "AAC", "G711", "AMRNB", "AMRWB", "ADPCM", "ALAC",
        "PCM", "SBC", "LC3",
    ]:
        assert f"# CONFIG_AUDIO_ENCODER_{disabled_encoder}_SUPPORT is not set" in sdkconfig
    assert "CONFIG_AUDIO_ENCODER_OPUS_SUPPORT=y" in sdkconfig
    assert "# CONFIG_AUDIO_SIMPLE_DEC_WAV_SUPPORT is not set" in sdkconfig
    assert "# CONFIG_AUDIO_SIMPLE_DEC_M4A_SUPPORT is not set" in sdkconfig
    assert "# CONFIG_AUDIO_SIMPLE_DEC_TS_SUPPORT is not set" in sdkconfig
    assert "audio_music_player_init(event_queue)" in main
    assert "audio_music_player_stop()" in main
    assert "audio_music_player_is_playing()" in main
    assert "audio_music_player_play_url_async(CONFIG_SMART_SPEAKER_MUSIC_DEFAULT_URL)" in main
    assert "SMART_SPEAKER_MUSIC_DEFAULT_URL" in read("bsp/Kconfig.projbuild")


def test_audio_music_player_dispatches_http_mp3_and_wav_urls():
    header = read("bsp/include/audio_music_player.h")
    player = read("bsp/src/audio_music_player.c")
    cmake = read("bsp/CMakeLists.txt")
    main = read("main/main.c")
    music = read("ai/src/ai_music_control.c")

    assert '"src/audio_music_player.c"' in cmake
    assert "audio_music_player_init" in header
    assert "audio_music_player_play_url_async" in header
    assert "audio_music_player_stop" in header
    assert "audio_music_player_is_playing" in header
    assert "audio_http_player_init(event_queue)" in player
    assert "audio_http_player_play_url_async(url)" in player
    assert "audio_http_player_stop()" in player
    assert "audio_output_is_active()" in player
    assert "Audio output is busy, music playback blocked" in player
    assert "audio_music_player_is_mp3_url" in player
    assert "audio_music_player_is_wav_url" in player
    assert "audio_music_player_play_http_wav" in player
    assert "audio_music_player_read_wav_header" in player
    assert "audio_music_player_write_wav_pcm" in player
    assert "audio_music_player_sample_to_s16" in player
    assert "info->bits_per_sample != 16 && info->bits_per_sample != 24" in player
    assert "bits_per_sample == 24" in player
    assert "0x00800000" in player
    assert "0xFF000000" in player
    assert "RIFF" in player
    assert "WAVE" in player
    assert "fmt " in player
    assert "data" in player
    assert "APP_EVENT_PLAYBACK_STARTED" in player
    assert "APP_EVENT_PLAYBACK_STOPPED" in player
    assert "APP_EVENT_PLAYBACK_FAILED" in player
    assert "s_wav_stop_requested && ret != ESP_OK" in player
    assert "HTTP WAV playback stopped by user" in player
    assert "AUDIO_MUSIC_PLAYER_OPEN_RETRIES" in player
    assert "audio_music_player_open_http" in player
    assert "retry open WAV URL" in player
    assert "AUDIO_MUSIC_PLAYER_READ_RETRIES" in player
    assert "retry read WAV HTTP stream" in player
    assert "vTaskDelay(pdMS_TO_TICKS(AUDIO_MUSIC_PLAYER_READ_RETRY_DELAY_MS))" in player
    assert "audio_output_start" in player
    assert "audio_output_write_pcm" in player
    assert "audio_output_stop" in player
    assert "audio_music_player_init(event_queue)" in main
    assert "audio_music_player_play_url_async(CONFIG_SMART_SPEAKER_MUSIC_DEFAULT_URL)" in main
    assert "audio_music_player_stop()" in main
    assert "audio_music_player_is_playing()" in main
    assert "audio_http_player_play_url_async(CONFIG_SMART_SPEAKER_MUSIC_DEFAULT_URL)" not in main
    assert "audio_http_player_stop()" not in main
    assert "audio_http_player_is_playing()" not in main
    assert "audio_music_player_play_url_async(CONFIG_SMART_SPEAKER_MUSIC_DEFAULT_URL)" in music
    assert "audio_music_player_play_url_async(track->url)" in music
    assert "audio_music_player_stop()" in music
    assert "audio_music_player_is_playing()" in music
    assert "audio_http_player" not in music


def test_xiaozhi_can_control_http_music_playback():
    header = read("ai/include/ai_music_control.h")
    music = read("ai/src/ai_music_control.c")
    library_header = read("ai/include/ai_music_library.h")
    library = read("ai/src/ai_music_library.c")
    protocol = read("ai/src/xiaozhi_protocol.c")
    cmake = read("bsp/CMakeLists.txt")

    assert '"../ai/src/ai_music_control.c"' in cmake
    assert '"../ai/src/ai_music_library.c"' in cmake
    assert "ai_music_track_t" in library_header
    assert "ai_music_library_format_list" in library_header
    assert "ai_music_library_find" in library_header
    assert "不灵不灵" in library
    assert "讨厌" in library
    assert "垃圾桶" in library
    assert "底牌" in library
    assert "ai_music_library_query_contains_token" in library
    assert "ai_music_library_track_matches" in library
    assert "http://lubancat.local:8081/" in library
    assert "ai_music_control_play_default" in header
    assert "ai_music_control_play_by_name" in header
    assert "ai_music_control_format_list" in header
    assert "ai_music_control_stop" in header
    assert "audio_music_player_play_url_async(CONFIG_SMART_SPEAKER_MUSIC_DEFAULT_URL)" in music
    assert "audio_music_player_play_url_async(track->url)" in music
    assert "HTTP music already playing, keeping current track" in music
    assert "ai_music_library_find" in music
    assert "ai_music_library_format_list" in music
    assert "audio_music_player_stop()" in music
    assert "audio_music_player_is_playing()" in music
    assert "music.play_default" in protocol
    assert "music.list" in protocol
    assert "music.play_by_name" in protocol
    assert "music.stop" in protocol
    assert "#define XIAOZHI_MUSIC_LIST_TEXT_SIZE 1600" in protocol
    assert "Play the default local HTTP MP3 music track" in protocol
    assert "List local HTTP MP3 music tracks" in protocol
    assert "Play a local HTTP MP3 track by name" in protocol
    assert "Stop local HTTP music playback" in protocol
    assert '"tools/call"' in protocol
    assert "xiaozhi_protocol_send_mcp_tool_result" in protocol
    assert "xiaozhi_protocol_send_mcp_tool_text_result" in protocol
    assert "xiaozhi_protocol_handle_mcp_tool_call" in protocol
    assert "xiaozhi_protocol_prepare_local_music" in protocol
    assert "XiaoZhi TTS interrupted for local music" in protocol
    prepare_music = protocol.split("static void xiaozhi_protocol_prepare_local_music", 1)[1].split("static void xiaozhi_write_be16", 1)[0]
    assert "xiaozhi_audio_stream_is_running()" in prepare_music
    assert "xiaozhi_audio_stream_stop()" in prepare_music
    assert "xiaozhi_protocol_send_listen(\"stop\")" in prepare_music
    assert "xiaozhi_tts_player_stop()" in protocol
    assert "s_tts_interrupted = true" in protocol
    assert "ai_music_control_play_default()" in protocol
    assert "ai_music_control_play_by_name" in protocol
    assert "ai_music_control_format_list" in protocol
    assert "ai_music_control_stop()" in protocol
    assert "xiaozhi_protocol_handle_music_text" not in protocol
    assert "text music play fallback" not in protocol
    assert "text named music play fallback" not in protocol


def test_audio_prompt_system_uses_short_tone_sequences():
    prompt = read("bsp/src/audio_prompt.c")
    header = read("bsp/include/audio_prompt.h")
    output_header = read("bsp/include/audio_output.h")
    output = read("bsp/src/audio_output.c")
    cmake = read("bsp/CMakeLists.txt")
    main = read("main/main.c")
    assert '"src/audio_prompt.c"' in cmake
    assert "audio_prompt_t" in header
    assert "AUDIO_PROMPT_RECORD_START" in header
    assert "AUDIO_PROMPT_RECORD_STOP" in header
    assert "AUDIO_PROMPT_PLAYBACK_START" in header
    assert "AUDIO_PROMPT_PLAYBACK_STOP" in header
    assert "AUDIO_PROMPT_VOLUME" in header
    assert "AUDIO_PROMPT_ERROR" in header
    assert "audio_prompt_play" in header
    assert "audio_output_play_tone" in output_header
    assert "audio_output_is_active" in output_header
    assert "audio_output_is_active()" in prompt
    assert "Prompt skipped while audio output is active" in prompt
    assert "audio_output_play_tone(uint32_t tone_hz" in output
    assert "TaskHandle_t s_tx_owner" in output
    assert "xTaskGetCurrentTaskHandle()" in output
    assert "s_tx_owner != current_task" in output
    assert "write owner mismatch" in output
    assert "stop owner mismatch" in output
    assert "ESP_ERR_INVALID_STATE" in output
    assert "s_tx_owner = NULL" in output
    assert "audio_output_tone_sample(sample_index++, tone_hz)" in output
    assert "prompt_steps" in prompt
    assert "audio_output_play_tone" in prompt
    assert "vTaskDelay(pdMS_TO_TICKS(step->gap_ms))" in prompt
    assert "audio_prompt_play(AUDIO_PROMPT_RECORD_START)" not in main
    assert "audio_prompt_play(AUDIO_PROMPT_RECORD_STOP)" in main
    assert "audio_prompt_play(AUDIO_PROMPT_PLAYBACK_START)" in main
    assert "audio_prompt_play(AUDIO_PROMPT_PLAYBACK_STOP)" in main
    assert "s_suppress_next_playback_stop_prompt = false" in main
    assert "audio_prompt_play(AUDIO_PROMPT_VOLUME)" in main
    assert "audio_prompt_play(AUDIO_PROMPT_ERROR)" in main
    ai_failed_block = main.split("event.type == APP_EVENT_AI_REQUEST_FAILED", 1)[1].split("event.type == APP_EVENT_WIFI_CONNECTING", 1)[0]
    assert "!audio_player_is_playing() && !audio_music_player_is_playing()" in ai_failed_block


def test_ai_voice_link_is_reserved_after_recording_stops():
    ai_voice = read("ai/src/ai_voice.c")
    header = read("ai/include/ai_voice.h")
    cmake = read("bsp/CMakeLists.txt")
    app_events = read("bsp/include/app_events.h")
    app_state = read("bsp/src/app_state.c")
    oled = read("bsp/src/oled_display.c")
    main = read("main/main.c")
    assert '"../ai/src/ai_voice.c"' in cmake
    assert '"../ai/include"' in cmake
    assert "ai_voice_init" in header
    assert "ai_voice_submit_latest_recording" in header
    assert "ai_voice_is_pending" in header
    assert "APP_EVENT_AI_REQUEST_PENDING" in app_events
    assert "APP_EVENT_AI_RESPONSE_READY" in app_events
    assert "APP_EVENT_AI_REQUEST_FAILED" in app_events
    assert "s_event_queue" in ai_voice
    assert "ai_voice_post_event(APP_EVENT_AI_REQUEST_PENDING)" in ai_voice
    assert "AI request reserved for latest recording" in ai_voice
    assert "APP_AI_PENDING" in app_state
    assert "AI pending" in app_state
    assert "AI failed" in app_state
    assert "snapshot->ai_status == APP_AI_PENDING" in oled
    assert "framebuffer_icon_ai" in oled
    assert "ai_voice_init(event_queue)" in main
    assert "ai_voice_submit_latest_recording()" in main
    assert "AI request failed" in main


def test_ai_voice_http_test_path_is_removed():
    ai_voice = read("ai/src/ai_voice.c")
    kconfig = read("bsp/Kconfig.projbuild")
    readme = read("README.md")

    assert "esp_http_client.h" not in ai_voice
    assert "wifi_manager_is_connected" not in ai_voice
    assert "storage_sd_is_mounted" not in ai_voice
    assert "find_latest_recording" not in ai_voice
    assert "http_upload_wav" not in ai_voice
    assert "ai_voice_task" not in ai_voice
    assert "CONFIG_SMART_SPEAKER_AI_HTTP" not in ai_voice
    assert "config SMART_SPEAKER_AI_HTTP" not in kconfig
    assert "HTTP AI Test Link" not in readme
    assert "tools/ai_http_test_server.py" not in readme


def test_xiaozhi_websocket_client_is_reserved_for_protocol_probe():
    header = read("ai/include/xiaozhi_client.h")
    client = read("ai/src/xiaozhi_client.c")
    protocol_header = read("ai/include/xiaozhi_protocol.h")
    protocol = read("ai/src/xiaozhi_protocol.c")
    cmake = read("bsp/CMakeLists.txt")
    kconfig = read("bsp/Kconfig.projbuild")
    main = read("main/main.c")
    sdkconfig = read("sdkconfig")

    assert '"../ai/src/xiaozhi_client.c"' in cmake
    assert '"../ai/src/xiaozhi_protocol.c"' in cmake
    assert "esp_websocket_client" in cmake
    assert "xiaozhi_client_init" in header
    assert "static SemaphoreHandle_t s_client_mutex" in protocol
    assert "xiaozhi_protocol_lock_client" in protocol
    assert "xiaozhi_protocol_unlock_client" in protocol
    assert "xiaozhi_client_start" in header
    assert "xiaozhi_client_stop" in header
    assert "xiaozhi_client_is_connected" in header
    assert "xiaozhi_protocol_init" in protocol_header
    assert "xiaozhi_protocol_open_audio_channel" in protocol_header
    assert "xiaozhi_protocol_close_audio_channel" in protocol_header
    assert "xiaozhi_protocol_send_text" in protocol_header
    assert "xiaozhi_protocol_send_audio" in protocol_header
    assert "xiaozhi_protocol_start_listening" in protocol_header
    assert "xiaozhi_protocol_stop_listening" in protocol_header
    assert "xiaozhi_protocol_is_listening" in protocol_header
    assert "xiaozhi_protocol_is_audio_channel_open" in protocol_header
    assert "esp_websocket_client.h" in protocol
    assert "xiaozhi_protocol.h" in client
    assert "xiaozhi_protocol_init" in client
    assert "xiaozhi_protocol_open_audio_channel" in client
    assert "xiaozhi_protocol_close_audio_channel" in client
    assert "xiaozhi_protocol_is_audio_channel_open" in client
    assert "xiaozhi_client_start_listening" in header
    assert "xiaozhi_client_stop_listening" in header
    assert "xiaozhi_client_is_listening" in header
    assert "xiaozhi_protocol_start_listening" in client
    assert "xiaozhi_protocol_stop_listening" in client
    assert "xiaozhi_protocol_is_listening" in client
    assert "esp_http_client.h" in protocol
    assert "cJSON.h" in protocol
    assert "esp_crt_bundle.h" in protocol
    assert ".crt_bundle_attach = esp_crt_bundle_attach" in protocol
    assert "XIAOZHI_CLIENT_TASK_STACK 12288" in protocol
    assert ".task_stack = XIAOZHI_CLIENT_TASK_STACK" in protocol
    assert "XiaoZhi websocket task stack free words" in protocol
    assert "esp_get_free_heap_size" in protocol
    assert "esp_get_minimum_free_heap_size" in protocol
    assert "xiaozhi_protocol_fetch_ota_config" in protocol
    assert "xiaozhi_protocol_parse_ota_response" in protocol
    assert "xiaozhi_protocol_open_task" in protocol
    assert "xiaozhi_protocol_open_audio_channel_blocking" in protocol
    assert "xTaskCreate(xiaozhi_protocol_open_task" in protocol
    assert "XIAOZHI_OPEN_TASK_STACK" in protocol
    assert "websocket" in protocol
    assert "XiaoZhi OTA websocket discovered" in protocol
    assert "XiaoZhi audio channel open requested" in protocol
    assert "XiaoZhi client ready, enabled=%s auto=%s ota=%s direct=%s" in client
    ready_log = client.split("XiaoZhi client ready", 1)[1].split(");", 1)[0]
    assert "CONFIG_SMART_SPEAKER_XIAOZHI_DIRECT_TOKEN," not in ready_log
    assert "CONFIG_SMART_SPEAKER_XIAOZHI_ENABLE" in protocol
    assert "CONFIG_SMART_SPEAKER_XIAOZHI_OTA_URL" in protocol
    assert "CONFIG_SMART_SPEAKER_XIAOZHI_DIRECT_WS_URL" in protocol
    assert "CONFIG_SMART_SPEAKER_XIAOZHI_DIRECT_TOKEN" in protocol
    assert "Authorization: %s" in protocol
    assert "Bearer %s" in protocol
    assert "Protocol-Version: %d" in protocol
    assert "Device-Id:" in protocol
    assert "Client-Id:" in protocol
    assert "Activation-Version" in protocol
    assert "Accept-Language" in protocol
    assert '\\"type\\":\\"hello\\"' in protocol
    assert '\\"features\\":{\\"mcp\\":true}' in protocol
    assert "esp_websocket_client_send_text" in protocol
    assert "esp_websocket_client_send_bin" in protocol
    assert "esp_websocket_client_is_connected" in protocol
    assert "xiaozhi_protocol_connection_is_alive" in protocol
    assert "XiaoZhi websocket stale, reset state" in protocol
    assert "xiaozhi_protocol_reset_stale_connection" in protocol
    assert "data->op_code == 0x1" in protocol
    assert "data->op_code == 0x2" in protocol
    assert "XiaoZhi TTS audio frame" in protocol
    assert "xiaozhi_protocol_send_listen" in protocol
    assert '\\"type\\":\\"listen\\"' in protocol
    assert '\\"state\\":\\"start\\"' in protocol
    assert '\\"mode\\":\\"auto\\"' in protocol
    assert '\\"mode\\":\\"manual\\"' not in protocol
    assert '\\"state\\":\\"stop\\"' in protocol
    assert "XiaoZhi listen started" in protocol
    assert "XiaoZhi listen stopped" in protocol
    start_listening_block = protocol.split("esp_err_t xiaozhi_protocol_start_listening(void)", 1)[1].split(
        "esp_err_t xiaozhi_protocol_stop_listening(void)", 1
    )[0]
    assert start_listening_block.index("if (tts_was_playing)") < start_listening_block.index(
        "s_tts_interrupted = true;"
    )
    assert "xiaozhi_protocol_clear_tts_interrupt(\"listen start\")" in start_listening_block
    assert "xiaozhi_protocol_send_hello" in protocol
    assert "xiaozhi_protocol_parse_server_hello" in protocol
    assert "xiaozhi_protocol_handle_mcp" in protocol
    assert "xiaozhi_protocol_send_mcp_initialize_result" in protocol
    assert "xiaozhi_protocol_send_mcp_tools_list_result" in protocol
    assert '"initialize"' in protocol
    assert '"tools/list"' in protocol
    assert '"notifications/"' in protocol
    assert '\\"protocolVersion\\":\\"2024-11-05\\"' in protocol
    assert '\\"capabilities\\":{\\"tools\\":{}}' in protocol
    assert '\\"serverInfo\\":{\\"name\\":\\"SmartSpeaker\\"' in protocol
    assert '\\"result\\":{\\"tools\\":[' in protocol
    assert '"music.play_default"' in protocol
    assert '"music.list"' in protocol
    assert '"music.play_by_name"' in protocol
    assert '"music.stop"' in protocol
    assert '"tools/call"' in protocol
    assert '\\"nextCursor\\":\\""' not in protocol
    assert "XiaoZhi MCP tools/list replied" in protocol
    assert "before server hello" in protocol
    assert "config SMART_SPEAKER_XIAOZHI_ENABLE" in kconfig
    assert "config SMART_SPEAKER_XIAOZHI_OTA_URL" in kconfig
    assert "config SMART_SPEAKER_XIAOZHI_DIRECT_WS_URL" in kconfig
    assert "config SMART_SPEAKER_XIAOZHI_DIRECT_TOKEN" in kconfig
    assert "config SMART_SPEAKER_XIAOZHI_AUTO_START" in kconfig
    assert "xiaozhi_client_init(event_queue)" in main
    assert "xiaozhi_client_start()" in main
    assert "xiaozhi_client_stop()" in main
    assert "xiaozhi_client_start_listening()" in main
    assert "xiaozhi_client_stop_listening()" in main
    assert "xiaozhi_client_is_listening()" in main
    assert "Main short press ignored; XiaoZhi is voice wakeup only" not in main
    assert "s_pending_wakeup_listen" in main
    assert "XiaoZhi ready keeps WakeNet armed; waiting for wake word" in main
    assert "XiaoZhi ready starts pending WakeNet listen" not in main
    assert "start_xiaozhi_listening()" in main
    assert "event.type == APP_EVENT_VOICE_WAKEUP" in main
    assert "APP_EVENT_WIFI_CONNECTED" in main
    assert 'CONFIG_SMART_SPEAKER_XIAOZHI_OTA_URL="https://api.tenclass.net/xiaozhi/ota/"' in sdkconfig
    assert 'CONFIG_SMART_SPEAKER_XIAOZHI_DIRECT_WS_URL=""' in sdkconfig
    assert 'CONFIG_SMART_SPEAKER_XIAOZHI_DIRECT_TOKEN=""' in sdkconfig


def test_wifi_manager_reserves_ble_provisioning_flow():
    wifi_manager = read("wifi/src/wifi_manager.c")
    header = read("wifi/include/wifi_manager.h")
    cmake = read("bsp/CMakeLists.txt")
    app_events = read("bsp/include/app_events.h")
    app_state = read("bsp/src/app_state.c")
    oled = read("bsp/src/oled_display.c")
    main = read("main/main.c")
    readme = read("README.md")
    assert '"../wifi/src/wifi_manager.c"' in cmake
    assert '"../wifi/include"' in cmake
    assert "wifi_manager_init" in header
    assert "wifi_manager_start_auto_connect" in header
    assert "wifi_manager_start_provisioning" in header
    assert "wifi_manager_clear_credentials" in header
    assert "wifi_manager_save_credentials" in header
    assert "wifi_manager_load_credentials" in header
    assert "WIFI_MANAGER_MAX_SSID_LEN" in header
    assert "WIFI_MANAGER_MAX_PASSWORD_LEN" in header
    assert "WIFI_MANAGER_PROV_SERVICE_NAME" in header
    assert "WIFI_MANAGER_PROV_POP" in header
    assert "wifi_manager_is_connected" in header
    assert "APP_EVENT_WIFI_CONNECTING" in app_events
    assert "APP_EVENT_WIFI_CONNECTED" in app_events
    assert "APP_EVENT_WIFI_DISCONNECTED" in app_events
    assert "APP_EVENT_WIFI_PROVISIONING" in app_events
    assert "APP_EVENT_WIFI_FAILED" in app_events
    assert "Wi-Fi manager ready" in wifi_manager
    assert "wifi_prov_mgr_init" in wifi_manager
    assert "wifi_prov_scheme_ble" in wifi_manager
    assert "wifi_prov_scheme_ble_set_service_uuid" in wifi_manager
    assert "wifi_prov_mgr_start_provisioning" in wifi_manager
    assert "WIFI_PROV_SECURITY_1" in wifi_manager
    assert "wifi_manager_prov_event_handler" in wifi_manager
    assert "wifi_manager_protocomm_event_handler" in wifi_manager
    assert "PROTOCOMM_TRANSPORT_BLE_CONNECTED" in wifi_manager
    assert "PROTOCOMM_SECURITY_SESSION_SETUP_OK" in wifi_manager
    assert "PROTOCOMM_SECURITY_SESSION_CREDENTIALS_MISMATCH" in wifi_manager
    assert "WIFI_PROV_CRED_RECV" in wifi_manager
    assert "WIFI_PROV_CRED_SUCCESS" in wifi_manager
    assert "WIFI_PROV_CRED_FAIL" in wifi_manager
    assert "esp_wifi_init" in wifi_manager
    assert "esp_wifi_set_mode(WIFI_MODE_STA)" in wifi_manager
    assert "esp_wifi_set_config(WIFI_IF_STA" in wifi_manager
    assert "esp_wifi_connect()" in wifi_manager
    assert "BLE provisioning started" in wifi_manager
    assert "wifi_creds_t" in wifi_manager
    assert "nvs_flash_init" in wifi_manager
    assert "nvs_open(WIFI_MANAGER_NVS_NAMESPACE" in wifi_manager
    assert "nvs_get_str(handle, WIFI_MANAGER_NVS_KEY_SSID" in wifi_manager
    assert "nvs_get_str(handle, WIFI_MANAGER_NVS_KEY_PASSWORD" in wifi_manager
    assert "nvs_set_str(handle, WIFI_MANAGER_NVS_KEY_SSID" in wifi_manager
    assert "nvs_set_str(handle, WIFI_MANAGER_NVS_KEY_PASSWORD" in wifi_manager
    assert "nvs_commit(handle)" in wifi_manager
    assert "nvs_erase_key(handle, WIFI_MANAGER_NVS_KEY_SSID)" in wifi_manager
    assert "nvs_erase_key(handle, WIFI_MANAGER_NVS_KEY_PASSWORD)" in wifi_manager
    assert "Wi-Fi credentials loaded from NVS" in wifi_manager
    assert "No Wi-Fi credentials saved, entering provisioning" in wifi_manager
    assert "wifi_manager_post_event(APP_EVENT_WIFI_PROVISIONING)" in wifi_manager
    assert "APP_WIFI_PROVISIONING" in app_state
    assert "WiFi setup" in app_state
    assert "WiFi failed" in app_state
    assert "framebuffer_icon_wifi" in oled
    assert "snapshot->wifi_status == APP_WIFI_CONNECTED" in oled
    assert "wifi_manager_init(event_queue)" in main
    assert "wifi_manager_start_auto_connect()" in main
    assert "Wi-Fi Provisioning Plan" in readme
    assert "wifi/include/" in readme
    assert "wifi/src/" in readme
    assert "BLE provisioning service name" in readme


def test_storage_sd_uses_1_bit_sdmmc_and_rw_probe():
    storage_sd = read("bsp/src/storage_sd.c")
    assert "esp_vfs_fat_sdmmc_mount" in storage_sd
    assert "SDMMC_HOST_FLAG_1BIT" in storage_sd
    assert ".clk = CONFIG_SMART_SPEAKER_SDMMC_CLK_GPIO" in storage_sd
    assert ".cmd = CONFIG_SMART_SPEAKER_SDMMC_CMD_GPIO" in storage_sd
    assert ".d0 = CONFIG_SMART_SPEAKER_SDMMC_D0_GPIO" in storage_sd
    assert ".cd = CONFIG_SMART_SPEAKER_SDMMC_CD_GPIO" in storage_sd
    assert 'CONFIG_SMART_SPEAKER_SDMMC_MOUNT_POINT "/hello.txt"' in storage_sd
    assert "fopen" in storage_sd


def test_audio_recorder_writes_wav_files_to_sd_card():
    recorder = read("bsp/src/audio_recorder.c")
    main = read("main/main.c")
    assert "RIFF" in recorder
    assert "WAVE" in recorder
    assert "fmt " in recorder
    assert "data" in recorder
    assert 'CONFIG_SMART_SPEAKER_SDMMC_MOUNT_POINT "/recordings"' in recorder
    assert "rec_%03u.wav" in recorder
    assert "mkdir" in recorder
    assert "audio_input_read_samples" in recorder
    assert "audio_input_set_level_reporting_enabled" not in recorder
    assert "storage_sd_is_mounted" in recorder
    assert "APP_EVENT_RECORDING_STARTED" in recorder
    assert "APP_EVENT_RECORDING_STOPPED" in recorder
    assert "event.button == APP_BUTTON_MAIN" in main
    main_button_block = main.split("event.button == APP_BUTTON_MAIN", 1)[1].split(
        "} else if (event.button == APP_BUTTON_BACK_MUTE)", 1
    )[0]
    assert "audio_recorder_toggle" not in main_button_block
    assert "xiaozhi_client_start_listening" not in main_button_block
    assert "xiaozhi_audio_stream_start" not in main_button_block
    assert "voice wakeup only" in main_button_block


def test_main_long_press_restarts_ble_provisioning():
    buttons = read("bsp/src/buttons.c")
    app_events = read("bsp/include/app_events.h")
    app_state = read("bsp/src/app_state.c")
    main = read("main/main.c")

    assert "APP_BUTTON_MAIN_LONG" in app_events
    assert "APP_BUTTON_VOLUME_UP_LONG" in app_events
    assert "APP_BUTTON_VOLUME_DOWN_LONG" in app_events
    assert "MAIN LONG" in app_state
    assert "VOL+ LONG" in app_state
    assert "VOL- LONG" in app_state
    assert "CONFIG_SMART_SPEAKER_BUTTON_LONG_PRESS_MS" in buttons
    assert "press_ticks" in buttons
    assert "button->action == APP_BUTTON_MAIN" in buttons
    assert "APP_BUTTON_MAIN_LONG" in buttons
    assert "button->action == APP_BUTTON_VOLUME_UP" in buttons
    assert "APP_BUTTON_VOLUME_UP_LONG" in buttons
    assert "button->action == APP_BUTTON_VOLUME_DOWN" in buttons
    assert "APP_BUTTON_VOLUME_DOWN_LONG" in buttons
    assert "event.button == APP_BUTTON_MAIN_LONG" in main
    assert "wifi_manager_clear_credentials()" in main
    assert "Restarting BLE provisioning" in main


def test_music_library_generator_updates_json_and_c_library():
    script = read("tools/generate_music_library.py")
    music_json = read("music/music_list.json")
    library = read("ai/src/ai_music_library.c")

    assert "MUSIC_DIR" in script
    assert "music_list.json" in script
    assert "ai_music_library.c" in script
    assert "urllib.parse.quote" in script
    assert "encoding=\"utf-8\"" in script
    assert ".mp3" in script and ".wav" in script
    assert "http://lubancat.local:8081/music/" in script
    assert "不要手动修改" in library
    assert "不灵不灵" in library
    assert "讨厌" in library
    assert "\"format\": \"mp3\"" in music_json
    assert "\"format\": \"wav\"" in music_json


def test_voice_wakeup_uses_esp_sr_wakenet_and_starts_xiaozhi():
    component_yml = read("bsp/idf_component.yml")
    partitions = read("partitions.csv")
    cmake = read("bsp/CMakeLists.txt")
    kconfig = read("bsp/Kconfig.projbuild")
    app_events = read("bsp/include/app_events.h")
    app_state_h = read("bsp/include/app_state.h")
    app_state = read("bsp/src/app_state.c")
    header = read("bsp/include/voice_wakeup.h")
    wakeup = read("bsp/src/voice_wakeup.c")
    main = read("main/main.c")
    protocol = read("ai/src/xiaozhi_protocol.c")
    oled = read("bsp/src/oled_display.c")
    readme = read("README.md")
    sdkconfig = read("sdkconfig")

    assert "espressif/esp-sr" in component_yml
    assert "model,    data, spiffs" in partitions
    assert '"src/voice_wakeup.c"' in cmake
    assert "esp-sr" in cmake
    assert "config SMART_SPEAKER_VOICE_WAKEUP_ENABLE" in kconfig
    assert "config SMART_SPEAKER_VOICE_WAKEUP_MODEL_PARTITION" in kconfig
    assert "config SMART_SPEAKER_VOICE_WAKEUP_TASK_STACK" in kconfig
    assert "APP_EVENT_VOICE_WAKEUP_READY" in app_events
    assert "APP_EVENT_VOICE_WAKEUP" in app_events
    assert "APP_EVENT_VOICE_WAKEUP_FAILED" in app_events
    assert "bool voice_wakeup_ready" in app_state_h
    assert "bool voice_wakeup_detected" in app_state_h
    assert "voice_wakeup_init" in header
    assert "voice_wakeup_start" in header
    assert "voice_wakeup_is_running" in header
    assert "#include \"esp_afe_sr_iface.h\"" in wakeup
    assert "#include \"esp_afe_sr_models.h\"" in wakeup
    assert "#include \"model_path.h\"" in wakeup
    assert "esp_srmodel_init(CONFIG_SMART_SPEAKER_VOICE_WAKEUP_MODEL_PARTITION)" in wakeup
    assert "CONFIG_SR_WN_WN9S_NIHAOXIAOZHI=y" in sdkconfig
    assert "voice_wakeup_has_wakenet_model" in wakeup
    assert "No WakeNet model loaded" in wakeup
    assert "esp_afe_handle_from_config" in wakeup
    assert "afe_handle->create_from_config" in wakeup
    assert "afe_handle->feed" in wakeup
    assert "afe_handle->fetch" in wakeup
    assert "WAKENET_DETECTED" in wakeup
    assert "APP_EVENT_VOICE_WAKEUP" in wakeup
    assert "voice_wakeup_init(event_queue)" in main
    assert "voice_wakeup_start()" in main
    reconnect_block = main.split("if (!xiaozhi_client_is_connected())", 1)[1].split(
        "return start_ret;", 1
    )[0]
    assert reconnect_block.index("voice_wakeup_stop()") < reconnect_block.index(
        "xiaozhi_client_start()"
    )
    wifi_connected_block = main.split("event.type == APP_EVENT_WIFI_CONNECTED", 1)[1].split(
        "event.type == APP_EVENT_WIFI_DISCONNECTED", 1
    )[0]
    assert "xiaozhi_client_start()" in wifi_connected_block
    assert "start_xiaozhi_listening()" not in wifi_connected_block
    assert "XiaoZhi auto listen skipped; WakeNet stays armed" in wifi_connected_block
    assert "XiaoZhi channel warming in background without listen" in wifi_connected_block
    ai_ready_block = main.split("event.type == APP_EVENT_AI_RESPONSE_READY", 1)[1].split(
        "event.type == APP_EVENT_AI_REQUEST_FAILED", 1
    )[0]
    assert "WakeNet restart after AI response failed" not in ai_ready_block
    assert "start_xiaozhi_listening()" not in ai_ready_block
    assert "xiaozhi_audio_stream_start" not in ai_ready_block
    assert "event.type == APP_EVENT_VOICE_WAKEUP" in main
    assert "start_xiaozhi_listening()" in main
    assert "s_pending_wakeup_listen = true" not in main
    assert "s_pending_wakeup_listen = false" in main
    assert "XiaoZhi ready keeps WakeNet armed; waiting for wake word" in main
    assert "XiaoZhi channel closed before WakeNet rearm" in main
    assert "XiaoZhi response finished, WakeNet rearmed" in main
    assert "voice_wakeup_is_running()" in main
    assert "XiaoZhi listen stopped after TTS stop" in protocol
    assert "xiaozhi_post_event(APP_EVENT_AI_RESPONSE_READY, \"XiaoZhi TTS done\")" in protocol
    assert "VOICE_WAKEUP_STOP_WAIT_MS 1500" in wakeup
    assert "WakeNet stopped cleanly" in wakeup
    assert "snapshot->voice_wakeup_detected" in oled
    assert "WakeNet" in readme
    assert "idf.py flash srmodels" in readme


def test_xiaozhi_local_music_fallback_avoids_cloud_search_and_send_contention():
    protocol = read("ai/src/xiaozhi_protocol.c")

    assert "xiaozhi_protocol_handle_stt_text" in protocol
    assert "xiaozhi_protocol_try_local_music_command" in protocol
    assert "播放" in protocol
    assert "ai_music_control_play_by_name(query)" in protocol
    assert "XiaoZhi STT local music fallback" in protocol
    assert "s_local_music_active" in protocol
    assert "XiaoZhi audio frame skipped during local music" in protocol
    assert "XiaoZhi TTS start ignored during local music" in protocol
    assert "s_local_music_active || s_tts_interrupted" in protocol
    assert "xiaozhi_protocol_send_abort(\"local_music\")" in protocol
    assert "XiaoZhi abort sent for local music" in protocol
    assert "xiaozhi_protocol_close_after_local_music_task" in protocol
    assert "XiaoZhi audio channel closed after local music takeover" in protocol
    assert "XIAOZHI_LOCAL_MUSIC_CLOSE_DELAY_MS" in protocol
    tts_start_block = protocol.split('strcmp(state->valuestring, "start") == 0', 1)[1].split(
        '} else if (cJSON_IsString(state) && strcmp(state->valuestring, "stop") == 0)', 1
    )[0]
    assert "xiaozhi_audio_stream_stop()" in tts_start_block
    assert "XiaoZhi audio stream stopped before TTS playback" in tts_start_block

    tools_list_block = protocol.split('strcmp(method->valuestring, "tools/list") == 0', 1)[1].split(
        '} else if (strcmp(method->valuestring, "tools/call") == 0)', 1
    )[0]
    assert "xiaozhi_audio_stream_stop()" in tools_list_block
    assert "xiaozhi_protocol_resume_audio_after_mcp_tools_list()" in tools_list_block
    assert "XiaoZhi listen resumed after MCP tools/list" not in protocol
    assert "XiaoZhi audio stream resumed after MCP tools/list" in protocol

    send_audio = protocol.split("esp_err_t xiaozhi_protocol_send_audio", 1)[1].split(
        "esp_err_t xiaozhi_protocol_start_listening", 1
    )[0]
    assert "pdMS_TO_TICKS(XIAOZHI_SEND_AUDIO_LOCK_TIMEOUT_MS)" in send_audio
    assert "xiaozhi_audio_stream_is_stopping()" in send_audio
    assert "XiaoZhi audio frame send ignored during stream stop" in send_audio
    assert "XIAOZHI_SEND_AUDIO_TIMEOUT_MS 250" in protocol

    stream_header = read("ai/include/xiaozhi_audio_stream.h")
    stream = read("ai/src/xiaozhi_audio_stream.c")
    assert "xiaozhi_audio_stream_is_stopping" in stream_header
    assert "s_sending" in stream
    assert "XIAOZHI_AUDIO_STREAM_STOP_WAIT_MS" in stream


if __name__ == "__main__":
    test_phase1_modules_and_entrypoint_are_present()
    test_phase1_configuration_symbols_are_declared()
    test_oled_status_page_renders_core_fields()
    test_oled_uses_page_addressing_for_page_flush()
    test_oled_uses_conservative_i2c_transfer_settings()
    test_oled_turns_display_on_after_initial_clear()
    test_gesture_sensor_starts_before_oled_display()
    test_gesture_sensor_uses_falling_edge_interrupt()
    test_gesture_sensor_uses_background_recovery_init()
    test_audio_input_uses_inmp441_i2s_rx_settings()
    test_opus_encoder_prepares_inmp441_pcm_for_xiaozhi()
    test_xiaozhi_tts_player_decodes_opus_to_max98357a()
    test_audio_output_uses_max98357a_i2s_tx_settings()
    test_gesture_control_mode_maps_gestures_to_actions()
    test_audio_player_reads_wav_from_sd_and_uses_pcm_output()
    test_audio_http_player_streams_mp3_from_url()
    test_audio_music_player_dispatches_http_mp3_and_wav_urls()
    test_xiaozhi_can_control_http_music_playback()
    test_audio_prompt_system_uses_short_tone_sequences()
    test_ai_voice_link_is_reserved_after_recording_stops()
    test_ai_voice_http_test_path_is_removed()
    test_xiaozhi_websocket_client_is_reserved_for_protocol_probe()
    test_wifi_manager_reserves_ble_provisioning_flow()
    test_storage_sd_uses_1_bit_sdmmc_and_rw_probe()
    test_audio_recorder_writes_wav_files_to_sd_card()
    test_main_long_press_restarts_ble_provisioning()
    test_music_library_generator_updates_json_and_c_library()
    test_voice_wakeup_uses_esp_sr_wakenet_and_starts_xiaozhi()
    test_xiaozhi_local_music_fallback_avoids_cloud_search_and_send_contention()
