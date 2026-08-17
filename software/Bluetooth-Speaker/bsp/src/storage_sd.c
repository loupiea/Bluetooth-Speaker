#include "storage_sd.h"

#include <stdio.h>
#include <string.h>
#include "driver/gpio.h"
#include "driver/sdmmc_host.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "sdkconfig.h"

#define STORAGE_SD_TEST_TEXT "smart-speaker sd ok\n"

static const char *TAG = "storage_sd";
static sdmmc_card_t *s_card;
static bool s_mounted;

static esp_err_t storage_sd_probe_file(void)
{
    char path[96] = CONFIG_SMART_SPEAKER_SDMMC_MOUNT_POINT "/hello.txt";
    char buffer[sizeof(STORAGE_SD_TEST_TEXT)] = { 0 };

    FILE *file = fopen(path, "w");
    if (file == NULL) {
        return ESP_FAIL;
    }
    int written = fputs(STORAGE_SD_TEST_TEXT, file);
    fclose(file);
    if (written < 0) {
        return ESP_FAIL;
    }

    file = fopen(path, "r");
    if (file == NULL) {
        return ESP_FAIL;
    }
    char *read_ret = fgets(buffer, sizeof(buffer), file);
    fclose(file);
    if (read_ret == NULL || strcmp(buffer, STORAGE_SD_TEST_TEXT) != 0) {
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t storage_sd_init(void)
{
    if (s_mounted) {
        return ESP_OK;
    }

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.flags = SDMMC_HOST_FLAG_1BIT;

    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.clk = CONFIG_SMART_SPEAKER_SDMMC_CLK_GPIO;
    slot_config.cmd = CONFIG_SMART_SPEAKER_SDMMC_CMD_GPIO;
    slot_config.d0 = CONFIG_SMART_SPEAKER_SDMMC_D0_GPIO;
    slot_config.d1 = GPIO_NUM_NC;
    slot_config.d2 = GPIO_NUM_NC;
    slot_config.d3 = GPIO_NUM_NC;
    slot_config.cd = CONFIG_SMART_SPEAKER_SDMMC_CD_GPIO;
    slot_config.wp = SDMMC_SLOT_NO_WP;
    slot_config.width = 1;
    slot_config.flags = SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    esp_vfs_fat_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 4,
        .allocation_unit_size = 16 * 1024,
    };

    ESP_LOGI(TAG, "Mounting SD card at %s, CLK=%d CMD=%d D0=%d CD=%d, 1-bit SDMMC",
             CONFIG_SMART_SPEAKER_SDMMC_MOUNT_POINT,
             CONFIG_SMART_SPEAKER_SDMMC_CLK_GPIO,
             CONFIG_SMART_SPEAKER_SDMMC_CMD_GPIO,
             CONFIG_SMART_SPEAKER_SDMMC_D0_GPIO,
             CONFIG_SMART_SPEAKER_SDMMC_CD_GPIO);

    ESP_RETURN_ON_ERROR(esp_vfs_fat_sdmmc_mount(CONFIG_SMART_SPEAKER_SDMMC_MOUNT_POINT,
                                                &host, &slot_config, &mount_config, &s_card),
                        TAG, "mount failed");
    s_mounted = true;

    esp_err_t ret = storage_sd_probe_file();
    if (ret != ESP_OK) {
        esp_vfs_fat_sdcard_unmount(CONFIG_SMART_SPEAKER_SDMMC_MOUNT_POINT, s_card);
        s_card = NULL;
        s_mounted = false;
        ESP_RETURN_ON_ERROR(ret, TAG, "hello file write/read probe failed");
    }

    uint64_t capacity_mb = ((uint64_t)s_card->csd.capacity * s_card->csd.sector_size) /
                           (1024ULL * 1024ULL);
    ESP_LOGI(TAG, "SD card mounted: name=%s capacity=%llu MB",
             s_card->cid.name, capacity_mb);
    ESP_LOGI(TAG, "SD write/read probe OK: %s/hello.txt",
             CONFIG_SMART_SPEAKER_SDMMC_MOUNT_POINT);
    return ESP_OK;
}

bool storage_sd_is_mounted(void)
{
    return s_mounted;
}
