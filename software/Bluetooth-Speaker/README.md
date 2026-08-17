# ESP32-S3 Smart Speaker

ESP32-S3 smart speaker project based on ESP-IDF.

Current focus is the XiaoZhi voice path on top of the validated local interaction, storage, recording, playback, Wi-Fi, and BLE provisioning foundation.

## Hardware

- MCU: ESP32-S3
- Gesture sensor: PAJ7620, I2C address `0x73`
- OLED: SSD1306, I2C address `0x3C`
- Microphone: INMP441-compatible I2S board
- Local storage: SDMMC card socket, 1-bit mode
- Speaker amplifier: MAX98357A, I2S input
- I2C SDA: `GPIO8`
- I2C SCL: `GPIO9`
- PAJ7620 INT: `GPIO15`, falling-edge interrupt
- I2S BCLK: `GPIO11`
- I2S LRCLK: `GPIO12`
- I2S DOUT: `GPIO10`
- INMP441 BCLK/SCK: `GPIO16`
- INMP441 LRCLK/WS: `GPIO17`
- INMP441 DIN/SD: `GPIO18`
- INMP441 L/R: `GND`, left channel
- SDMMC CLK: `GPIO14`
- SDMMC CMD: `GPIO21`
- SDMMC D0: `GPIO13`
- SDMMC CD: `GPIO38`
- SDMMC D1/D2/D3: not connected
- Button main function: `GPIO4`
- Button back/mute: `GPIO5`
- Button volume up: `GPIO6`
- Button volume down: `GPIO7`
- Button main long press: clear saved Wi-Fi credentials and restart BLE provisioning
- Power: use `3.3V` and common `GND`

ESP32-S3 supports Wi-Fi and Bluetooth LE. Classic Bluetooth A2DP music playback is not part of the first version.

## Wi-Fi Provisioning Plan

Wi-Fi setup will use BLE provisioning so the device can move between different Wi-Fi networks without changing code or reflashing firmware.

Default boot flow:

```text
1. Read saved Wi-Fi credentials from NVS.
2. If connection succeeds, enter normal app mode.
3. If connection fails or no credentials exist, enter BLE provisioning mode.
4. A phone connects over BLE and sends Wi-Fi SSID/password.
5. ESP32-S3 connects to Wi-Fi and saves the credentials for future boots.
```

Future Wi-Fi related code belongs under `wifi/`:

```text
wifi/include/
wifi/src/
```

Planned module boundary:

```c
esp_err_t wifi_manager_init(QueueHandle_t event_queue);
esp_err_t wifi_manager_start_auto_connect(void);
bool wifi_manager_is_connected(void);
esp_err_t wifi_manager_start_provisioning(void);
esp_err_t wifi_manager_clear_credentials(void);
esp_err_t wifi_manager_save_credentials(const char *ssid, const char *password);
esp_err_t wifi_manager_load_credentials(char *ssid,
                                        size_t ssid_size,
                                        char *password,
                                        size_t password_size);
```

Current Wi-Fi baseline:

- `wifi_manager` initializes NVS during app startup.
- `wifi_manager` initializes ESP-IDF Wi-Fi STA, `esp_netif`, event loop, and BLE provisioning manager.
- Saved credentials use NVS namespace `wifi`.
- Saved keys are `ssid` and `password`.
- Boot flow now really checks NVS first. If credentials exist, the app starts Wi-Fi STA connection; if not, it starts BLE provisioning.
- BLE provisioning uses ESP-IDF `wifi_prov_mgr` with the BLE transport scheme.
- Credentials received over BLE are saved through `wifi_manager_save_credentials()` for future boots.
- `wifi_manager_clear_credentials()` clears the local NVS copy and returns the app to provisioning state.
- BLE provisioning service name: `SmartSpeaker`.
- BLE provisioning POP: set a deployment-specific value in `menuconfig`.
- Hold the main button for 2 seconds to clear saved Wi-Fi credentials and start BLE provisioning again.

Because Wi-Fi, BLE, audio codecs, and WakeNet increase firmware size, the project uses a custom 16 MB flash partition table in `partitions.csv` with a 4 MB factory app partition and a `model` data partition for ESP-SR models.

