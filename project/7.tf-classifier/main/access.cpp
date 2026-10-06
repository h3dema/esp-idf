#include "access.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "driver/sdspi_host.h"
#include "sdmmc_cmd.h"

#include "dl_image.hpp"
#include "dl_image_jpeg.hpp"

static const char *TAG = "ACCESS";

esp_err_t mount_sdcard(void)
{
    esp_err_t ret;
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024,
    };

    sdmmc_card_t *card;
    const char mount_point[] = MOUNT_POINT;

    ESP_LOGI(TAG, "Initializing SD card (SPI mode)");

    spi_bus_config_t bus_cfg = {
        .mosi_io_num = PIN_NUM_MOSI,
        .miso_io_num = PIN_NUM_MISO,
        .sclk_io_num = PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };

    ret = spi_bus_initialize(SPI2_HOST, &bus_cfg, SDSPI_DEFAULT_DMA);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SPI bus (%s)", esp_err_to_name(ret));
        return ret;
    }

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI2_HOST;

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = PIN_NUM_CS;
    slot_config.host_id = SPI2_HOST;

    ret = esp_vfs_fat_sdspi_mount(mount_point, &host, &slot_config,
                                  &mount_config, &card);
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount filesystem. "
                          "Enable format_if_mount_failed to auto-format.");
        } else {
            ESP_LOGE(TAG, "Failed to initialize the card (%s). "
                          "Check wiring/pull-ups.", esp_err_to_name(ret));
        }
        spi_bus_free(SPI2_HOST);
        return ret;
    }

    ESP_LOGI(TAG, "SD card mounted at %s", mount_point);
    sdmmc_card_print_info(stdout, card);
    return ESP_OK;
}

uint8_t *read_and_decode_jpeg(const char *filepath,
                              uint16_t *out_width,
                              uint16_t *out_height)
{
    FILE *f = fopen(filepath, "rb");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open file: %s", filepath);
        return nullptr;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    uint8_t *jpeg_data = (uint8_t *)malloc(size);
    if (!jpeg_data) {
        ESP_LOGE(TAG, "Failed to allocate memory for JPEG data");
        fclose(f);
        return nullptr;
    }

    fread(jpeg_data, 1, size, f);
    fclose(f);

    dl::image::jpeg_img_t jpeg_img = {
        .data = jpeg_data,
        .data_len = (size_t)size,
    };

    dl::image::img_t img =
        dl::image::sw_decode_jpeg(jpeg_img, dl::image::DL_IMAGE_PIX_TYPE_RGB888);

    free(jpeg_data);

    if (img.data == nullptr) {
        ESP_LOGE(TAG, "JPEG decode failed");
        return nullptr;
    }

    *out_width = img.width;
    *out_height = img.height;
    ESP_LOGI(TAG, "Decoded JPEG: %dx%d", img.width, img.height);

    return (uint8_t *)img.data;
}
