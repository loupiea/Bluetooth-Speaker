#pragma once

#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *name;
    const char *artist;
    const char *url;
} ai_music_track_t;

size_t ai_music_library_count(void);
const ai_music_track_t *ai_music_library_get(size_t index);
const ai_music_track_t *ai_music_library_find(const char *query);
esp_err_t ai_music_library_format_list(char *buffer, size_t buffer_size);

#ifdef __cplusplus
}
#endif