## Voice Wakeup Plan

Voice wakeup uses Espressif ESP-SR WakeNet on ESP32-S3. This is real local wake-word detection, not a microphone volume threshold. The `voice_wakeup` module reads INMP441 audio, feeds ESP-SR AFE/WakeNet, and posts `APP_EVENT_VOICE_WAKEUP` when WakeNet reports `WAKENET_DETECTED`.

After a wake word is detected, the main loop reuses the existing XiaoZhi path:

```text
WakeNet detected
-> APP_EVENT_VOICE_WAKEUP
-> start_xiaozhi_listening()
-> XiaoZhi WebSocket listen start
-> INMP441 audio stream to XiaoZhi
```

WakeNet and XiaoZhi streaming both use the same I2S microphone, so the app stops WakeNet before starting recording or XiaoZhi live audio. WakeNet restarts after recording, AI response, or AI failure.

The default model partition label is `model`:

```text
CONFIG_SMART_SPEAKER_VOICE_WAKEUP_MODEL_PARTITION="model"
```

After changing ESP-SR models or flashing a clean board, flash both firmware and speech models:

```powershell
idf.py build
idf.py flash srmodels
idf.py monitor
```

The wake word depends on the WakeNet model selected by ESP-SR model configuration, for example a XiaoZhi-style model such as `你好小智` when that model is included in the flashed srmodels image.

Planned status events:

```text
APP_EVENT_WIFI_CONNECTING
APP_EVENT_WIFI_CONNECTED
APP_EVENT_WIFI_DISCONNECTED
APP_EVENT_WIFI_PROVISIONING
APP_EVENT_WIFI_FAILED
```

## Audio Plan

MAX98357A is an I2S digital amplifier. It does not provide an I2C volume register, so speaker volume will be handled in software by scaling PCM samples before writing them to I2S.

The first speaker validation step initializes I2S TX and plays audio through MAX98357A. Local SD WAV playback remains available as a module, but the current music playback path is HTTP MP3 streaming from a computer/NAS/server URL. Press `GPIO5` while idle to start the configured HTTP MP3 test track. Press `GPIO5` again while playback is active to stop it. Press `GPIO6` or `GPIO7` to adjust software volume; a short feedback tone plays only when playback is idle.

Speaker playback now exposes a common mono 16-bit PCM output API:

```c
esp_err_t audio_output_write_pcm(const int16_t *samples, size_t sample_count);
```

The output module scales samples by the current software volume, duplicates mono samples to left/right I2S slots, writes a short silence tail, and disables I2S TX when playback ends.

The WAV player module runs as a background task. It reads standard mono 16-bit PCM WAV files from the SD card and streams them through `audio_output_write_pcm` while the main event loop remains responsive to buttons and gestures.

The HTTP MP3 player module also runs as a background task. It uses ESP-IDF `esp_http_client` to read MP3 data from the local music server and Espressif `esp_audio_codec` Simple Decoder to parse and decode MP3 frames from arbitrary-size HTTP chunks. Decoded PCM is downmixed/resampled to the project output format before being written through `audio_output_write_pcm`.

Recording and playback are mutually exclusive. If WAV or HTTP MP3 playback is active, `GPIO4` will not start recording. If recording is active, `GPIO5` will not start playback.

The prompt module plays short tone sequences for local feedback. It currently covers recording start, recording stop, playback start, playback stop, volume changes, and error feedback.

The AI voice link now keeps only the app-level request state. After a recording stops, the app enters `AI PEND`; the real cloud path is handled by the XiaoZhi WebSocket protocol module.

## XiaoZhi WebSocket

The XiaoZhi path uses the official device flow:

```text
Wi-Fi connected
-> POST OTA discovery endpoint
-> receive websocket.url / websocket.token / version
-> open device WebSocket
-> send hello
-> receive server hello and audio params
-> reply to MCP initialize and tools/list
```

Current validated state:

```text
I xiaozhi: XiaoZhi OTA websocket discovered: url=wss://... token=set version=1
I xiaozhi: XiaoZhi WebSocket connected
I xiaozhi: XiaoZhi hello sent: version=1
I xiaozhi: XiaoZhi session id: ...
I xiaozhi: XiaoZhi MCP initialize replied: id=1
I xiaozhi: XiaoZhi MCP tools/list replied: id=2
```

