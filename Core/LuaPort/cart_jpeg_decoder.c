#include "cart_jpeg_decoder.h"

#include <stddef.h>

#include "dma2d.h"
#include "jpeg.h"
#include "main.h"
#include "xhgc_cart.h"

#define JPEG_DECODE_TIMEOUT_MS 500u
#define DMA2D_CONVERT_TIMEOUT_MS 100u

static uint32_t align_up(uint32_t value, uint32_t alignment)
{
  return (value + alignment - 1u) & ~(alignment - 1u);
}

uint32_t CartJpegDecoder_ScratchSize(uint16_t width, uint16_t height)
{
  uint64_t size = (uint64_t)align_up(width, 16u) *
                  (uint64_t)align_up(height, 16u) * 3u;
  return size <= UINT32_MAX ? (uint32_t)size : 0u;
}

static void clean_cache(const void *pointer, uint32_t size)
{
#if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1U)
  uintptr_t start = (uintptr_t)pointer & ~(uintptr_t)31u;
  uintptr_t end = ((uintptr_t)pointer + size + 31u) & ~(uintptr_t)31u;
  SCB_CleanDCache_by_Addr((uint32_t *)start, (int32_t)(end - start));
#else
  (void)pointer;
  (void)size;
#endif
}

static void clean_invalidate_cache(void *pointer, uint32_t size)
{
#if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1U)
  uintptr_t start = (uintptr_t)pointer & ~(uintptr_t)31u;
  uintptr_t end = ((uintptr_t)pointer + size + 31u) & ~(uintptr_t)31u;
  SCB_CleanInvalidateDCache_by_Addr((uint32_t *)start,
                                   (int32_t)(end - start));
#else
  (void)pointer;
  (void)size;
#endif
}

static void invalidate_cache(void *pointer, uint32_t size)
{
#if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1U)
  uintptr_t start = (uintptr_t)pointer & ~(uintptr_t)31u;
  uintptr_t end = ((uintptr_t)pointer + size + 31u) & ~(uintptr_t)31u;
  SCB_InvalidateDCache_by_Addr((uint32_t *)start, (int32_t)(end - start));
#else
  (void)pointer;
  (void)size;
#endif
}

static bool dma2d_ycbcr_to_bgra(const JPEG_ConfTypeDef *info,
                                void *source, void *destination,
                                uint32_t destination_size)
{
  uint32_t css;
  uint32_t mcu_width;
  DMA2D_InitTypeDef saved_init = hdma2d.Init;
  DMA2D_LayerCfgTypeDef saved_layer = hdma2d.LayerCfg[1];
  void (*saved_callback)(DMA2D_HandleTypeDef *) = hdma2d.XferCpltCallback;

  if (info->ChromaSubsampling == JPEG_444_SUBSAMPLING) {
    css = DMA2D_NO_CSS;
    mcu_width = 8u;
  } else if (info->ChromaSubsampling == JPEG_420_SUBSAMPLING) {
    css = DMA2D_CSS_420;
    mcu_width = 16u;
  } else {
    return false;
  }

  clean_cache(source, CartJpegDecoder_ScratchSize(
                          (uint16_t)info->ImageWidth,
                          (uint16_t)info->ImageHeight));
  clean_invalidate_cache(destination, destination_size);

  hdma2d.Init.Mode = DMA2D_M2M_PFC;
  hdma2d.Init.ColorMode = DMA2D_OUTPUT_ARGB8888;
  hdma2d.Init.OutputOffset = 0u;
  hdma2d.Init.AlphaInverted = DMA2D_REGULAR_ALPHA;
  hdma2d.Init.RedBlueSwap = DMA2D_RB_REGULAR;
  hdma2d.LayerCfg[1].AlphaMode = DMA2D_REPLACE_ALPHA;
  hdma2d.LayerCfg[1].InputAlpha = 0xFFu;
  hdma2d.LayerCfg[1].InputColorMode = DMA2D_INPUT_YCBCR;
  hdma2d.LayerCfg[1].ChromaSubSampling = css;
  hdma2d.LayerCfg[1].InputOffset =
      align_up(info->ImageWidth, mcu_width) - info->ImageWidth;
  hdma2d.LayerCfg[1].RedBlueSwap = DMA2D_RB_REGULAR;
  hdma2d.LayerCfg[1].AlphaInverted = DMA2D_REGULAR_ALPHA;

  bool ok = HAL_DMA2D_Init(&hdma2d) == HAL_OK &&
      HAL_DMA2D_ConfigLayer(&hdma2d, 1u) == HAL_OK &&
      HAL_DMA2D_Start(&hdma2d, (uint32_t)(uintptr_t)source,
                      (uint32_t)(uintptr_t)destination,
                      info->ImageWidth, info->ImageHeight) == HAL_OK &&
      HAL_DMA2D_PollForTransfer(&hdma2d, DMA2D_CONVERT_TIMEOUT_MS) == HAL_OK;
  invalidate_cache(destination, destination_size);

  hdma2d.Init = saved_init;
  hdma2d.LayerCfg[1] = saved_layer;
  (void)HAL_DMA2D_Init(&hdma2d);
  (void)HAL_DMA2D_ConfigLayer(&hdma2d, 1u);
  hdma2d.XferCpltCallback = saved_callback;
  return ok;
}

