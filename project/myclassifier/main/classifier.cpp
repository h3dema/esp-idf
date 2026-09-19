#include "classifier.h"

#include <vector>

#include "esp_log.h"
#include "dl_image.hpp"
#include "dl_cls_postprocessor.hpp"

static const char *TAG = "CLASSIFIER";

// ---------- Custom 10-class postprocessor ----------
class My10ClassPostprocessor : public dl::cls::ClsPostprocessor {
public:
    My10ClassPostprocessor(dl::Model *model,
                           int topk = 1,
                           float score_thr = 0.0f,
                           bool need_softmax = true)
        : ClsPostprocessor(model, topk, score_thr, need_softmax, "output") {
        m_cat_names = my_class_names;
    }

private:
    static const char *my_class_names[10];
};

const char *My10ClassPostprocessor::my_class_names[10] = {
    "class_0", "class_1", "class_2", "class_3", "class_4",
    "class_5", "class_6", "class_7", "class_8", "class_9"
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
    if (model == nullptr || img_data == nullptr) {
        ESP_LOGE(TAG, "Invalid model or image data");
        return;
    }

    // Get the model input using the ESP-DL 3.3.x API.
    dl::TensorBase *input = model->get_input();
    if (input == nullptr) {
        ESP_LOGE(TAG, "Model has no input tensor");
        return;
    }

    ESP_LOGI(TAG,
             "Input tensor: shape=[%d,%d,%d,%d], dtype=%d, size=%d",
             input->shape.size() > 0 ? input->shape[0] : -1,
             input->shape.size() > 1 ? input->shape[1] : -1,
             input->shape.size() > 2 ? input->shape[2] : -1,
             input->shape.size() > 3 ? input->shape[3] : -1,
             input->dtype,
             input->size);

    /*
     * The model input tensor already has its shape, dtype and memory
     * allocated by ESP-DL. We therefore create a temporary TensorBase
     * containing the RGB888 image and assign it to the model input.
     *
     * For an RGB image, the temporary tensor is represented as:
     *
     *   [1, 3, height, width]
     *
     * with RGB data in CHW order.
     */

    if (input->shape.size() != 4) {
        ESP_LOGE(TAG,
                 "Unsupported input shape. Expected 4 dimensions, got %d",
                 static_cast<int>(input->shape.size()));
        return;
    }

    const int channels = input->shape[1];
    const int input_height = input->shape[2];
    const int input_width = input->shape[3];

    if (channels != 3) {
        ESP_LOGE(TAG,
                 "Unsupported number of input channels: %d. Expected 3",
                 channels);
        return;
    }

    if (width != input_width || height != input_height) {
        ESP_LOGE(TAG,
                 "Image size %ux%u does not match model input %dx%d",
                 width,
                 height,
                 input_width,
                 input_height);
        return;
    }

    const size_t pixel_count =
        static_cast<size_t>(width) * static_cast<size_t>(height);

    const size_t element_count = pixel_count * 3;

    /*
     * Convert RGB888 HWC:
     *
     *   RGB RGB RGB ...
     *
     * into CHW:
     *
     *   RRRR... GGGG... BBBB...
     *
     * This is the layout normally expected by ESP-DL models with
     * input shape [1, 3, H, W].
     */
    std::vector<float> tensor_data(element_count);

    float *red   = tensor_data.data();
    float *green = red + pixel_count;
    float *blue  = green + pixel_count;

    for (size_t i = 0; i < pixel_count; ++i) {
        red[i]   = static_cast<float>(img_data[i * 3 + 0]);
        green[i] = static_cast<float>(img_data[i * 3 + 1]);
        blue[i]  = static_cast<float>(img_data[i * 3 + 2]);
    }

    /*
     * Create a temporary TensorBase using the model's input dtype.
     *
     * The exponent is taken from the existing model input. This is
     * important for quantized ESP-DL models.
     */
    dl::TensorBase image_tensor(
        {1, 3, static_cast<int>(height), static_cast<int>(width)},
        tensor_data.data(),
        0,
        dl::DATA_TYPE_FLOAT,
        true
    );

    if (image_tensor.data == nullptr) {
        ESP_LOGE(TAG, "Failed to create image tensor");
        return;
    }

    if (!input->assign(&image_tensor)) {
        ESP_LOGE(TAG, "Failed to assign image tensor to model input");
        return;
    }

    ESP_LOGI(TAG, "Running inference for %s", filename);

    model->run();

    /*
     * Post-process the model output.
     */
    My10ClassPostprocessor postprocessor(
        model,
        1,       // top-k
        0.0f,    // score threshold
        true     // apply softmax
    );

    std::vector<dl::cls::result_t> &results =
        postprocessor.postprocess();

    if (!results.empty()) {
        ESP_LOGI(TAG,
                 "File: %s -> Predicted class: %s (score: %.4f)",
                 filename,
                 results[0].cat_name,
                 results[0].score);
    } else {
        ESP_LOGI(TAG,
                 "File: %s -> No prediction",
                 filename);
    }
}