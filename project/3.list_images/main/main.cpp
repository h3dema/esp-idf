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

#include "dl_image_jpeg.hpp"
#include "dl_image_bmp.hpp"
#include "dl_image_define.hpp"


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


// Read a JPEG file from the SD card and return it as a dl::image::img_t
dl::image::img_t read_jpg_from_sdcard(const char *filepath) {
    dl::image::img_t img = {}; // Initialize an empty image struct
    FILE *f = fopen(filepath, "rb");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open file: %s", filepath);
        return img;
    }

    // Get file size
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    // Allocate memory for the JPEG data
    uint8_t *jpeg_data = (uint8_t *)malloc(size);
    if (!jpeg_data) {
        ESP_LOGE(TAG, "Failed to allocate memory for JPEG");
        fclose(f);
        return img;
    }

    // Read the JPEG file into the buffer
    if (fread(jpeg_data, 1, size, f) != static_cast<size_t>(size)) {
        ESP_LOGE(TAG, "Failed to read JPEG: %s", filepath);
        free(jpeg_data);
        fclose(f);
        return img;
    }
    fclose(f);

    // Prepare the JPEG image structure
    dl::image::jpeg_img_t jpeg_img = {
        .data = jpeg_data,
        .data_len = (size_t)size
    };

    // Decode the JPEG to RGB888 format
    img = dl::image::sw_decode_jpeg(jpeg_img, dl::image::DL_IMAGE_PIX_TYPE_RGB888);

    // Free the JPEG buffer as it's no longer needed
    free(jpeg_data);

    return img;
}


// Read a BMP file from the SD card and return it as a dl::image::img_t
static dl::image::img_t read_bmp_from_sdcard(const char *filepath)
{
    dl::image::img_t img = {};

    FILE *f = fopen(filepath, "rb");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file: %s", filepath);
        return img;
    }

    // BMP signature
    uint16_t signature;
    if (fread(&signature, sizeof(signature), 1, f) != 1 ||
        signature != 0x4D42) {
        ESP_LOGW(TAG, "%s is not a BMP file", filepath);
        fclose(f);
        return img;
    }

    // Pixel data offset
    fseek(f, 10, SEEK_SET);

    uint32_t pixel_offset;
    if (fread(&pixel_offset, sizeof(pixel_offset), 1, f) != 1) {
        fclose(f);
        return img;
    }

    // Image dimensions
    fseek(f, 18, SEEK_SET);

    int32_t width;
    int32_t height;

    if (fread(&width, sizeof(width), 1, f) != 1 ||
        fread(&height, sizeof(height), 1, f) != 1) {
        fclose(f);
        return img;
    }

    // Bits per pixel
    fseek(f, 28, SEEK_SET);

    uint16_t bits_per_pixel;
    if (fread(&bits_per_pixel, sizeof(bits_per_pixel), 1, f) != 1) {
        fclose(f);
        return img;
    }

    if (bits_per_pixel != 24) {
        ESP_LOGW(TAG,
                 "%s: only 24-bit BMP supported (got %u bpp)",
                 filepath,
                 bits_per_pixel);
        fclose(f);
        return img;
    }
    const int32_t abs_height = abs(height);

    // BMP rows are padded to 4-byte boundaries.
    const size_t row_size = ((static_cast<size_t>(width) * 3 + 3) & ~static_cast<size_t>(3));
    const size_t image_size = static_cast<size_t>(width) * abs_height * 3;
    uint8_t *data = static_cast<uint8_t *>(malloc(image_size));
    if (data == nullptr) {
        ESP_LOGE(TAG, "Out of memory allocating BMP image");
        fclose(f);
        return img;
    }

    fseek(f, pixel_offset, SEEK_SET);
    uint8_t *row = static_cast<uint8_t *>(malloc(row_size));
    if (row == nullptr) {
        ESP_LOGE(TAG, "Out of memory allocating BMP row");
        free(data);
        fclose(f);
        return img;
    }

    for (int32_t y = 0; y < abs_height; y++) {

        if (fread(row, 1, row_size, f) != row_size) {
            ESP_LOGE(TAG, "Read error in %s", filepath);
            free(row);
            free(data);
            fclose(f);
            return img;
        }

        // BMP is BGR, while RGB888 expects RGB.
        //
        // Positive height: bottom-up BMP.
        // Negative height: top-down BMP.
        const int32_t dst_y = (height > 0) ? (abs_height - 1 - y) : y;
        uint8_t *dst = data + static_cast<size_t>(dst_y) * width * 3;
        for (int32_t x = 0; x < width; x++) {
            dst[x * 3 + 0] = row[x * 3 + 2]; // R
            dst[x * 3 + 1] = row[x * 3 + 1]; // G
            dst[x * 3 + 2] = row[x * 3 + 0]; // B
        }
    }

    free(row);
    fclose(f);

    img.data = data;
    img.width = width;
    img.height = abs_height;
    img.pix_type = dl::image::DL_IMAGE_PIX_TYPE_RGB888;

    return img;
}

