#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t paj7620_init(void);
void paj7620_task(void *arg);

#ifdef __cplusplus
}
#endif