The next XiaoZhi step is real voice streaming: send listen start/stop events, encode microphone PCM as Opus frames, send audio frames to the WebSocket, then decode/play returned Opus audio through MAX98357A.

Playback status is reflected on the OLED speaker line:

```text
SD OK AI PEND
SPK OK IDLE V 10
SPK OK PLAY V 10
SPK OK ERR  V 10
```

INMP441 is an I2S digital microphone. The microphone module now keeps only the shared I2S RX initialization and sample-read API used by WAV recording and the future AI voice pipeline.

The SD card socket uses 1-bit SDMMC mode. Phase 2 storage validation mounts the card at `/sdcard`, writes `/sdcard/hello.txt`, then reads it back to verify the hardware path and FAT filesystem.

The recorder module saves INMP441 audio as standard WAV files on the SD card. Press `GPIO4` once to start recording, and press it again to stop. Files are written under `/sdcard/recordings` as `rec_001.wav`, `rec_002.wav`, and so on. Recording is limited by `CONFIG_SMART_SPEAKER_RECORDER_MAX_SECONDS` to avoid filling the card during tests.

Planned API:

```c
void audio_set_volume(uint8_t volume_percent);
uint8_t audio_get_volume(void);
```

Gesture mapping planned for Phase 2:

```text
UP: volume up
DOWN: volume down
FORWARD: wake or confirm
BACKWARD: mute or cancel
```

## Button Plan

Four hardware buttons are connected to GPIOs with falling-edge interrupts. Hardware debounce is already handled on the board.

```text
GPIO4: main function, wake, confirm, play/pause
GPIO5: back, cancel, mute
GPIO6: volume up
GPIO7: volume down
```

Planned button action names:

```c
BUTTON_ACTION_MAIN
BUTTON_ACTION_BACK_MUTE
BUTTON_ACTION_VOLUME_UP
BUTTON_ACTION_VOLUME_DOWN
```

## Project Layout

- `main/`: application entry point only
- `ai/include/`: AI voice link public headers
- `ai/src/`: AI voice link implementations
- `wifi/include/`: Wi-Fi and BLE provisioning public headers
- `wifi/src/`: Wi-Fi and BLE provisioning implementations
- `bsp/include/`: module public headers
- `bsp/src/`: module implementations
- `bsp/Kconfig.projbuild`: project configuration items
- `sdkconfig`: project configuration
- `tests/`: lightweight structure checks

Keep feature modules inside `bsp`. Keep `main/main.c` focused on initialization, task startup, and top-level event handling.
Keep AI-related code inside `ai`.
Keep Wi-Fi and BLE provisioning code inside `wifi`.

## Current Modules

- `i2c_bus`: shared I2C bus setup
- `app_events`: common event definitions
- `app_state`: boot, idle, gesture, and error state tracking
- `ai_voice`: app-level AI request state bridge for the XiaoZhi voice path
- `ai_music_control`: XiaoZhi AI command bridge for local HTTP music playback
- `xiaozhi_protocol`: XiaoZhi OTA discovery, device WebSocket, hello, and MCP handshake
- `buttons`: falling-edge hardware button input
- `audio_input`: INMP441 I2S microphone input for recording and AI capture
- `audio_output`: MAX98357A I2S speaker output and test tone playback
- `audio_player`: SD card WAV playback through MAX98357A
- `audio_http_player`: HTTP MP3 streaming playback through MAX98357A
- `audio_prompt`: short local tone prompts through MAX98357A
- `audio_recorder`: INMP441 to SD card WAV recording
- `storage_sd`: 1-bit SDMMC mount and write/read probe
- `wifi_manager`: NVS Wi-Fi credential storage plus reserved auto-connect and BLE provisioning state manager
- `paj7620`: gesture sensor initialization and interrupt-driven gesture reads
- `oled_display`: SSD1306 status display

OLED initialization is optional at runtime: if the OLED is not connected, the app logs a warning and continues running the gesture module.

OLED and PAJ7620 share the same I2C bus on `GPIO8/GPIO9`. The bus is initialized once by `i2c_bus`; each module only registers its own I2C device address.

