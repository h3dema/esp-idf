#pragma once

#include <vector>
#include "dl_tensor_base.hpp"

class Int8ClassificationPostprocessor {
public:
    Int8ClassificationPostprocessor(const dl::TensorBase* out_tensor,
                                    const char** labels,
                                    int num_classes);

    int get_class_index() const;
    const char* get_label() const;
    int get_logit() const;

    const std::vector<int>& get_all_logits() const;

private:
    const dl::TensorBase* out;
    const char** class_labels;
    int n;

    std::vector<int> logits;
    int best_idx;
    int best_val;

    void parse_logits();
};
