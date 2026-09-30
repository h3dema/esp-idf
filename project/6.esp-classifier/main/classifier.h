#pragma once

#include <cstdint>

#include "dl_model_base.hpp"

/**
 * @brief Load an .espdl model from the SD card.
 *
 * @param model_path Path to the .espdl file (e.g. "/sdcard/models/model.espdl").
 * @return Pointer to the loaded model, or nullptr on failure.
 */
dl::Model *load_model(const char *model_path);

/**
 * @brief Run classification on a decoded RGB888 image and log the result.
 *
 * @param model     Loaded ESP-DL model.
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
