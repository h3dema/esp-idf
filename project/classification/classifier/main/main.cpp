#include <cmath>
#include <cstdint>
#include <cstdio>
#include <dirent.h>
#include <sys/stat.h>
#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "esp_log.h"
#include "esp_timer.h"

#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

#include "sdkconfig.h"
#include "esp_jpeg_dec.h"
#include "dl_model.hpp"

static const char *TAG = "classifier";

static const char *CIFAR10_LABELS[] = {
    "airplane",
    "automobile",
    "bird",
    "cat",
    "deer",
    "dog",
    "frog",
    "horse",
    "ship",
    "truck"
};

struct ImageRGB
{
    int width;
    int height;
    std::vector<uint8_t> pixels;
};


#if CONFIG_CLASSIFIER_USE_EMBEDDED_MODEL

extern const uint8_t model_espdl_start[] asm("_binary_model_espdl_start");
extern const uint8_t model_espdl_end[] asm("_binary_model_espdl_end");

#endif


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
    size_t bytes_read = fread(jpeg_data.data(), 1, jpeg_data.size(), fp);
    fclose(fp);

    if (bytes_read != jpeg_data.size())
    {
        ESP_LOGE(TAG, "Failed to read JPEG file");
        return false;
    }

    jpeg_dec_config_t config = DEFAULT_JPEG_DEC_CONFIG();
    config.output_type = JPEG_PIXEL_FORMAT_RGB888;
    jpeg_dec_handle_t decoder = nullptr;
    jpeg_error_t ret = jpeg_dec_open(&config, &decoder);
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

    ret = jpeg_dec_parse_header(decoder, &io, &header);
    if (ret != JPEG_ERR_OK)
    {
        ESP_LOGE(TAG, "jpeg_dec_parse_header failed");
        jpeg_dec_close(decoder);
        return false;
    }

    image.width = header.width;
    image.height = header.height;

    int output_buffer_size = 0;
    ret = jpeg_dec_get_outbuf_len(decoder, &output_buffer_size);
    if (ret != JPEG_ERR_OK)
    {
        ESP_LOGE(TAG, "jpeg_dec_get_outbuf_len failed");
        jpeg_dec_close(decoder);
        return false;
    }
    image.pixels.resize(output_buffer_size);
    io.outbuf = image.pixels.data();
    ret = jpeg_dec_process(decoder, &io);
    jpeg_dec_close(decoder);
    if (ret != JPEG_ERR_OK)
    {
        ESP_LOGE(TAG, "jpeg_dec_process failed");
        return false;
    }

    return true;
}


static ImageRGB resize_bilinear(
    const ImageRGB &src,
    int dst_width,
    int dst_height
)
{
    ImageRGB dst;

    dst.width = dst_width;
    dst.height = dst_height;

    dst.pixels.resize(
        dst_width * dst_height * 3
    );

    const float x_ratio = static_cast<float>(src.width - 1) / dst_width;
    const float y_ratio = static_cast<float>(src.height - 1) / dst_height;
    for (int y = 0; y < dst_height; ++y)
    {
        for (int x = 0; x < dst_width; ++x)
        {
            float gx = x * x_ratio;
            float gy = y * y_ratio;

            int x0 = static_cast<int>(gx);
            int y0 = static_cast<int>(gy);

            int x1 = std::min(x0 + 1, src.width - 1);
            int y1 = std::min(y0 + 1, src.height - 1);

            float dx = gx - x0;
            float dy = gy - y0;

            for (int c = 0; c < 3; ++c)
            {
                float p00 = src.pixels[(y0 * src.width + x0) * 3 + c];
                float p01 = src.pixels[(y0 * src.width + x1) * 3 + c];
                float p10 = src.pixels[(y1 * src.width + x0) * 3 + c];
                float p11 = src.pixels[(y1 * src.width + x1) * 3 + c];
                float value =
                    p00 * (1.0f - dx) * (1.0f - dy) +
                    p01 * dx * (1.0f - dy) +
                    p10 * (1.0f - dx) * dy +
                    p11 * dx * dy;

                dst.pixels[(y * dst_width + x) * 3 + c] =
                    static_cast<uint8_t>(value);
            }
        }
    }

    return dst;
}


static inline float mobilenet_preprocess(uint8_t pixel)
{
    return (pixel / 127.5f) - 1.0f;
}


static inline int8_t quantize_int8(float value)
{
    const int32_t q = static_cast<int32_t>(std::round(value / CONFIG_CLASSIFIER_INPUT_SCALE)) + CONFIG_CLASSIFIER_INPUT_ZERO_POINT;
    return static_cast<int8_t>(std::clamp(q, -128, 127));
}

static bool mount_sdcard()
{
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 8,
        .allocation_unit_size = 16 * 1024
    };

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    sdmmc_card_t *card = nullptr;

    esp_err_t err = esp_vfs_fat_sdmmc_mount(
        "/sdcard",
        &host,
        &slot_config,
        &mount_config,
        &card
    );

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to mount SD card");
        return false;
    }
    sdmmc_card_print_info(stdout, card);
    ESP_LOGI(TAG, "SD card mounted");
    return true;
}