static uint64_t sum_image_pixels(const dl::image::img_t &img)
{
    if (img.data == nullptr) {
        return 0;
    }

    if (img.pix_type != dl::image::DL_IMAGE_PIX_TYPE_RGB888) {
        ESP_LOGW(TAG, "Unsupported image pixel type");
        return 0;
    }

    const uint8_t *data = static_cast<const uint8_t *>(img.data);
    const size_t num_pixels = static_cast<size_t>(img.width) * img.height;

    uint64_t sum = 0;
    for (size_t i = 0; i < num_pixels * 3; i++) {
        sum += data[i];
    }

    return sum;
}



// sum_file_pixels()
//         |
//         +-- .jpg/.jpeg ----------> read_jpg_from_sdcard()
//         |                                     |
//         |                                     v
//         |                             decoded RGB888 img
//         |                                       |
//         +-- .bmp --> read_bmp_from_sdcard()     |
//                                   |             |
//                                   v             |
//                            RGB888 img           |
//                                   |             |
//                                   |<------------+
//                                   v
//                          sum_image_pixels()

static uint64_t sum_file_pixels(const char *path)
{
    dl::image::img_t img = {};

    const char *extension = strrchr(path, '.');

    if (extension == nullptr) {
        ESP_LOGW(TAG, "File has no extension: %s", path);
        return 0;
    }

    if (strcasecmp(extension, ".jpg") == 0 ||
        strcasecmp(extension, ".jpeg") == 0) {
        img = read_jpg_from_sdcard(path);

    } else if (strcasecmp(extension, ".bmp") == 0) {
        img = read_bmp_from_sdcard(path);

    } else {
        ESP_LOGW(TAG, "Unsupported image format: %s", path);
        return 0;
    }

    if (img.data == nullptr) {
        return 0;
    }

    uint64_t sum = sum_image_pixels(img);
    free(img.data);
    return sum;
}


extern "C" void app_main()
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

    ret = spi_bus_initialize(
        static_cast<spi_host_device_t>(host.slot),
        &bus_cfg,
        SDSPI_DEFAULT_DMA
    );
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize bus.");
        return;
    }

    // This initializes the slot without card detect (CD) and write protect (WP) signals.
    // Modify slot_config.gpio_cd and slot_config.gpio_wp if your board has these signals.
    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = static_cast<gpio_num_t>(PIN_NUM_CS);
    slot_config.host_id = static_cast<spi_host_device_t>(host.slot);

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
    spi_bus_free(static_cast<spi_host_device_t>(host.slot));

    // Deinitialize the power control driver if it was used
#if CONFIG_EXAMPLE_SD_PWR_CTRL_LDO_INTERNAL_IO
    ret = sd_pwr_ctrl_del_on_chip_ldo(pwr_ctrl_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to delete the on-chip LDO power control driver");
        return;
    }
#endif
}
