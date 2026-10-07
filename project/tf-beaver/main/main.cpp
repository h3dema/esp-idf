#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// ===== TFLITE MICRO =====
#include "tensorflow/lite/schema/schema_generated.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/micro/micro_log.h"

// ===== MODEL HEADERS =====
#include "best_triple_i256_m1_int8.h"
#include "best_triple_i256_m2_int8.h"
#include "best_triple_i256_m3_int8.h"

static const char *TAG = "TRIPLE_MODEL";

// ===== ARENA SIZE =====
// Increase if your models need more RAM
constexpr int kTensorArenaSize = 3 * 1024 * 1024;


// We create one result struct to hold everything needed from a model (may have unused fields depending on the model).
// The result struct is returned to the caller, which can then pass the relevant outputs to the next model.
//
// image → M1 → logits1, high_res
// high_res → M2 → logits2, mid_res
// high_res + mid_res (two inputs) → M3 → logits3
//
// ModelResult holds the outputs of each model, and the inputs for the next model are passed as arguments to the next function.
struct TensorBuffer {
    int8_t* data = nullptr;
    size_t bytes = 0;
    float scale = 0.0f;
    int zero_point = 0;
};

struct ModelResult {
    TensorBuffer logits;
    TensorBuffer high_res;
    TensorBuffer mid_res;
};


static bool copy_tensor(
    TfLiteTensor* src,
    TensorBuffer& dst)
{
    if (!src)
        return false;

    dst.bytes = src->bytes;
    dst.scale = src->params.scale;
    dst.zero_point = src->params.zero_point;

    dst.data = static_cast<int8_t*>(
        heap_caps_malloc(
            dst.bytes,
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));

    if (!dst.data) {
        ESP_LOGE(TAG, "Failed to allocate %u bytes", (unsigned)dst.bytes);
        return false;
    }
    memcpy(dst.data, src->data.int8, dst.bytes);
    return true;
}


void dump_output_tensor(const TensorBuffer& tensor, const char* label)
{
    constexpr int kMaxPrint = 8;

    ESP_LOGI(TAG, "===== %s =====", label);
    ESP_LOGI(TAG,
             "bytes=%u scale=%f zero_point=%d",
             static_cast<unsigned>(tensor.bytes),
             tensor.scale,
             tensor.zero_point);

    if (tensor.data == nullptr) {
        ESP_LOGW(TAG, "Tensor data is nullptr");
        return;
    }

    int count = tensor.bytes / sizeof(int8_t);

    ESP_LOGI(TAG, "INT8 tensor (%d elements)", count);

    for (int i = 0; i < count && i < kMaxPrint; ++i) {
        float dequant =
            (static_cast<int>(tensor.data[i]) - tensor.zero_point) *
            tensor.scale;

        ESP_LOGI(TAG,
                 "[%d] raw=%d dequant=%f",
                 i,
                 static_cast<int>(tensor.data[i]),
                 dequant);
    }
}


ModelResult run_m1(
    const unsigned char *model_data,
    unsigned int model_len,
    uint8_t *arena)
{
    ModelResult result = {};

    ESP_LOGI(TAG, "Loading model:M1");
    const tflite::Model *model = tflite::GetModel(model_data);
    if (model->version() != TFLITE_SCHEMA_VERSION) {
        ESP_LOGE(TAG, "Model schema %d not equal to runtime schema %d", model->version(), TFLITE_SCHEMA_VERSION);
        return result;
    }

    // static tflite::AllOpsResolver resolver;
    tflite::MicroMutableOpResolver<11> resolver;
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
        ESP_LOGE(TAG, "AllocateTensors() failed for M1");
        return result;
    }

    // ===== FEED BLANK IMAGE =====
    TfLiteTensor *input = interpreter.input(0);
    ESP_LOGI(TAG, "M1 input: type=%d, dims=[%d,%d,%d,%d]",
        input->type,
        input->dims->data[0],
        input->dims->data[1],
        input->dims->data[2],
        input->dims->data[3]);
    memset(input->data.int8, 0, input->bytes);  // Fill input tensor with zeros

    ESP_LOGI(TAG, "M1 running inference...");
    if (interpreter.Invoke() != kTfLiteOk) {
        ESP_LOGE(TAG, "Invoke failed for M1");
        return result;
    }

    // ===== HANDLE MULTIPLE OUTPUTS =====
    int output_count = interpreter.outputs_size();
    ESP_LOGI(TAG, "M1 has %d output tensors", output_count);
    // M1 has 2 outputs: logits1 + high_res
    TfLiteTensor* high_res_tensor = interpreter.output(0);
    TfLiteTensor* logits_tensor  = interpreter.output(1);
    ESP_LOGI(TAG, "M1 logits   : %d bytes", logits_tensor->bytes);
    ESP_LOGI(TAG, "M1 high_res : %d bytes", high_res_tensor->bytes);

    if (!copy_tensor(high_res_tensor, result.high_res)) {
        return result;
    }

    if (!copy_tensor(logits_tensor, result.logits)) {
        heap_caps_free(result.high_res.data);
        result.high_res.data = nullptr;
        return result;
    }
    return result;
}