bool CartJpegDecoder_DecodeXimgV2(const void *blob,
                                  uint32_t blob_size,
                                  uint8_t storage_format,
                                  uint16_t expected_width,
                                  uint16_t expected_height,
                                  void *output_bgra,
                                  uint32_t output_size,
                                  void *scratch_ycbcr,
                                  uint32_t scratch_size,
                                  const char **error)
{
  XHGC_XimgV2 image;
  JPEG_ConfTypeDef info = {0};
  const uint8_t *bytes = (const uint8_t *)blob;
  uint64_t expected_output = (uint64_t)expected_width * expected_height * 4u;
  uint32_t needed_scratch =
      CartJpegDecoder_ScratchSize(expected_width, expected_height);

  if (error) *error = NULL;
  if (!blob || !output_bgra || !scratch_ycbcr || needed_scratch == 0u ||
      output_size != expected_output || scratch_size < needed_scratch ||
      xhgc_ximg_v2_parse(blob, blob_size, storage_format, expected_width,
                         expected_height, &image) != XHGC_CART_OK) {
    if (error) *error = "invalid XIMG v2 image";
    return false;
  }

  if (HAL_JPEG_Decode(&hjpeg, (uint8_t *)(uintptr_t)(bytes + image.jpeg_offset),
                      image.jpeg_size, scratch_ycbcr, scratch_size,
                      JPEG_DECODE_TIMEOUT_MS) != HAL_OK ||
      HAL_JPEG_GetInfo(&hjpeg, &info) != HAL_OK ||
      info.ColorSpace != JPEG_YCBCR_COLORSPACE ||
      info.ImageWidth != expected_width || info.ImageHeight != expected_height ||
      (info.ChromaSubsampling != JPEG_444_SUBSAMPLING &&
       info.ChromaSubsampling != JPEG_420_SUBSAMPLING)) {
    if (error) *error = "hardware JPEG decode failed or unsupported sampling";
    return false;
  }
  if (!dma2d_ycbcr_to_bgra(&info, scratch_ycbcr, output_bgra, output_size)) {
    if (error) *error = "DMA2D YCbCr conversion failed";
    return false;
  }
  if (storage_format == XHGC_IMG_JPEG_A8) {
    uint8_t *pixels = (uint8_t *)output_bgra;
    const uint8_t *alpha = bytes + image.a8_offset;
    uint32_t pixel_count = (uint32_t)expected_width * expected_height;
    for (uint32_t i = 0u; i < pixel_count; ++i) pixels[i * 4u + 3u] = alpha[i];
  }
  return true;
}
