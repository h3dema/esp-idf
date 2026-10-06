#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// ===== TFLITE MICRO =====
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"
#include "tensorflow/lite/micro/micro_log.h"
// #include "tensorflow/lite/micro/all_ops_resolver.h"

// ===== MODEL HEADERS =====
#include "best_triple_i256_m1_int8.h"
#include "best_triple_i256_m2_int8.h"
#include "best_triple_i256_m3_int8.h"

static const char *TAG = "TRIPLE_MODEL";

// ===== ARENA SIZE =====
// Increase if your models need more RAM
constexpr int kTensorArenaSize = 250 * 1024;


void dump_output_tensor(TfLiteTensor *tensor, const char *label)
{
    ESP_LOGI(TAG, "===== %s =====", label);
    ESP_LOGI(TAG,
             "type=%d bytes=%d scale=%f zero_point=%d",
             tensor->type,
             tensor->bytes,
             tensor->params.scale,
             tensor->params.zero_point);

    int n = tensor->bytes;
    switch (tensor->type) {
    case kTfLiteInt8:
        {
            int count = n / sizeof(int8_t);
            ESP_LOGI(TAG, "INT8 tensor (%d elements)", count);
            for (int i = 0; i < count && i < 32; i++) {
                float dequant = (tensor->data.int8[i] - tensor->params.zero_point) * tensor->params.scale;
                ESP_LOGI(TAG, "[%d] raw=%d dequant=%f", i, tensor->data.int8[i], dequant);
            }
        }
        break;

    case kTfLiteUInt8:
        {
            int count = n / sizeof(uint8_t);
            ESP_LOGI(TAG, "UINT8 tensor (%d elements)", count);
            for (int i = 0; i < count && i < 32; i++) {
                float dequant = (tensor->data.uint8[i] - tensor->params.zero_point) * tensor->params.scale;
                ESP_LOGI(TAG, "[%d] raw=%u dequant=%f", i, tensor->data.uint8[i], dequant);
            }
        }
        break;

    case kTfLiteFloat32:
        {
            int count = n / sizeof(float);
            ESP_LOGI(TAG, "FLOAT32 tensor (%d elements)", count);
            for (int i = 0; i < count && i < 32; i++) {
                ESP_LOGI(TAG,"[%d] value=%f", i,tensor->data.f[i]);
            }
        }
        break;

    default:
        ESP_LOGW(TAG, "Unsupported tensor type: %d", tensor->type);
        break;
    }
}

void run_model(
    const unsigned char *model_data,
    unsigned int model_len,
    uint8_t *arena,
    const char *name)
{
    ESP_LOGI(TAG, "Loading model: %s", name);

    const tflite::Model *model = tflite::GetModel(model_data);
    if (model->version() != TFLITE_SCHEMA_VERSION) {
        ESP_LOGE(TAG, "Model schema %d not equal to runtime schema %d",
                 model->version(), TFLITE_SCHEMA_VERSION);
        return;
    }

    // static tflite::AllOpsResolver resolver;
    tflite::MicroMutableOpResolver<10> resolver;
    // use `check_tflite.py` to list the ops used in the model and add them here
    resolver.AddAdd();
    resolver.AddConcatenation();
    resolver.AddConv2D();
    resolver.AddDepthwiseConv2D();
    resolver.AddLogistic();
    resolver.AddMean();
    resolver.AddMul();
    resolver.AddPad();
    resolver.AddReduceMax();
    resolver.AddResizeBilinear();
    resolver.AddStridedSlice();

    tflite::MicroInterpreter interpreter(model, resolver, arena, kTensorArenaSize);
    TfLiteStatus allocate_status = interpreter.AllocateTensors();
    if (allocate_status != kTfLiteOk) {
        ESP_LOGE(TAG, "AllocateTensors() failed for %s", name);
        return;
    }

    TfLiteTensor *input = interpreter.input(0);
    ESP_LOGI(TAG, "%s input: type=%d, dims=[%d,%d,%d,%d]",
        name,
        input->type,
        input->dims->data[0],
        input->dims->data[1],
        input->dims->data[2],
        input->dims->data[3]);

    // ===== FEED BLANK IMAGE =====
    memset(input->data.int8, 0, input->bytes);

    ESP_LOGI(TAG, "%s running inference...", name);
    if (interpreter.Invoke() != kTfLiteOk) {
        ESP_LOGE(TAG, "Invoke failed for %s", name);
        return;
    }

    // ===== HANDLE MULTIPLE OUTPUTS =====
    int output_count = interpreter.outputs_size();
    ESP_LOGI(TAG, "%s has %d output tensors", name, output_count);

    for (int i = 0; i < output_count; i++) {
        TfLiteTensor *out = interpreter.output(i);

        char label[32];
        snprintf(label, sizeof(label), "%s OUT%d", name, i);

        dump_output_tensor(out, label);
    }
}

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "Starting triple-model test");

   static uint8_t tensor_arena_m1[kTensorArenaSize];
   run_model(_workspace_weights_best_triple_i256_m1_int8_pth,
    _workspace_weights_best_triple_i256_m1_int8_pth_len,
    tensor_arena_m1,
    "MODEL M1");

    // static uint8_t tensor_arena_m2[kTensorArenaSize];
    // run_model(_workspace_weights_best_triple_i256_m2_int8_pth,
    //           _workspace_weights_best_triple_i256_m2_int8_pth_len,
    //           tensor_arena_m2,
    //           "MODEL M2");

    // static uint8_t tensor_arena_m3[kTensorArenaSize];
    // run_model(_workspace_weights_best_triple_i256_m3_int8_pth,
    //           _workspace_weights_best_triple_i256_m3_int8_pth_len,
    //           tensor_arena_m3,
    //           "MODEL M3");

    ESP_LOGI(TAG, "All models executed");
}