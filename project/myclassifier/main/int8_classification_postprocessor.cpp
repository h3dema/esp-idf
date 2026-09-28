#include "esp_log.h"
#include "int8_classification_postprocessor.h"


// ---------------------------------------------------------
//  Int8ClassificationPostprocessor Implementation
// ---------------------------------------------------------

Int8ClassificationPostprocessor::Int8ClassificationPostprocessor(
        const dl::TensorBase* out_tensor,
        const char** labels,
        int num_classes)
    : out(out_tensor),
      class_labels(labels),
      n(num_classes),
      best_idx(0),
      best_val(-128)
{
    parse_logits();
}

void Int8ClassificationPostprocessor::parse_logits()
{
    logits.resize(n);

    const int8_t* d = reinterpret_cast<const int8_t*>(out->data);

    best_val = d[0];
    best_idx = 0;

    for (int i = 0; i < n; i++) {
        logits[i] = d[i];

        if (d[i] > best_val) {
            best_val = d[i];
            best_idx = i;
        }
    }
}

int Int8ClassificationPostprocessor::get_class_index() const
{
    return best_idx;
}

const char* Int8ClassificationPostprocessor::get_label() const
{
    return class_labels[best_idx];
}

int Int8ClassificationPostprocessor::get_logit() const
{
    return best_val;
}

const std::vector<int>& Int8ClassificationPostprocessor::get_all_logits() const
{
    return logits;
}
