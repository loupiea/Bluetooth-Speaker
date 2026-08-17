#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t audio_input_init(void);
esp_err_t audio_input_read_samples(int32_t *samples,
                                   size_t sample_count,
                                   size_t *samples_read,
                                   TickType_t timeout_ticks);

#ifdef __cplusplus
}
#endif
