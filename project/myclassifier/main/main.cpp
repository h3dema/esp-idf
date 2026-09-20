#include <cstdlib>

#include "esp_log.h"

#include "access.h"
#include "classifier.h"

static const char *TAG = "MAIN";

#define MODEL_PATH  MOUNT_POINT"/models/my_model.espdl"
#define IMAGE_PATH  MOUNT_POINT"/images/0011_class3.jpg"

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "Starting SD Card Classification Demo");

    // 1. Mount SD card
    if (mount_sdcard() != ESP_OK) {
        ESP_LOGE(TAG, "SD card mount failed, exiting");
        return;
    }

    // 2. Load the model
    dl::Model *model = load_model(MODEL_PATH);
    if (!model) {
        return;
    }

    // 3. Read + decode JPEG
    uint16_t img_width = 0, img_height = 0;
    uint8_t *img_data = read_and_decode_jpeg(IMAGE_PATH, &img_width, &img_height);
    if (!img_data) {
        ESP_LOGE(TAG, "Failed to read/decode image");
        delete model;
        return;
    }

    // 4. Classify
    classify_image(model, img_data, img_width, img_height, IMAGE_PATH);

    // 5. Cleanup
    free(img_data);
    delete model;

    ESP_LOGI(TAG, "Done");
}
