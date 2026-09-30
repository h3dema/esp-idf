#include "classifier.h"
#include "int8_classification_postprocessor.h"

#include <vector>

#include "esp_log.h"
#include "dl_image.hpp"
#include "dl_cls_postprocessor.hpp"

static const char *TAG = "CLASSIFIER";
static const char *OUTPUT_LAYER = "predictions";

// ---------- Custom 10-class postprocessor ----------
const uint32_t NUM_CLASSES = 10;
const char *my_class_names[NUM_CLASSES] = {
    "airplane", "automobile", "bird", "cat", "deer",
    "dog", "frog", "horse", "ship", "truck"
};

// ---------- Public API ----------
dl::Model *load_model(const char *model_path)
{
    ESP_LOGI(TAG, "Loading model: %s", model_path);
    dl::Model *model = new dl::Model(model_path, fbs::MODEL_LOCATION_IN_SDCARD);
    if (!model) {
        ESP_LOGE(TAG, "Failed to load model from %s", model_path);
        return nullptr;
    }
    ESP_LOGI(TAG, "Model loaded successfully");
    return model;
}

void classify_image(dl::Model *model,
                    uint8_t *img_data,
                    uint16_t width,
                    uint16_t height,
                    const char *filename)
{
    if (!model || !img_data) {
        ESP_LOGE(TAG, "Invalid model or image data");
        return;
    }

    dl::TensorBase *input = model->get_input();
    if (!input) {
        ESP_LOGE(TAG, "Model has no input tensor");
        return;
    }

    // Model input is NHWC: [1, H, W, C]
    if (input->shape.size() != 4) {
        ESP_LOGE(TAG, "Expected 4D NHWC input, got %d dims",
                 (int)input->shape.size());
        return;
    }

    /*
     * The model input tensor already has its shape, dtype and memory
     * allocated by ESP-DL. We therefore create a temporary TensorBase
     * containing the RGB888 image and assign it to the model input.
     *
     * ESP‑DL 3.3.x models exported from ESP‑DL tools (and most ESP‑DL examples)
     * use NHWC layout.
     *
     * For an RGB image, the temporary tensor is represented as:
     *   [1, 3, height, width]
     *
     * with RGB data in CHW order.
     */
    const int batch        = input->shape[0];
    const int input_height = input->shape[1];
    const int input_width  = input->shape[2];
    const int channels     = input->shape[3];

    ESP_LOGI(TAG,
             "Input tensor: shape=[%d,%d,%d,%d], dtype=%d, size=%d",
             batch, input_height, input_width, channels,
             input->dtype, input->size);

    if (batch != 1) {
        ESP_LOGE(TAG, "Only batch=1 supported, got %d", batch);
        return;
    }
    if (channels != 3) {
        ESP_LOGE(TAG, "Model expects 3 channels, got %d", channels);
        return;
    }

    if (width != input_width || height != input_height) {
        ESP_LOGE(TAG,
                 "Image size %ux%u does not match model input %dx%d",
                 width, height, input_width, input_height);
        return;
    }

    const size_t pixel_count = (size_t)width * (size_t)height;
    const size_t element_count = pixel_count * 3;

    // --- Handle dtype ---
    dl::dtype_t dtype = input->dtype;

    if (!input->data) {
        ESP_LOGE(TAG, "Input tensor has null data pointer");
        return;
    }

    // --- Fill the existing buffer in HWC order ---
    if (dtype == dl::DATA_TYPE_INT8) {
        int8_t *buf = static_cast<int8_t *>(input->data);

        if (input->size < (int)element_count) {
            ESP_LOGE(TAG,
                     "Input buffer too small: size=%d, needed=%d",
                     input->size, (int)element_count);
            return;
        }

        for (size_t i = 0; i < pixel_count; ++i) {
            uint8_t r = img_data[i * 3 + 0];
            uint8_t g = img_data[i * 3 + 1];
            uint8_t b = img_data[i * 3 + 2];

            buf[i * 3 + 0] = (int8_t)((int)r - 128);
            buf[i * 3 + 1] = (int8_t)((int)g - 128);
            buf[i * 3 + 2] = (int8_t)((int)b - 128);
        }

    } else if (dtype == dl::DATA_TYPE_FLOAT) {
        float *buf = static_cast<float *>(input->data);

        if (input->size < (int)element_count) {
            ESP_LOGE(TAG,
                     "Input buffer too small: size=%d, needed=%d",
                     input->size, (int)element_count);
            return;
        }

        for (size_t i = 0; i < pixel_count; ++i) {
            buf[i * 3 + 0] = (float)img_data[i * 3 + 0];
            buf[i * 3 + 1] = (float)img_data[i * 3 + 1];
            buf[i * 3 + 2] = (float)img_data[i * 3 + 2];
        }

    } else {
        ESP_LOGE(TAG, "Unsupported input dtype=%d", (int)dtype);
        return;
    }

    ESP_LOGI(TAG, "Running inference for %s", filename);
    model->run();  // outputs INT8 logits
    auto out = model->get_output(OUTPUT_LAYER);
    if (out) {
        ESP_LOGI(TAG, "Output dtype=%d, size=%d", out->dtype, out->size);
        Int8ClassificationPostprocessor pp(out, my_class_names, 10);

        ESP_LOGI(TAG, "Predicted class %d: %s (logit=%d)",
            pp.get_class_index(),
            pp.get_label(),
            pp.get_logit()
        );

        // Optional: print all logits
        for (int i = 0; i < 10; i++) {
            ESP_LOGI(TAG, "logit[%d] = %d", i, pp.get_all_logits()[i]);
        }

    } else {
        ESP_LOGI(TAG, "File: %s -> No prediction", filename);
    }
}