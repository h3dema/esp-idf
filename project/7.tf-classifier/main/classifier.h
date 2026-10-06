#pragma once

#include <cstdint>

// Forward declaration of TFLite model struct wrapper
namespace dl {
    struct Model {
        // Raw .tflite buffer loaded from SD card
        uint8_t *user_data = nullptr;

        // Parsed TFLite model pointer
        const void *tflite_model = nullptr;

        // Destructor frees model buffer
        ~Model() {
            if (user_data) {
                free(user_data);
                user_data = nullptr;
            }
        }
    };
}

/**
 * @brief Load a .tflite model from the SD card.
 *
 * @param model_path Path to the .tflite file (e.g. "/sdcard/models/model.tflite").
 * @return Pointer to the loaded model wrapper, or nullptr on failure.
 */
dl::Model *load_model(const char *model_path);

/**
 * @brief Run classification on a decoded RGB888 image and log the result.
 *
 * @param model     Loaded TFLite model wrapper.
 * @param img_data  RGB888 image buffer.
 * @param width     Image width.
 * @param height    Image height.
 * @param filename  Source filename (used only for logging).
 */
void classify_image(dl::Model *model,
                    uint8_t *img_data,
                    uint16_t width,
                    uint16_t height,
                    const char *filename);