PAJ7620 gesture recognition uses the module interrupt pin on `GPIO15`. The GPIO is configured as a falling-edge interrupt input, and the gesture task reads the PAJ7620 gesture registers only after an interrupt notification.

Battery power can ramp more slowly than a USB reset. To avoid PAJ7620 initialization racing the sensor power-on sequence, the driver waits briefly before the first attempt, then a background task retries until PAJ7620 initializes. OLED can show `PAJ NONE` during recovery and `PAJ OK` once ready.

The OLED driver uses conservative settings validated on hardware:

```text
SSD1306 address mode: page addressing
OLED I2C clock: 100000 Hz
OLED data chunk: 16 bytes per transmit
OLED write retries: 3
Startup order: PAJ7620 first, OLED second
```

## Configuration

Project configuration is kept in `sdkconfig`:

```text
CONFIG_SMART_SPEAKER_I2C_SDA_GPIO=8
CONFIG_SMART_SPEAKER_I2C_SCL_GPIO=9
CONFIG_SMART_SPEAKER_BUTTON_MAIN_GPIO=4
CONFIG_SMART_SPEAKER_BUTTON_BACK_MUTE_GPIO=5
CONFIG_SMART_SPEAKER_BUTTON_VOLUME_UP_GPIO=6
CONFIG_SMART_SPEAKER_BUTTON_VOLUME_DOWN_GPIO=7
CONFIG_SMART_SPEAKER_BUTTON_GUARD_MS=120
CONFIG_SMART_SPEAKER_BUTTON_CONFIRM_MS=20
CONFIG_SMART_SPEAKER_OLED_I2C_ADDR=0x3C
CONFIG_SMART_SPEAKER_INMP441_BCLK_GPIO=16
CONFIG_SMART_SPEAKER_INMP441_LRCLK_GPIO=17
CONFIG_SMART_SPEAKER_INMP441_DIN_GPIO=18
CONFIG_SMART_SPEAKER_INMP441_SAMPLE_RATE_HZ=16000
CONFIG_SMART_SPEAKER_MAX98357A_BCLK_GPIO=11
CONFIG_SMART_SPEAKER_MAX98357A_LRCLK_GPIO=12
CONFIG_SMART_SPEAKER_MAX98357A_DOUT_GPIO=10
CONFIG_SMART_SPEAKER_AUDIO_OUTPUT_SAMPLE_RATE_HZ=16000
CONFIG_SMART_SPEAKER_AUDIO_OUTPUT_DEFAULT_VOLUME=5
CONFIG_SMART_SPEAKER_AUDIO_OUTPUT_TEST_TONE_HZ=1000
CONFIG_SMART_SPEAKER_MUSIC_DEFAULT_URL="http://lubancat.local:8081/music/%E5%80%AA%E8%8E%AB%E9%97%AE%20-%20%E4%B8%8D%E7%81%B5%E4%B8%8D%E7%81%B5.mp3"
CONFIG_SMART_SPEAKER_RECORDER_SAMPLE_RATE_HZ=16000
CONFIG_SMART_SPEAKER_RECORDER_MAX_SECONDS=30
CONFIG_FATFS_LFN_HEAP=y
CONFIG_SMART_SPEAKER_SDMMC_CLK_GPIO=14
CONFIG_SMART_SPEAKER_SDMMC_CMD_GPIO=21
CONFIG_SMART_SPEAKER_SDMMC_D0_GPIO=13
CONFIG_SMART_SPEAKER_SDMMC_CD_GPIO=38
CONFIG_SMART_SPEAKER_SDMMC_MOUNT_POINT="/sdcard"
CONFIG_SMART_SPEAKER_PAJ7620_I2C_ADDR=0x73
CONFIG_SMART_SPEAKER_PAJ7620_I2C_FREQ_HZ=100000
CONFIG_SMART_SPEAKER_PAJ7620_INT_GPIO=15
CONFIG_SMART_SPEAKER_PAJ7620_POWER_ON_DELAY_MS=100
CONFIG_SMART_SPEAKER_PAJ7620_RECOVERY_RETRY_MS=500
CONFIG_SMART_SPEAKER_OLED_REFRESH_MS=500
```

