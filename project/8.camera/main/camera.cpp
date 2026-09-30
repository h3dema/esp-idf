#include <stdio.h>
#include <string.h>
#include "esp_camera.h"
#include "esp_log.h"
#include "esp_err.h"
#include "driver/sdmmc_host.h"
#include "driver/sdmmc_defs.h"
#include "sdmmc_cmd.h"
#include "esp_vfs_fat.h"
#include "esp_timer.h"

#define TAG "CAM_APP"

// Include your pin definitions
#include "camera_pins.h"

static uint64_t last_capture_time = 0;
static int image_count = 1;
static bool camera_ok = false;
static bool sd_ok = false;

static void save_photo(const char *filename)
{
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
        ESP_LOGE(TAG, "Failed to get frame buffer");
        return;
    }

    FILE *f = fopen(filename, "wb");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open file %s", filename);
        esp_camera_fb_return(fb);
        return;
    }

    fwrite(fb->buf, 1, fb->len, f);
    fclose(f);

    esp_camera_fb_return(fb);

    ESP_LOGI(TAG, "Saved photo: %s", filename);
}

void app_main(void)
{
    ESP_LOGI(TAG, "Starting camera app...");

    // -----------------------------
    // CAMERA CONFIG
    // -----------------------------
    camera_config_t config = {
        .ledc_channel = LEDC_CHANNEL_0,
        .ledc_timer = LEDC_TIMER_0,
        .pin_d0 = Y2_GPIO_NUM,
        .pin_d1 = Y3_GPIO_NUM,
        .pin_d2 = Y4_GPIO_NUM,
        .pin_d3 = Y5_GPIO_NUM,
        .pin_d4 = Y6_GPIO_NUM,
        .pin_d5 = Y7_GPIO_NUM,
        .pin_d6 = Y8_GPIO_NUM,
        .pin_d7 = Y9_GPIO_NUM,
        .pin_xclk = XCLK_GPIO_NUM,
        .pin_pclk = PCLK_GPIO_NUM,
        .pin_vsync = VSYNC_GPIO_NUM,
        .pin_href = HREF_GPIO_NUM,
        .pin_sscb_sda = SIOD_GPIO_NUM,
        .pin_sscb_scl = SIOC_GPIO_NUM,
        .pin_pwdn = PWDN_GPIO_NUM,
        .pin_reset = RESET_GPIO_NUM,
        .xclk_freq_hz = 20000000,
        .frame_size = FRAMESIZE_UXGA,
        .pixel_format = PIXFORMAT_JPEG,
        .grab_mode = CAMERA_GRAB_WHEN_EMPTY,
        .fb_location = CAMERA_FB_IN_PSRAM,
        .jpeg_quality = 12,
        .fb_count = 1
    };

    if (psramFound()) {
        config.jpeg_quality = 10;
        config.fb_count = 2;
        config.grab_mode = CAMERA_GRAB_LATEST;
    } else {
        config.frame_size = FRAMESIZE_SVGA;
        config.fb_location = CAMERA_FB_IN_DRAM;
    }

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Camera init failed: 0x%x", err);
        return;
    }

    camera_ok = true;
    ESP_LOGI(TAG, "Camera initialized");

    // -----------------------------
    // SD CARD INIT (SDMMC)
    // -----------------------------
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };

    sdmmc_card_t *card;
    err = esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot_config, &mount_config, &card);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount SD card: %s", esp_err_to_name(err));
        return;
    }

    sd_ok = true;
    ESP_LOGI(TAG, "SD card mounted");

    last_capture_time = esp_timer_get_time();

    // -----------------------------
    // MAIN LOOP
    // -----------------------------
    while (true) {
        if (camera_ok && sd_ok) {
            uint64_t now = esp_timer_get_time();

            if ((now - last_capture_time) >= 60000ULL * 1000ULL) {
                char filename[64];
                snprintf(filename, sizeof(filename), "/sdcard/image%d.jpg", image_count);

                save_photo(filename);

                image_count++;
                last_capture_time = now;

                ESP_LOGI(TAG, "Next photo in 1 minute");
            }
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