static bool is_jpeg_file(const std::string &filename)
{
    std::string lower = filename;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    return (lower.size() >= 4 && lower.compare(lower.size() - 4, 4, ".jpg") == 0) ||
       (lower.size() >= 5 && lower.compare(lower.size() - 5, 5, ".jpeg") == 0);
}

static std::vector<std::string> scan_images(const std::string &folder)
{
    std::vector<std::string> files;
    DIR *dir = opendir(folder.c_str());
    if (!dir)
    {
        ESP_LOGE(TAG, "Unable to open folder: %s", folder.c_str());
        return files;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != nullptr)
    {
        if (!is_jpeg_file(entry->d_name))
        {
            continue;
        }

        files.emplace_back(folder + "/" + entry->d_name);
        if (files.size() >= CONFIG_CLASSIFIER_MAX_IMAGE_FILES)
        {
            break;
        }
    }
    closedir(dir);
    return files;
}


static bool load_and_preprocess_image(
    const std::string &path,
    dl::TensorBase *input_tensor
)
{
    ImageRGB decoded;
    if (!decode_jpeg(path, decoded))
    {
        ESP_LOGE(TAG, "JPEG decode failed: %s", path.c_str());
        return false;
    }

    ImageRGB resized = resize_bilinear(
        decoded,
        CONFIG_CLASSIFIER_INPUT_WIDTH,
        CONFIG_CLASSIFIER_INPUT_HEIGHT
    );

    int8_t *tensor_data = reinterpret_cast<int8_t *>(input_tensor->data);
    size_t index = 0;
    for (int y = 0; y < resized.height; ++y)
    {
        for (int x = 0; x < resized.width; ++x)
        {
            size_t pixel = (y * resized.width + x) * 3;
            float r = mobilenet_preprocess(resized.pixels[pixel]);
            float g = mobilenet_preprocess(resized.pixels[pixel + 1]);
            float b = mobilenet_preprocess(resized.pixels[pixel + 2]);

            tensor_data[index++] = quantize_int8(r);
            tensor_data[index++] = quantize_int8(g);
            tensor_data[index++] = quantize_int8(b);
        }
    }

    return true;
}

static int argmax(const float *data, int count, float *best_score)
{
    int best_index = 0;
    *best_score = data[0];
    for (int i = 1; i < count; ++i)
    {
        if (data[i] > *best_score)
        {
            *best_score = data[i];
            best_index = i;
        }
    }

    return best_index;
}

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "Starting classifier");
    if (!mount_sdcard())
    {
        return;
    }
    std::unique_ptr<dl::model::Model> model;

#if CONFIG_CLASSIFIER_USE_EMBEDDED_MODEL

    ESP_LOGI(TAG, "Loading embedded model");
    model.reset(new dl::Model(model_espdl_start));

#else

    ESP_LOGI(TAG, "Loading model: %s", CONFIG_CLASSIFIER_MODEL_PATH);
    model.reset(new dl::Model(CONFIG_CLASSIFIER_MODEL_PATH));

#endif

    if (!model)
    {
        ESP_LOGE(TAG, "Model creation failed");
        return;
    }

    std::vector<std::string> images = scan_images(CONFIG_CLASSIFIER_IMAGE_FOLDER);
    if (images.empty())
    {
        ESP_LOGW(TAG, "No JPEG images found");
        return;
    }

    double total_time_ms = 0.0;
    int processed_images = 0;
    for (const auto &image_path : images)
    {
        auto inputs = model->get_inputs();
        if (inputs.empty())
        {
            ESP_LOGE(TAG, "No model inputs");
            return;
        }
        dl::TensorBase *input = inputs[0];

        if (!load_and_preprocess_image(image_path, input))
        {
            ESP_LOGW(TAG, "Failed preprocessing: %s", image_path.c_str());
            continue;
        }

        int64_t start_us = esp_timer_get_time();

#if CONFIG_CLASSIFIER_RUNTIME_MODE_MULTICORE

        model->run(dl::RUNTIME_MODE_MULTI_CORE);

#else

        model->run(dl::RUNTIME_MODE_SINGLE_CORE);

#endif

        int64_t end_us = esp_timer_get_time();
        double exec_time_ms = static_cast<double>(end_us - start_us) / 1000.0;
        total_time_ms += exec_time_ms;


        auto outputs = model->get_outputs();
        if (outputs.empty())
        {
            ESP_LOGE(TAG, "No model outputs");
            continue;
        }
        dl::TensorBase *output = outputs[0];

        const float *scores = reinterpret_cast<float *>(output->data);
        float best_score;
        int category_index = argmax(scores, 10, &best_score);
        printf(
            "category: %s, score: %f, execution time: %f ms\n",
            CIFAR10_LABELS[category_index],
            best_score,
            exec_time_ms
        );

        processed_images++;
    }

    if (processed_images > 0)
    {
        printf(
            "Average execution time: %f ms, Total images processed: %d\n",
            total_time_ms / static_cast<double>(processed_images),
            processed_images
        );
    }

    ESP_LOGI(TAG, "Finished");
}