Use `idf.py menuconfig` when changing project settings.

## Build And Flash

```powershell
idf.py build
idf.py flash monitor
```

Expected gesture logs:

```text
I paj7620: PAJ7620 device registered at 0x73, recovery task will initialize sensor
I paj7620: PAJ7620 recovery task waiting 100 ms before first init
W paj7620: PAJ7620 not ready, retry in 500 ms: ...
I paj7620: PAJ7620 ready at 0x73, i2c=100000 Hz, int=GPIO15 falling edge
I paj7620: Gesture interrupt started on GPIO15
I paj7620: Gesture: LEFT
I smart_speaker: State updated from gesture: LEFT
```

Expected OLED log after successful initialization:

```text
I oled: SSD1306 ready at 0x3C
```

Expected microphone logs:

```text
I audio_input: INMP441 ready: BCLK=16 LRCLK=17 DIN=18 sample_rate=16000 Hz
```

Expected SD storage logs:

```text
I storage_sd: Mounting SD card at /sdcard, CLK=14 CMD=21 D0=13 CD=38, 1-bit SDMMC
I storage_sd: SD card mounted: name=... capacity=... MB
I storage_sd: SD write/read probe OK: /sdcard/hello.txt
```

Expected recording logs:

```text
I audio_recorder: WAV recorder ready: dir=/sdcard/recordings sample_rate=16000 max_seconds=30
I smart_speaker: Button action: MAIN
I audio_recorder: Recording started: /sdcard/recordings/rec_001.wav
I audio_recorder: Recording stop requested
I audio_recorder: Recording stopped: /sdcard/recordings/rec_001.wav, bytes=...
I ai_voice: AI request reserved for latest recording
I smart_speaker: AI state: pending
```

Expected speaker and playback status logs:

```text
I audio_output: MAX98357A ready: BCLK=11 LRCLK=12 DOUT=10 sample_rate=16000 Hz volume=5
OLED speaker line changes from `IDLE` to `PLAY`, then back to `IDLE`.
W smart_speaker: Recording busy, playback blocked
W smart_speaker: Playback busy, recording blocked
I smart_speaker: Button action: VOL+
I audio_output: Speaker volume: 40
I audio_output: Tone played: 1200 Hz 45 ms volume=40
```

Expected HTTP MP3 test flow:

```text
Computer or LubanCat A0:
cd music
python -m http.server 8081

ESP32:
I audio_http_player: HTTP MP3 player task ready
I smart_speaker: Button action: BACK/MUTE
I audio_http_player: HTTP MP3 playback requested: http://<LUBANCAT_IP>:8081/...
I smart_speaker: Playback state: started
I audio_http_player: HTTP MP3 info: sample_rate=... bits=16 channels=...
I smart_speaker: Button action: BACK/MUTE
I audio_http_player: HTTP MP3 playback stop requested
I smart_speaker: Playback state: stopped
```

For the HTTP MP3 hardware test, confirm that the LubanCat A0 or development computer allows port `8081`, the ESP32 is on the same LAN, and `GPIO5` starts/stops the configured MP3 URL cleanly.

## Today Progress

