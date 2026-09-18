/* SD card and FAT filesystem example.
   Reads the images on /sdcard and print the sum of their pixels to serial.

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <sys/unistd.h>
#include <sys/stat.h>
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "sd_test_io.h"
#if SOC_SDMMC_IO_POWER_EXTERNAL
#include "sd_pwr_ctrl_by_on_chip_ldo.h"
#endif
#include <stdint.h>
#include <stdio.h>

#include <dirent.h>
#include <ctype.h>
#include <string.h>


#define EXAMPLE_MAX_CHAR_SIZE    64

static const char *TAG = "example";

#define MOUNT_POINT "/sdcard"
#define IMAGE_DIR   MOUNT_POINT"/images"

#ifdef CONFIG_EXAMPLE_DEBUG_PIN_CONNECTIONS
const char* names[] = {"CLK ", "MOSI", "MISO", "CS  "};
const int pins[] = {CONFIG_EXAMPLE_PIN_CLK,
                    CONFIG_EXAMPLE_PIN_MOSI,
                    CONFIG_EXAMPLE_PIN_MISO,
                    CONFIG_EXAMPLE_PIN_CS};

const int pin_count = sizeof(pins)/sizeof(pins[0]);
#if CONFIG_EXAMPLE_ENABLE_ADC_FEATURE
const int adc_channels[] = {CONFIG_EXAMPLE_ADC_PIN_CLK,
                            CONFIG_EXAMPLE_ADC_PIN_MOSI,
                            CONFIG_EXAMPLE_ADC_PIN_MISO,
                            CONFIG_EXAMPLE_ADC_PIN_CS};
#endif //CONFIG_EXAMPLE_ENABLE_ADC_FEATURE

pin_configuration_t config = {
    .names = names,
    .pins = pins,
#if CONFIG_EXAMPLE_ENABLE_ADC_FEATURE
    .adc_channels = adc_channels,
#endif
};
#endif //CONFIG_EXAMPLE_DEBUG_PIN_CONNECTIONS

// Pin assignments can be set in menuconfig, see "SD SPI Example Configuration" menu.
// You can also change the pin assignments here by changing the following 4 lines.
#define PIN_NUM_MISO  CONFIG_EXAMPLE_PIN_MISO
#define PIN_NUM_MOSI  CONFIG_EXAMPLE_PIN_MOSI
#define PIN_NUM_CLK   CONFIG_EXAMPLE_PIN_CLK
#define PIN_NUM_CS    CONFIG_EXAMPLE_PIN_CS


static bool is_image_file(const char *name)
{
    if (name == NULL) {
        return false;
    }

    const char *ext = strrchr(name, '.');
    if (ext == NULL || *(ext + 1) == '\0') {
        return false;
    }
    ext++;  // Skip '.'
    return (strcasecmp(ext, "bmp")  == 0) ||
           (strcasecmp(ext, "jpg")  == 0) ||
           (strcasecmp(ext, "jpeg") == 0) ||
           (strcasecmp(ext, "png")  == 0) ||
           (strcasecmp(ext, "gif")  == 0) ||
           (strcasecmp(ext, "tif")  == 0) ||
           (strcasecmp(ext, "tiff") == 0) ||
           (strcasecmp(ext, "webp") == 0);
}


static uint64_t sum_file_pixels(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open %s", path);
        return 0;
    }

    // BMP Header
    uint16_t signature;
    fread(&signature, sizeof(signature), 1, f);

    if (signature != 0x4D42) { // "BM"
        ESP_LOGW(TAG, "%s is not a BMP file", path);
        fclose(f);
        return 0;
    }

    fseek(f, 10, SEEK_SET);

    uint32_t pixel_offset;
    fread(&pixel_offset, sizeof(pixel_offset), 1, f);

    fseek(f, 18, SEEK_SET);

    int32_t width;
    int32_t height;

    fread(&width, sizeof(width), 1, f);
    fread(&height, sizeof(height), 1, f);

    fseek(f, 28, SEEK_SET);

    uint16_t bits_per_pixel;
    fread(&bits_per_pixel, sizeof(bits_per_pixel), 1, f);

    if (bits_per_pixel != 24) {
        ESP_LOGW(TAG,
                 "%s: only 24-bit BMP supported (got %u bpp)",
                 path,
                 bits_per_pixel);
        fclose(f);
        return 0;
    }

    fseek(f, pixel_offset, SEEK_SET);

    uint64_t sum = 0;

    const int row_size =
        ((width * 3 + 3) & ~3); // BMP rows padded to 4 bytes

    uint8_t *row = malloc(row_size);

    if (row == NULL) {
        ESP_LOGE(TAG, "Out of memory");
        fclose(f);
        return 0;
    }

    for (int y = 0; y < abs(height); y++) {

        if (fread(row, 1, row_size, f) != row_size) {
            ESP_LOGE(TAG, "Read error in %s", path);
            free(row);
            fclose(f);
            return 0;
        }

        for (int x = 0; x < width; x++) {

            uint8_t b = row[x * 3 + 0];
            uint8_t g = row[x * 3 + 1];
            uint8_t r = row[x * 3 + 2];

            sum += r;
            sum += g;
            sum += b;
        }
    }
    free(row);
    fclose(f);

    return sum;
}


void app_main(void)
{
    esp_err_t ret;

    // Options for mounting the filesystem.
    // If format_if_mount_failed is set to true, SD card will be partitioned and
    // formatted in case when mounting fails.
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
#ifdef CONFIG_EXAMPLE_FORMAT_IF_MOUNT_FAILED
        .format_if_mount_failed = true,
#else
        .format_if_mount_failed = false,
#endif // EXAMPLE_FORMAT_IF_MOUNT_FAILED
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };
    sdmmc_card_t *card;
    const char mount_point[] = MOUNT_POINT;
    ESP_LOGI(TAG, "Initializing SD card");

    // Use settings defined above to initialize SD card and mount FAT filesystem.
    // Note: esp_vfs_fat_sdmmc/sdspi_mount is all-in-one convenience functions.
    // Please check its source code and implement error recovery when developing
    // production applications.
    ESP_LOGI(TAG, "Using SPI peripheral");

    // By default, SD card frequency is initialized to SDMMC_FREQ_DEFAULT (20MHz)
    // For setting a specific frequency, use host.max_freq_khz (range 400kHz - 20MHz for SDSPI)
    // Example: for fixed frequency of 10MHz, use host.max_freq_khz = 10000;
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.unaligned_multi_block_rw_max_chunk_size = 8;

    // For SoCs where the SD power can be supplied both via an internal or external (e.g. on-board LDO) power supply.
    // When using specific IO pins (which can be used for ultra high-speed SDMMC) to connect to the SD card
    // and the internal LDO power supply, we need to initialize the power supply first.
#if CONFIG_EXAMPLE_SD_PWR_CTRL_LDO_INTERNAL_IO
    sd_pwr_ctrl_ldo_config_t ldo_config = {
        .ldo_chan_id = CONFIG_EXAMPLE_SD_PWR_CTRL_LDO_IO_ID,
    };
    sd_pwr_ctrl_handle_t pwr_ctrl_handle = NULL;

    ret = sd_pwr_ctrl_new_on_chip_ldo(&ldo_config, &pwr_ctrl_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create a new on-chip LDO power control driver");
        return;
    }
    host.pwr_ctrl_handle = pwr_ctrl_handle;
#endif

    spi_bus_config_t bus_cfg = {
        .mosi_io_num = PIN_NUM_MOSI,
        .miso_io_num = PIN_NUM_MISO,
        .sclk_io_num = PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };

    ret = spi_bus_initialize(host.slot, &bus_cfg, SDSPI_DEFAULT_DMA);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize bus.");
        return;
    }

    // This initializes the slot without card detect (CD) and write protect (WP) signals.
    // Modify slot_config.gpio_cd and slot_config.gpio_wp if your board has these signals.
    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = PIN_NUM_CS;
    slot_config.host_id = host.slot;

    ESP_LOGI(TAG, "Mounting filesystem");
    ret = esp_vfs_fat_sdspi_mount(mount_point, &host, &slot_config, &mount_config, &card);

    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount filesystem. "
                     "If you want the card to be formatted, set the CONFIG_EXAMPLE_FORMAT_IF_MOUNT_FAILED menuconfig option.");
        } else {
            ESP_LOGE(TAG, "Failed to initialize the card (%s). "
                     "Make sure SD card lines have pull-up resistors in place.", esp_err_to_name(ret));
#ifdef CONFIG_EXAMPLE_DEBUG_PIN_CONNECTIONS
            check_sd_card_pins(&config, pin_count);
#endif
        }
        return;
    }
    ESP_LOGI(TAG, "Filesystem mounted");

    // Card has been initialized, print its properties
    sdmmc_card_print_info(stdout, card);

    // Use POSIX and C standard library functions to work with files.
    DIR *dir = opendir(IMAGE_DIR);
    if (dir == NULL) {
        ESP_LOGE(TAG, "Failed to open directory: %s", IMAGE_DIR);
        return;
    }
    ESP_LOGE(TAG, "Directory: %s", IMAGE_DIR);

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        ESP_LOGE(TAG, "Entry: %s", entry->d_name);
        if (!is_image_file(entry->d_name)) {
            continue;
        }

        char path[256];
        int len = snprintf(path, sizeof(path), "%s/%s", IMAGE_DIR, entry->d_name);
        if (len < 0 || len >= sizeof(path)) {
            ESP_LOGW(TAG, "Path too long: %s/%s", IMAGE_DIR, entry->d_name);
            continue;
        }
        uint64_t pixel_sum = sum_file_pixels(path);
        ESP_LOGI(TAG, "Image: %s Pixel sum: %llu", entry->d_name, (unsigned long long)pixel_sum);
    }
    ESP_LOGE(TAG, "Finished scanning directory");
    closedir(dir);

    //deinitialize the bus after all devices are removed
    spi_bus_free(host.slot);

    // Deinitialize the power control driver if it was used
#if CONFIG_EXAMPLE_SD_PWR_CTRL_LDO_INTERNAL_IO
    ret = sd_pwr_ctrl_del_on_chip_ldo(pwr_ctrl_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to delete the on-chip LDO power control driver");
        return;
    }
#endif
}
