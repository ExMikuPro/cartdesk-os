#pragma once

#include <stdbool.h>
#include <stdint.h>

uint32_t CartJpegDecoder_ScratchSize(uint16_t width, uint16_t height);

bool CartJpegDecoder_DecodeXimgV2(const void *blob,
                                  uint32_t blob_size,
                                  uint8_t storage_format,
                                  uint16_t expected_width,
                                  uint16_t expected_height,
                                  void *output_bgra,
                                  uint32_t output_size,
                                  void *scratch_ycbcr,
                                  uint32_t scratch_size,
                                  const char **error);
