// SD card support (FAT over SPI) for the CrowPanel Advance 7". See sdcard.h.

#include "sdcard.h"

#include <string.h>
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "ff.h"
#include "sdmmc_cmd.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "driver/gpio.h"

static const char *TAG = "sdcard";

// SD slot wiring on this board (SPI mode). MOSI=6, MISO=4, CLK=5; the card's
// CS is tied to 3.3V (always selected), so there is no CS GPIO: use NC (-1).
// NOTE: these pins are SHARED with the I2S speaker, switched by the on-board
// S0/S1 keys. The switch must be in the SD position or the card is physically
// off the bus and init will time out (0x108).
#define SD_PIN_MOSI 6
#define SD_PIN_MISO 4
#define SD_PIN_SCK 5
#define SD_PIN_CS GPIO_NUM_NC
#define SD_SPI_HOST SPI2_HOST

static sdmmc_card_t *s_card = NULL;

esp_err_t sdcard_mount(void)
{
    if (s_card) {
        return ESP_OK;
    }

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SD_SPI_HOST;

    spi_bus_config_t bus_cfg = {
        .mosi_io_num = SD_PIN_MOSI,
        .miso_io_num = SD_PIN_MISO,
        .sclk_io_num = SD_PIN_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };
    esp_err_t err = spi_bus_initialize(SD_SPI_HOST, &bus_cfg, SDSPI_DEFAULT_DMA);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_initialize failed: %s", esp_err_to_name(err));
        return err;
    }

    sdspi_device_config_t slot_cfg = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_cfg.gpio_cs = SD_PIN_CS;
    slot_cfg.host_id = SD_SPI_HOST;

    esp_vfs_fat_mount_config_t mount_cfg = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024,
    };

    err = esp_vfs_fat_sdspi_mount(SDCARD_MOUNT_POINT, &host, &slot_cfg, &mount_cfg, &s_card);
    if (err != ESP_OK) {
        if (err == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount FAT (card unformatted or absent?)");
        } else {
            ESP_LOGE(TAG, "Failed to init SD card: %s", esp_err_to_name(err));
        }
        spi_bus_free(SD_SPI_HOST);
        s_card = NULL;
        return err;
    }

    ESP_LOGI(TAG, "SD card mounted at %s", SDCARD_MOUNT_POINT);
    sdmmc_card_print_info(stdout, s_card);
    return ESP_OK;
}

esp_err_t sdcard_unmount(void)
{
    if (!s_card) {
        return ESP_OK;
    }
    esp_err_t err = esp_vfs_fat_sdcard_unmount(SDCARD_MOUNT_POINT, s_card);
    spi_bus_free(SD_SPI_HOST);
    s_card = NULL;
    return err;
}

esp_err_t sdcard_usage(uint64_t *total_bytes, uint64_t *used_bytes)
{
    if (!s_card) {
        return ESP_ERR_INVALID_STATE;
    }
    FATFS *fs = NULL;
    DWORD free_clusters = 0;
    // Drive "0:" is the default registered by the mount above.
    if (f_getfree("0:", &free_clusters, &fs) != FR_OK || fs == NULL) {
        return ESP_FAIL;
    }
    uint64_t sector = s_card->csd.sector_size;
    uint64_t total_sectors = (uint64_t)(fs->n_fatent - 2) * fs->csize;
    uint64_t free_sectors = (uint64_t)free_clusters * fs->csize;
    if (total_bytes) {
        *total_bytes = total_sectors * sector;
    }
    if (used_bytes) {
        *used_bytes = (total_sectors - free_sectors) * sector;
    }
    return ESP_OK;
}