- Fixed project path and CMake build setup.
- Moved reusable modules into `bsp/`; kept `main/` for entry code only.
- Added shared I2C bus initialization.
- Added app event queue and state machine.
- Added SSD1306 OLED module.
- Added PAJ7620 module.
- Confirmed PAJ7620 gesture recognition works on hardware.
- Confirmed SSD1306 OLED status page displays normally on hardware.
- Stabilized OLED writes with page addressing, 100 kHz I2C, small data chunks, write retries, and delayed display-on after initial clear.
- Kept I2C bus initialization shared; OLED and PAJ7620 are separate devices on the same bus.
- Started PAJ7620 before OLED so gesture recognition remains the priority if OLED initialization fails.
- Added PAJ7620 power-on delay for battery startup.
- Added PAJ7620 background recovery initialization for battery cold starts.
- Confirmed PAJ7620 can recover from `PAJ NONE` to `PAJ OK` after battery cold boot.
- Added initial INMP441 I2S microphone input and confirmed it works on hardware.
- Removed the temporary microphone level reporting task after the recording path was validated.
- Added 1-bit SDMMC local storage module with `/sdcard` mount and `/sdcard/hello.txt` write/read probe.
- Confirmed SDMMC storage works on hardware at `CLK=GPIO14`, `CMD=GPIO21`, `D0=GPIO13`, `CD=GPIO38`.
- Added first WAV recording module from INMP441 to SD card.
- Mapped the main button `GPIO4` to start/stop recording.
- Added initial MAX98357A I2S speaker output module.
- Added SD card WAV playback module.
- Mapped `GPIO5` to play the newest saved WAV recording from `/sdcard/recordings`.
- Mapped `GPIO5` to stop WAV playback while playback is active.
- Mapped `GPIO6/GPIO7` to adjust software speaker volume and play feedback tones.
- Added common mono 16-bit PCM playback API for future prompts, WAV playback, and AI TTS output.
- Added playback status management for OLED `IDLE` / `PLAY` / `ERR` display.
- Moved WAV playback into a background task so the main event loop stays responsive.
- Added recording/playback mutual exclusion to avoid starting recording during playback or playback during recording.
- Added a short tone prompt system for record, playback, volume, and error feedback.
- Added a reserved AI voice request interface that marks the latest recording as `AI PEND` after recording stops.
- Removed the temporary local upload test path after the XiaoZhi WebSocket path became the main integration route.
- Added the initial `wifi_manager` framework under `wifi/` for future NVS auto-connect and BLE provisioning.
- Added real NVS Wi-Fi credential save/load/clear framework using namespace `wifi` and keys `ssid` / `password`.
- Added ESP-IDF BLE Wi-Fi provisioning through `wifi_prov_mgr` and NimBLE.
- Added a custom 4 MB factory app partition for the larger Wi-Fi/BLE firmware image.
- Simplified configuration by removing temporary OLED/PAJ7620 enable switches.
- Removed extra `sdkconfig.defaults*` files and kept a single `sdkconfig`.
- Limited CMake component selection to avoid compiling unused ESP-IDF components.
- Added XiaoZhi OTA discovery, device WebSocket connect, hello, MCP initialize reply, and tools/list reply.
- Added XiaoZhi listen start/stop from `GPIO4`, INMP441 PCM to Opus audio sending, returned Opus TTS decode, and MAX98357A playback.
- Added official XiaoZhi abort path on `GPIO5` so active listening/TTS can be interrupted.
- Added initial HTTP MP3 music playback module using `esp_http_client` plus Espressif `esp_audio_codec` Simple Decoder.
- Mapped idle `GPIO5` to start the configured HTTP MP3 URL and playback `GPIO5` to stop it.
- Confirmed HTTP MP3 playback works on hardware after registering both default audio decoders and simple decoders.
- Added XiaoZhi MCP tools `music.play_default` and `music.stop` for local HTTP music control.
- Added XiaoZhi text fallback for phrases like `播放音乐`, `放歌`, `停止播放`, and `暂停音乐`.

## Tomorrow Plan

1. Flash and monitor the ESP32-S3 firmware with XiaoZhi music control enabled.
2. Ask XiaoZhi to `播放音乐` and confirm the default HTTP MP3 starts.
3. Ask XiaoZhi to `停止播放` or `暂停音乐` and confirm playback stops.
4. Watch whether XiaoZhi uses MCP `tools/call`; if not, keep the text fallback and tune prompt wording later.
5. If AI-triggered playback overlaps with TTS, add a policy to stop or suppress TTS before starting music.

## Roadmap

1. Phase 1: finish OLED status page and gesture interaction polish.
2. Phase 2: add I2S microphone sampling and I2S speaker/amplifier output.
3. Phase 3: add Wi-Fi connection management and local audio pipeline.
4. Phase 4: complete XiaoZhi voice streaming and response playback.
5. Phase 5: product work, including persistent settings, recovery, enclosure controls, and startup animation.

## Development Notes

- Prefer ESP-IDF C modules for now; do not switch to C++ or ESP-ADF until audio architecture is clear.
- Keep hardware addresses and GPIOs configurable through Kconfig.
- Avoid putting reusable driver logic in `main/`.
- Verify every hardware-facing change with `idf.py build` and serial logs.
