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
    dl::image::img_t dl_img = {
        .data = img_data,
        .width = width,
        .height = height,
        .pix_type = dl::image::DL_IMAGE_PIX_TYPE_RGB888,
    };

    dl::TensorBase *input = model->get_inputs()[0];
    input->assign(dl_img);

    model->run();

    My10ClassPostprocessor postprocessor(model, 1, 0.0f, true);
    std::vector<dl::cls::result_t> &results = postprocessor.postprocess();

    if (!results.empty()) {
        ESP_LOGI(TAG, "File: %s -> Predicted class: %s (score: %.4f)",
                 filename, results[0].cat_name, results[0].score);
    } else {
        ESP_LOGI(TAG, "File: %s -> No prediction", filename);
    }
}
