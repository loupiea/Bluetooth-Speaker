#include "oled_display.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include "app_state.h"
#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "i2c_bus.h"
#include "sdkconfig.h"

#define OLED_WIDTH 128
#define OLED_HEIGHT 64
#define OLED_PAGES (OLED_HEIGHT / 8)
#define OLED_I2C_FREQ_HZ 100000
#define OLED_DATA_CHUNK_SIZE 16
#define OLED_WRITE_RETRY_COUNT 3
#define OLED_CMD_CONTROL 0x00
#define OLED_DATA_CONTROL 0x40

static const char *TAG = "oled";
static i2c_master_dev_handle_t s_oled_dev;
static uint8_t s_framebuffer[OLED_WIDTH * OLED_PAGES];

static esp_err_t oled_write(const uint8_t *data, size_t len)
{
    esp_err_t ret = ESP_OK;
    for (int attempt = 1; attempt <= OLED_WRITE_RETRY_COUNT; ++attempt) {
        ret = i2c_bus_lock(pdMS_TO_TICKS(300));
        if (ret == ESP_OK) {
            ret = i2c_master_transmit(s_oled_dev, data, len, pdMS_TO_TICKS(250));
            i2c_bus_unlock();
        }
        if (ret == ESP_OK) {
            return ESP_OK;
        }
        ESP_LOGD(TAG, "I2C write failed on attempt %d/%d: %s",
                 attempt, OLED_WRITE_RETRY_COUNT, esp_err_to_name(ret));
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return ret;
}

static esp_err_t oled_command(uint8_t command)
{
    uint8_t tx[] = { OLED_CMD_CONTROL, command };
    return oled_write(tx, sizeof(tx));
}

static esp_err_t oled_data(const uint8_t *data, size_t len)
{
    if (len > OLED_WIDTH || data == NULL) {
        return ESP_ERR_INVALID_SIZE;
    }

    size_t offset = 0;
    while (offset < len) {
        size_t chunk_len = len - offset;
        if (chunk_len > OLED_DATA_CHUNK_SIZE) {
            chunk_len = OLED_DATA_CHUNK_SIZE;
        }

        uint8_t line[OLED_DATA_CHUNK_SIZE + 1] = { OLED_DATA_CONTROL };
        memcpy(&line[1], &data[offset], chunk_len);
        ESP_RETURN_ON_ERROR(oled_write(line, chunk_len + 1), TAG, "data chunk failed");
        offset += chunk_len;
    }

    return ESP_OK;
}

static const uint8_t *font5x7_get(char ch)
{
    static const uint8_t blank[5] = { 0x00, 0x00, 0x00, 0x00, 0x00 };
    static const uint8_t punct[][5] = {
        { 0x00, 0x36, 0x36, 0x00, 0x00 }, // :
        { 0x08, 0x08, 0x08, 0x08, 0x08 }, // -
        { 0x20, 0x10, 0x08, 0x04, 0x02 }, // /
        { 0x08, 0x08, 0x3E, 0x08, 0x08 }, // +
    };
    static const uint8_t digits[10][5] = {
        { 0x3E, 0x51, 0x49, 0x45, 0x3E },
        { 0x00, 0x42, 0x7F, 0x40, 0x00 },
        { 0x42, 0x61, 0x51, 0x49, 0x46 },
        { 0x21, 0x41, 0x45, 0x4B, 0x31 },
        { 0x18, 0x14, 0x12, 0x7F, 0x10 },
        { 0x27, 0x45, 0x45, 0x45, 0x39 },
        { 0x3C, 0x4A, 0x49, 0x49, 0x30 },
        { 0x01, 0x71, 0x09, 0x05, 0x03 },
        { 0x36, 0x49, 0x49, 0x49, 0x36 },
        { 0x06, 0x49, 0x49, 0x29, 0x1E },
    };
    static const uint8_t upper[26][5] = {
        { 0x7E, 0x11, 0x11, 0x11, 0x7E },
        { 0x7F, 0x49, 0x49, 0x49, 0x36 },
        { 0x3E, 0x41, 0x41, 0x41, 0x22 },
        { 0x7F, 0x41, 0x41, 0x22, 0x1C },
        { 0x7F, 0x49, 0x49, 0x49, 0x41 },
        { 0x7F, 0x09, 0x09, 0x09, 0x01 },
        { 0x3E, 0x41, 0x49, 0x49, 0x7A },
        { 0x7F, 0x08, 0x08, 0x08, 0x7F },
        { 0x00, 0x41, 0x7F, 0x41, 0x00 },
        { 0x20, 0x40, 0x41, 0x3F, 0x01 },
        { 0x7F, 0x08, 0x14, 0x22, 0x41 },
        { 0x7F, 0x40, 0x40, 0x40, 0x40 },
        { 0x7F, 0x02, 0x0C, 0x02, 0x7F },
        { 0x7F, 0x04, 0x08, 0x10, 0x7F },
        { 0x3E, 0x41, 0x41, 0x41, 0x3E },
        { 0x7F, 0x09, 0x09, 0x09, 0x06 },
        { 0x3E, 0x41, 0x51, 0x21, 0x5E },
        { 0x7F, 0x09, 0x19, 0x29, 0x46 },
        { 0x46, 0x49, 0x49, 0x49, 0x31 },
        { 0x01, 0x01, 0x7F, 0x01, 0x01 },
        { 0x3F, 0x40, 0x40, 0x40, 0x3F },
        { 0x1F, 0x20, 0x40, 0x20, 0x1F },
        { 0x3F, 0x40, 0x38, 0x40, 0x3F },
        { 0x63, 0x14, 0x08, 0x14, 0x63 },
        { 0x07, 0x08, 0x70, 0x08, 0x07 },
        { 0x61, 0x51, 0x49, 0x45, 0x43 },
    };

    if (ch >= 'a' && ch <= 'z') {
        ch = (char)toupper((unsigned char)ch);
    }
    if (ch >= 'A' && ch <= 'Z') {
        return upper[ch - 'A'];
    }
    if (ch >= '0' && ch <= '9') {
        return digits[ch - '0'];
    }
    if (ch == ':') {
        return punct[0];
    }
    if (ch == '-') {
        return punct[1];
    }
    if (ch == '/') {
        return punct[2];
    }
    if (ch == '+') {
        return punct[3];
    }
    return blank;
}

static void framebuffer_clear(void)
{
    memset(s_framebuffer, 0, sizeof(s_framebuffer));
}

static void framebuffer_set_pixel(int x, int y, bool on)
{
    if (x < 0 || x >= OLED_WIDTH || y < 0 || y >= OLED_HEIGHT) {
        return;
    }

    uint8_t *cell = &s_framebuffer[(y / 8) * OLED_WIDTH + x];
    uint8_t mask = (uint8_t)(1U << (y % 8));
    if (on) {
        *cell |= mask;
    } else {
        *cell &= (uint8_t)~mask;
    }
}

static void framebuffer_vline(int x, int y, int height)
{
    for (int row = 0; row < height; ++row) {
        framebuffer_set_pixel(x, y + row, true);
    }
}

static void framebuffer_rect(int x, int y, int width, int height)
{
    for (int col = 0; col < width; ++col) {
        framebuffer_set_pixel(x + col, y, true);
        framebuffer_set_pixel(x + col, y + height - 1, true);
    }
    for (int row = 0; row < height; ++row) {
        framebuffer_set_pixel(x, y + row, true);
        framebuffer_set_pixel(x + width - 1, y + row, true);
    }
}

static void framebuffer_fill_rect(int x, int y, int width, int height)
{
    for (int row = 0; row < height; ++row) {
        for (int col = 0; col < width; ++col) {
            framebuffer_set_pixel(x + col, y + row, true);
        }
    }
}

static void framebuffer_text(int x, int page, const char *text)
{
    if (page < 0 || page >= OLED_PAGES || text == NULL) {
        return;
    }

    int cursor = x;
    while (*text != '\0' && cursor < OLED_WIDTH - 6) {
        const uint8_t *glyph = font5x7_get(*text++);
        for (int col = 0; col < 5; ++col) {
            s_framebuffer[page * OLED_WIDTH + cursor++] = glyph[col];
        }
        s_framebuffer[page * OLED_WIDTH + cursor++] = 0x00;
    }
}

static void framebuffer_hline(int page)
{
    if (page < 0 || page >= OLED_PAGES) {
        return;
    }

    memset(&s_framebuffer[page * OLED_WIDTH], 0x01, OLED_WIDTH);
}

static void framebuffer_icon_wifi(int x, int y, bool connected)
{
    framebuffer_set_pixel(x + 4, y + 7, true);
    if (connected) {
        framebuffer_set_pixel(x + 2, y + 5, true);
        framebuffer_set_pixel(x + 3, y + 4, true);
        framebuffer_set_pixel(x + 4, y + 4, true);
        framebuffer_set_pixel(x + 5, y + 4, true);
        framebuffer_set_pixel(x + 6, y + 5, true);
        framebuffer_set_pixel(x, y + 3, true);
        framebuffer_set_pixel(x + 1, y + 2, true);
        framebuffer_set_pixel(x + 7, y + 2, true);
        framebuffer_set_pixel(x + 8, y + 3, true);
    } else {
        framebuffer_vline(x + 1, y + 2, 5);
        framebuffer_vline(x + 7, y + 2, 5);
    }
}

static void framebuffer_icon_storage(int x, int y, bool ready)
{
    framebuffer_rect(x + 1, y + 1, 8, 7);
    framebuffer_fill_rect(x + 3, y, 4, 2);
    if (ready) {
        framebuffer_fill_rect(x + 3, y + 4, 4, 2);
    } else {
        framebuffer_set_pixel(x + 3, y + 3, true);
        framebuffer_set_pixel(x + 4, y + 4, true);
        framebuffer_set_pixel(x + 5, y + 5, true);
    }
}

static void framebuffer_icon_ai(int x, int y, bool active)
{
    framebuffer_rect(x, y + 1, 10, 6);
    framebuffer_set_pixel(x + 2, y + 7, true);
    framebuffer_set_pixel(x + 3, y + 8, true);
    framebuffer_set_pixel(x + 3, y + 4, true);
    framebuffer_set_pixel(x + 6, y + 4, true);
    if (active) {
        framebuffer_fill_rect(x + 4, y + 3, 2, 2);
    }
}

static void framebuffer_icon_microphone(int x, int y)
{
    framebuffer_rect(x + 5, y, 6, 10);
    framebuffer_vline(x + 4, y + 3, 5);
    framebuffer_vline(x + 11, y + 3, 5);
    framebuffer_vline(x + 8, y + 10, 4);
    framebuffer_fill_rect(x + 5, y + 14, 7, 2);
}

static void framebuffer_icon_play(int x, int y)
{
    for (int row = 0; row < 16; ++row) {
        int width = row < 8 ? row + 1 : 16 - row;
        for (int col = 0; col < width; ++col) {
            framebuffer_set_pixel(x + col, y + row, true);
        }
    }
}

static void framebuffer_icon_alert(int x, int y)
{
    for (int row = 0; row < 14; ++row) {
        int half = row / 2;
        framebuffer_vline(x + 8 - half, y + row, 1);
        framebuffer_vline(x + 8 + half, y + row, 1);
    }
    framebuffer_vline(x + 8, y + 5, 5);
    framebuffer_set_pixel(x + 8, y + 12, true);
    framebuffer_fill_rect(x + 2, y + 14, 13, 2);
}

static void framebuffer_volume_bar(int x, int y, uint8_t volume)
{
    const int width = 32;
    int fill = (volume > 100 ? 100 : volume) * (width - 2) / 100;
    framebuffer_rect(x, y, width, 7);
    if (fill > 0) {
        framebuffer_fill_rect(x + 1, y + 1, fill, 5);
    }
}

static void oled_render_status_bar(const app_state_snapshot_t *snapshot)
{
    framebuffer_text(0, 0, "XIAOZHI");
    framebuffer_icon_wifi(82, 0, snapshot->wifi_status == APP_WIFI_CONNECTED);
    framebuffer_icon_storage(97, 0, snapshot->storage_ready);
    framebuffer_icon_ai(113, 0, snapshot->ai_status == APP_AI_PENDING ||
                                snapshot->ai_status == APP_AI_READY);
    framebuffer_hline(1);
}

static void oled_render_hero(const app_state_snapshot_t *snapshot)
{
    const char *label = "READY";

    if (snapshot->status == APP_STATUS_ERROR ||
        snapshot->playback_status == APP_PLAYBACK_ERROR ||
        snapshot->ai_status == APP_AI_FAILED) {
        framebuffer_icon_alert(6, 22);
        label = "CHECK";
    } else if (snapshot->recording) {
        framebuffer_icon_microphone(6, 20);
        label = "REC";
    } else if (snapshot->playback_status == APP_PLAYBACK_PLAYING) {
        framebuffer_icon_play(7, 20);
        label = "PLAY";
    } else if (snapshot->voice_wakeup_detected) {
        framebuffer_icon_ai(9, 23, true);
        label = "WAKE";
    } else if (snapshot->ai_status == APP_AI_PENDING) {
        framebuffer_icon_ai(9, 23, true);
        label = "ASK";
    } else if (snapshot->wifi_status == APP_WIFI_PROVISIONING ||
               snapshot->wifi_status == APP_WIFI_CONNECTING) {
        framebuffer_icon_wifi(10, 24, true);
        label = "SETUP";
    } else {
        framebuffer_rect(7, 22, 16, 16);
        framebuffer_fill_rect(12, 27, 6, 6);
    }

    framebuffer_text(34, 3, label);
    framebuffer_text(34, 4, app_wifi_status_to_string(snapshot->wifi_status));
    framebuffer_volume_bar(86, 30, snapshot->speaker_volume);
    framebuffer_text(86, 5, "VOL");
}

static void oled_render_interaction_bar(const app_state_snapshot_t *snapshot)
{
    char line[32];
    snprintf(line, sizeof(line), "G:%s B:%s",
             app_gesture_to_string(snapshot->last_gesture),
             app_button_action_to_string(snapshot->last_button));
    framebuffer_text(0, 7, line);
}

static esp_err_t oled_flush(void)
{
    for (int page = 0; page < OLED_PAGES; ++page) {
        ESP_RETURN_ON_ERROR(oled_command(0xB0 + page), TAG, "page set failed");
        ESP_RETURN_ON_ERROR(oled_command(0x00), TAG, "low column failed");
        ESP_RETURN_ON_ERROR(oled_command(0x10), TAG, "high column failed");
        ESP_RETURN_ON_ERROR(oled_data(&s_framebuffer[page * OLED_WIDTH], OLED_WIDTH),
                            TAG, "data failed");
    }
    return ESP_OK;
}

static void oled_render_state(void)
{
    app_state_snapshot_t snapshot = { 0 };

    app_state_get_snapshot(&snapshot);
    framebuffer_clear();
    oled_render_status_bar(&snapshot);
    oled_render_hero(&snapshot);
    oled_render_interaction_bar(&snapshot);
}

esp_err_t oled_display_init(void)
{
    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = CONFIG_SMART_SPEAKER_OLED_I2C_ADDR,
        .scl_speed_hz = OLED_I2C_FREQ_HZ,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(i2c_bus_get_handle(), &dev_config, &s_oled_dev),
                        TAG, "add oled failed");

    vTaskDelay(pdMS_TO_TICKS(100));

    const uint8_t init_commands[] = {
        0xAE, 0x20, 0x02, 0xB0, 0xC8, 0x00, 0x10, 0x40,
        0x81, 0x7F, 0xA1, 0xA6, 0xA8, 0x3F, 0xA4, 0xD3,
        0x00, 0xD5, 0x80, 0xD9, 0xF1, 0xDA, 0x12, 0xDB,
        0x40, 0x8D, 0x14,
    };

    for (size_t i = 0; i < sizeof(init_commands); ++i) {
        ESP_RETURN_ON_ERROR(oled_command(init_commands[i]), TAG, "init command failed");
    }

    vTaskDelay(pdMS_TO_TICKS(50));

    framebuffer_clear();
    ESP_RETURN_ON_ERROR(oled_flush(), TAG, "initial flush failed");
    ESP_RETURN_ON_ERROR(oled_command(0xAF), TAG, "display on failed");
    ESP_LOGI(TAG, "SSD1306 ready at 0x%02X", CONFIG_SMART_SPEAKER_OLED_I2C_ADDR);
    return ESP_OK;
}

void oled_display_task(void *arg)
{
    (void)arg;
    while (true) {
        oled_render_state();
        esp_err_t ret = oled_flush();
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "OLED refresh failed: %s", esp_err_to_name(ret));
        }
        vTaskDelay(pdMS_TO_TICKS(CONFIG_SMART_SPEAKER_OLED_REFRESH_MS));
    }
}
