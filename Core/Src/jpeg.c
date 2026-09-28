/* Generated-compatible JPEG peripheral initialization. Runtime decode policy
 * stays in Core/LuaPort/cart_jpeg_decoder.c. */
#include "jpeg.h"

JPEG_HandleTypeDef hjpeg;

void MX_JPEG_Init(void)
{
  hjpeg.Instance = JPEG;
  if (HAL_JPEG_Init(&hjpeg) != HAL_OK) Error_Handler();
}

void HAL_JPEG_MspInit(JPEG_HandleTypeDef *jpegHandle)
{
  if (jpegHandle->Instance == JPEG) __HAL_RCC_JPGDECEN_CLK_ENABLE();
}

void HAL_JPEG_MspDeInit(JPEG_HandleTypeDef *jpegHandle)
{
  if (jpegHandle->Instance == JPEG) __HAL_RCC_JPGDECEN_CLK_DISABLE();
}
