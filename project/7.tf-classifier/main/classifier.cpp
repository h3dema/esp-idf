#include "classifier.h"

#include <cstdio>
#include <cstring>
#include <vector>

#include "esp_log.h"

// TFLite Micro
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/schema/schema_generated.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"

static const char *TAG = "TFLITE";

// CIFAR‑10 labels
static const char *kClassNames[10] = {
    "airplane", "automobile", "bird", "cat", "deer",
    "dog", "frog", "horse", "ship", "truck"
};

// Tensor arena (adjust if your model is larger)
static constexpr int kTensorArenaSize = 300 * 1024;
static uint8_t tensor_arena[kTensorArenaSize];

// ---------------------------------------------------------------------------
// Load TFLite model from SD card
// ---------------------------------------------------------------------------
dl::Model *load_model(const char *model_path)
{
    ESP_LOGI(TAG, "Loading TFLite model: %s", model_path);

    FILE *f = fopen(model_path, "rb");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open model file");
        return nullptr;
    }

    fseek(f, 0, SEEK_END);
    size_t model_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    uint8_t *model_buf = (uint8_t *)malloc(model_size);
    if (!model_buf) {
        ESP_LOGE(TAG, "Failed to allocate model buffer");
        fclose(f);
        return nullptr;
    }

    fread(model_buf, 1, model_size, f);
    fclose(f);

    const tflite::Model *model = tflite::GetModel(model_buf);
    if (model->version() != TFLITE_SCHEMA_VERSION) {
        ESP_LOGE(TAG, "Model schema %d not equal to runtime schema %d",
                 model->version(), TFLITE_SCHEMA_VERSION);
        free(model_buf);
        return nullptr;
    }

    // Wrap inside dl::Model so main.cpp stays unchanged
    dl::Model *wrapper = new dl::Model();
    wrapper->user_data = model_buf;   // store buffer
    wrapper->tflite_model = model;    // store parsed model

    ESP_LOGI(TAG, "TFLite model loaded successfully");
    return wrapper;
}

// ---------------------------------------------------------------------------
// Run inference
// ---------------------------------------------------------------------------
void classify_image(dl::Model *wrapper,
                    uint8_t *img_data,
                    uint16_t width,
                    uint16_t height,
                    const char *filename)
{
    if (!wrapper || !img_data) {
        ESP_LOGE(TAG, "Invalid model or image data");
        return;
    }

    const tflite::Model *model = reinterpret_cast<const tflite::Model *>(wrapper->tflite_model);

    tflite::MicroMutableOpResolver<10> resolver;
    resolver.AddConv2D();
    resolver.AddDepthwiseConv2D();
    resolver.AddFullyConnected();
    resolver.AddMean();
    resolver.AddPad();
    resolver.AddSoftmax();

    tflite::MicroInterpreter interpreter(model, resolver, tensor_arena, kTensorArenaSize);

    if (interpreter.AllocateTensors() != kTfLiteOk) {
        ESP_LOGE(TAG, "Failed to allocate tensors");
        return;
    }

    TfLiteTensor *input = interpreter.input_tensor(0);

    ESP_LOGI(TAG,
             "Input tensor: type=%d, dims=[%d,%d,%d,%d]",
             input->type,
             input->dims->data[0],
             input->dims->data[1],
             input->dims->data[2],
             input->dims->data[3]);

    // Expect NHWC
    int in_h = input->dims->data[1];
    int in_w = input->dims->data[2];
    int in_c = input->dims->data[3];

    if (in_h != height || in_w != width || in_c != 3) {
        ESP_LOGE(TAG, "Image size mismatch: model expects %dx%d",
                 in_w, in_h);
        return;
    }

    // Fill input tensor (NHWC)
    if (input->type == kTfLiteUInt8) {
        memcpy(input->data.uint8, img_data, width * height * 3);

    } else if (input->type == kTfLiteFloat32) {
        float *dst = input->data.f;
        for (size_t i = 0; i < width * height * 3; i++) {
            dst[i] = (float)img_data[i] / 255.0f;
        }

    } else {
        ESP_LOGE(TAG, "Unsupported input type %d", input->type);
        return;
    }

    ESP_LOGI(TAG, "Running inference for %s", filename);
    if (interpreter.Invoke() != kTfLiteOk) {
        ESP_LOGE(TAG, "Inference failed");
        return;
    }

    TfLiteTensor *output = interpreter.output_tensor(0);
    if (output->type != kTfLiteInt8 && output->type != kTfLiteFloat32) {
        ESP_LOGE(TAG, "Unsupported output type %d", output->type);
        return;
    }

    ESP_LOGI(TAG, "Logits:");
    int best_idx = -1;
    float best_val = -9999;
    for (int i = 0; i < 10; i++) {
        float val;

        if (output->type == kTfLiteInt8) {
            int8_t raw = output->data.int8[i];
            val = raw;  // no dequantization needed for argmax
        } else {
            val = output->data.f[i];
        }
        ESP_LOGI(TAG, "  [%d] = %.3f", i, val);

        if (val > best_val) {
            best_val = val;
            best_idx = i;
        }
    }

    ESP_LOGI(TAG, "Predicted class %d: %s (score=%.3f)", best_idx, kClassNames[best_idx], best_val);
}
