#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t xiaozhi_audio_stream_init(void);
esp_err_t xiaozhi_audio_stream_start(void);
esp_err_t xiaozhi_audio_stream_stop(void);
bool xiaozhi_audio_stream_is_running(void);
bool xiaozhi_audio_stream_is_stopping(void);

#ifdef __cplusplus
}
#endif