ModelResult run_m2(
    const unsigned char *model_data,
    unsigned int model_len,
    uint8_t *arena,
    const TensorBuffer& high_res)
{
    static const char * name = "m2";
    ModelResult result = {};

    ESP_LOGI(TAG, "Loading model: MODEL M2");
    const tflite::Model *model = tflite::GetModel(model_data);
    if (model->version() != TFLITE_SCHEMA_VERSION) {
        ESP_LOGE(TAG, "Model schema %d not equal to runtime schema %d",
                 model->version(), TFLITE_SCHEMA_VERSION);
        return result;
    }
    tflite::MicroMutableOpResolver<11> resolver;
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

    static tflite::MicroInterpreter interpreter(model, resolver, arena, kTensorArenaSize);
    TfLiteStatus allocate_status = interpreter.AllocateTensors();
    if (allocate_status != kTfLiteOk) {
        ESP_LOGE(TAG, "AllocateTensors() failed for %s", name);
        return result;
    }

    TfLiteTensor *input = interpreter.input(0);
    // Copy high_res → M2 input
    memcpy(input->data.int8, high_res.data, input->bytes);

    interpreter.Invoke();

    // M2 has 2 outputs: logits2 + mid_res
    int output_count = interpreter.outputs_size();
    ESP_LOGI(TAG, "M2 has %d output tensors", output_count);
    TfLiteTensor* mid_res_tensor = interpreter.output(0);
    TfLiteTensor* logits_tensor  = interpreter.output(1);
    ESP_LOGI(TAG, "M2 logits   %d bytes", logits_tensor->bytes);
    ESP_LOGI(TAG, "M2 mid_res %d bytes", mid_res_tensor->bytes);

    if (!copy_tensor(mid_res_tensor, result.mid_res)) {
        return result;
    }

    if (!copy_tensor(logits_tensor, result.logits)) {
        heap_caps_free(result.mid_res.data);
        result.mid_res.data = nullptr;
        return result;
    }

    return result;
}



ModelResult run_m3(
    const unsigned char *model_data,
    unsigned int model_len,
    uint8_t *arena,
    const TensorBuffer& high_res,
    const TensorBuffer& mid_res)
{
    ModelResult result = {};

    ESP_LOGI(TAG, "Loading model: MODEL M3");
    const tflite::Model *model = tflite::GetModel(model_data);
    if (model->version() != TFLITE_SCHEMA_VERSION) {
        ESP_LOGE(TAG, "Model schema %d not equal to runtime schema %d",
                 model->version(), TFLITE_SCHEMA_VERSION);
        return result;
    }

    ESP_LOGI(TAG, "M3: will create resolver.");
    tflite::MicroMutableOpResolver<11> resolver;
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

    ESP_LOGI(TAG, "M3: will create interpreter.");
    static tflite::MicroInterpreter interpreter(model, resolver, arena, kTensorArenaSize);
    TfLiteStatus allocate_status = interpreter.AllocateTensors();
    if (allocate_status != kTfLiteOk) {
        ESP_LOGE(TAG, "AllocateTensors() failed for M3");
        return result;
    }

    // M3 has TWO inputs:
    TfLiteTensor *input0 = interpreter.input(0); // high_res
    TfLiteTensor *input1 = interpreter.input(1); // mid_res

    ESP_LOGI(TAG, "M3 input0 (high_res): bytes=%d", input0->bytes);
    ESP_LOGI(TAG, "M3 input1 (mid_res):  bytes=%d", input1->bytes);

    // Copy high_res → input0
    memcpy(input0->data.int8, high_res.data, input0->bytes);
    ESP_LOGI(TAG, "M3: after copying high_res.");

    // Copy mid_res → input1
    memcpy(input1->data.int8, mid_res.data, input1->bytes);
    ESP_LOGI(TAG, "M3: after copying mid_res.");

    ESP_LOGI(TAG, "MODEL M3 running inference...");
    if (interpreter.Invoke() != kTfLiteOk) {
        ESP_LOGE(TAG, "Invoke failed for MODEL M3");
        return result;
    }

    TfLiteTensor* logits_tensor = interpreter.output(0);
    ESP_LOGI(TAG, "M3 logits   %d bytes", logits_tensor->bytes);
    if (!copy_tensor(logits_tensor, result.logits)) {
        return result;
    }
    return result;
}


extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "Starting triple-model test");

    uint8_t *tensor_arena =
        static_cast<uint8_t *>(heap_caps_malloc(
            kTensorArenaSize,
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    ESP_LOGI(TAG, "Free PSRAM: %u", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    if (tensor_arena == nullptr) {
        ESP_LOGE(TAG, "Failed to allocate tensor arena for M1");
        return;
    }

    // ---- M1 ----
    ModelResult r1 = run_m1(
        _workspace_weights_best_triple_i256_m1_int8_pth,
        _workspace_weights_best_triple_i256_m1_int8_pth_len,
        tensor_arena
    );
    dump_output_tensor(r1.logits, "M1 logits1");

    // ---- M2 ----
    ModelResult r2 = run_m2(
        _workspace_weights_best_triple_i256_m2_int8_pth,
        _workspace_weights_best_triple_i256_m2_int8_pth_len,
        tensor_arena,
        r1.high_res);

    dump_output_tensor(r2.logits, "M2 logits2");
    dump_output_tensor(r2.mid_res, "M2 mid_res");

    // ---- M3 ----
    ModelResult r3 = run_m3(
        _workspace_weights_best_triple_i256_m3_int8_pth,
        _workspace_weights_best_triple_i256_m3_int8_pth_len,
        tensor_arena,
        r1.high_res,
        r2.mid_res
    );

    dump_output_tensor(r3.logits, "M3 logits3");

    heap_caps_free(r1.high_res.data);
    heap_caps_free(r1.logits.data);

    heap_caps_free(r2.mid_res.data);
    heap_caps_free(r2.logits.data);

    heap_caps_free(r3.logits.data);

    heap_caps_free(tensor_arena);

    ESP_LOGI(TAG, "All models executed");
}