#include <cstdint>
#include <cstdio>
#include <dirent.h>
#include <algorithm>
#include <string>
#include <vector>

#include "esp_log.h"

#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"

#include "sdkconfig.h"
#include "esp_jpeg_dec.h"

static const char *TAG = "image_reader";

#define SD_MOSI 11
#define SD_MISO 13
#define SD_SCLK 12
#define SD_CS   21
#define IMAGE_FOLDER "/sdcard"

struct ImageRGB
{
    int width;
    int height;
    std::vector<uint8_t> pixels;
};


static bool decode_jpeg(
    const std::string &path,
    ImageRGB &image
)
{
    FILE *fp = fopen(path.c_str(), "rb");

    if (!fp)
    {
        ESP_LOGE(TAG, "Failed to open %s", path.c_str());
        return false;
    }

    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    rewind(fp);

    if (file_size <= 0)
    {
        fclose(fp);
        ESP_LOGE(TAG, "Invalid JPEG file size");
        return false;
    }

    std::vector<uint8_t> jpeg_data(file_size);

    size_t bytes_read = fread(
        jpeg_data.data(),
        1,
        jpeg_data.size(),
        fp
    );

    fclose(fp);

    if (bytes_read != jpeg_data.size())
    {
        ESP_LOGE(TAG, "Failed to read JPEG file");
        return false;
    }

    jpeg_dec_config_t config = DEFAULT_JPEG_DEC_CONFIG();
    config.output_type = JPEG_PIXEL_FORMAT_RGB888;

    jpeg_dec_handle_t decoder = nullptr;

    jpeg_error_t ret = jpeg_dec_open(
        &config,
        &decoder
    );

    if (ret != JPEG_ERR_OK)
    {
        ESP_LOGE(TAG, "jpeg_dec_open failed");
        return false;
    }

    jpeg_dec_io_t io = {};

    io.inbuf = jpeg_data.data();
    io.inbuf_len = static_cast<int>(jpeg_data.size());
    io.inbuf_remain = 0;
    io.outbuf = nullptr;
    io.out_size = 0;

    jpeg_dec_header_info_t header;

    ret = jpeg_dec_parse_header(
        decoder,
        &io,
        &header
    );

    if (ret != JPEG_ERR_OK)
    {
        ESP_LOGE(TAG, "jpeg_dec_parse_header failed");
        jpeg_dec_close(decoder);
        return false;
    }

    image.width = header.width;
    image.height = header.height;

    int output_buffer_size = 0;

    ret = jpeg_dec_get_outbuf_len(
        decoder,
        &output_buffer_size
    );

    if (ret != JPEG_ERR_OK)
    {
        ESP_LOGE(TAG, "jpeg_dec_get_outbuf_len failed");
        jpeg_dec_close(decoder);
        return false;
    }

    image.pixels.resize(output_buffer_size);

    io.outbuf = image.pixels.data();

    ret = jpeg_dec_process(
        decoder,
        &io
    );

    jpeg_dec_close(decoder);

    if (ret != JPEG_ERR_OK)
    {
        ESP_LOGE(TAG, "jpeg_dec_process failed");
        return false;
    }

    return true;
}



static bool mount_sdcard()
{
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 8,
        .allocation_unit_size = 16 * 1024,
        .disk_status_check_enable = false,
        .use_one_fat = false,
        .read_only = false
    };

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();

    spi_bus_config_t bus_cfg = {};
    bus_cfg.mosi_io_num = SD_MOSI;
    bus_cfg.miso_io_num = SD_MISO;
    bus_cfg.sclk_io_num = SD_SCLK;
    bus_cfg.quadwp_io_num = -1;
    bus_cfg.quadhd_io_num = -1;
    bus_cfg.max_transfer_sz = 4000;

    esp_err_t ret = spi_bus_initialize(
        static_cast<spi_host_device_t>(host.slot),
        &bus_cfg,
        SDSPI_DEFAULT_DMA
    );

    if (ret != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "Failed to initialize SPI bus: %s",
            esp_err_to_name(ret)
        );
        return false;
    }

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();

    slot_config.gpio_cs = static_cast<gpio_num_t>(SD_CS);
    slot_config.host_id = static_cast<spi_host_device_t>(host.slot);

    sdmmc_card_t *card = nullptr;
    ret = esp_vfs_fat_sdspi_mount(
        IMAGE_FOLDER,
        &host,
        &slot_config,
        &mount_config,
        &card
    );

    if (ret != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "Failed to mount SD card: %s",
            esp_err_to_name(ret)
        );

        spi_bus_free(static_cast<spi_host_device_t>(host.slot));
        return false;
    }

    sdmmc_card_print_info(stdout, card);

    ESP_LOGI(TAG, "SD card mounted");

    return true;
}


static bool is_jpeg_file(const std::string &filename)
{
    std::string lower = filename;

    std::transform(
        lower.begin(),
        lower.end(),
        lower.begin(),
        ::tolower
    );

    return
        (lower.size() >= 4 &&
         lower.compare(
             lower.size() - 4,
             4,
             ".jpg"
         ) == 0) ||
        (lower.size() >= 5 &&
         lower.compare(
             lower.size() - 5,
             5,
             ".jpeg"
         ) == 0);
}


static std::vector<std::string> scan_images(
    const std::string &folder
)
{
    std::vector<std::string> files;

    DIR *dir = opendir(folder.c_str());

    if (!dir)
    {
        ESP_LOGE(
            TAG,
            "Unable to open folder: %s",
            folder.c_str()
        );

        return files;
    }

    struct dirent *entry;

    while ((entry = readdir(dir)) != nullptr)
    {
        if (!is_jpeg_file(entry->d_name))
        {
            continue;
        }

        files.emplace_back(
            folder + "/" + entry->d_name
        );

        if (files.size() >= CONFIG_CLASSIFIER_MAX_IMAGE_FILES)
        {
            break;
        }
    }

    closedir(dir);

    return files;
}


static uint64_t sum_pixels(const ImageRGB &image)
{
    uint64_t sum = 0;

    for (uint8_t pixel : image.pixels)
    {
        sum += pixel;
    }

    return sum;
}


extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "Starting image reader");

    if (!mount_sdcard())
    {
        return;
    }

    std::vector<std::string> images =
        scan_images(CONFIG_CLASSIFIER_IMAGE_FOLDER);

    if (images.empty())
    {
        ESP_LOGW(TAG, "No JPEG images found");
        return;
    }

    for (const auto &image_path : images)
    {
        ImageRGB image;

        if (!decode_jpeg(image_path, image))
        {
            ESP_LOGW(
                TAG,
                "Failed to decode: %s",
                image_path.c_str()
            );

            continue;
        }

        uint64_t pixel_sum = sum_pixels(image);

        printf(
            "image: %s, width: %d, height: %d, pixel sum: %llu\n",
            image_path.c_str(),
            image.width,
            image.height,
            static_cast<unsigned long long>(pixel_sum)
        );
    }

    ESP_LOGI(TAG, "Finished");
}
