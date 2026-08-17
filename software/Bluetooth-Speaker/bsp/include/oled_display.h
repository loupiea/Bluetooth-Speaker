#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t oled_display_init(void);
void oled_display_task(void *arg);

#ifdef __cplusplus
}
#endif
