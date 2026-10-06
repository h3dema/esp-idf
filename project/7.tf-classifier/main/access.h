#pragma once

#include <cstdint>
#include "esp_err.h"

// --- SD Card Configuration (adjust to your hardware) ---
#define PIN_NUM_MOSI    GPIO_NUM_9
#define PIN_NUM_MISO    GPIO_NUM_8
#define PIN_NUM_CLK     GPIO_NUM_7
#define PIN_NUM_CS      GPIO_NUM_21
#define MOUNT_POINT     "/sdcard"

/**
 * @brief Mount the SD card in SPI mode.
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t mount_sdcard(void);

/**
 * @brief Read a JPEG file from the SD card and decode it to RGB888.
 *
 * @param filepath      Path to the JPEG file (e.g. "/sdcard/test.jpg").
 * @param out_width     Output: decoded image width.
 * @param out_height    Output: decoded image height.
 * @return Pointer to the decoded RGB888 buffer (must be freed by the caller),
 *         or nullptr on failure.
 */
uint8_t *read_and_decode_jpeg(const char *filepath,
                              uint16_t *out_width,
                              uint16_t *out_height);
