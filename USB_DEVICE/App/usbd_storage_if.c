#include "usbd_storage_if.h"

#include "diskio.h"
#include "ff_gen_drv.h"
#include "sd_diskio.h"

#define STORAGE_LUN_COUNT 1
#define STORAGE_LUN_SD    0u

static const int8_t s_inquiry_data[] = {
  0x00, 0x80, 0x02, 0x02, STANDARD_INQUIRY_DATA_LEN - 5,
  0x00, 0x00, 0x00,
  'C', 'a', 'r', 't', 'D', 'e', 's', 'k',
  'S', 'D', ' ', 'C', 'a', 'r', 'd', ' ',
  'M', 'S', 'C', ' ', ' ', ' ', ' ', ' ',
  '0', '.', '1', '0'
};

static int8_t storage_init(uint8_t lun)
{
  if (lun != STORAGE_LUN_SD) return USBD_FAIL;
  return SD_Driver.disk_initialize(STORAGE_LUN_SD) == 0u
             ? USBD_OK : USBD_FAIL;
}

static int8_t storage_get_capacity(uint8_t lun, uint32_t *block_num,
                                   uint16_t *block_size)
{
  DWORD sectors = 0u;
  WORD sector_size = 0u;

  if (lun != STORAGE_LUN_SD || block_num == NULL || block_size == NULL) {
    return USBD_FAIL;
  }
  if (SD_Driver.disk_ioctl(STORAGE_LUN_SD, GET_SECTOR_COUNT, &sectors) != RES_OK ||
      SD_Driver.disk_ioctl(STORAGE_LUN_SD, GET_SECTOR_SIZE, &sector_size) != RES_OK ||
      sectors == 0u || sector_size == 0u) {
    return USBD_FAIL;
  }
  *block_num = (uint32_t)sectors;
  *block_size = (uint16_t)sector_size;
  return USBD_OK;
}

static int8_t storage_is_ready(uint8_t lun)
{
  if (lun != STORAGE_LUN_SD) return USBD_FAIL;
  return (SD_Driver.disk_status(STORAGE_LUN_SD) & (STA_NOINIT | STA_NODISK)) == 0u
             ? USBD_OK : USBD_FAIL;
}

static int8_t storage_is_write_protected(uint8_t lun)
{
  if (lun != STORAGE_LUN_SD) return USBD_FAIL;
  return (SD_Driver.disk_status(STORAGE_LUN_SD) & STA_PROTECT) != 0u
             ? USBD_FAIL : USBD_OK;
}

static int8_t storage_read(uint8_t lun, uint8_t *buf, uint32_t block_addr,
                           uint16_t block_count)
{
  if (lun != STORAGE_LUN_SD || buf == NULL || block_count == 0u) {
    return USBD_FAIL;
  }
  return SD_Driver.disk_read(STORAGE_LUN_SD, buf, block_addr, block_count) == RES_OK
             ? USBD_OK : USBD_FAIL;
}

static int8_t storage_write(uint8_t lun, uint8_t *buf, uint32_t block_addr,
                            uint16_t block_count)
{
  if (lun != STORAGE_LUN_SD || buf == NULL || block_count == 0u) {
    return USBD_FAIL;
  }
  return SD_Driver.disk_write(STORAGE_LUN_SD, buf, block_addr, block_count) == RES_OK
             ? USBD_OK : USBD_FAIL;
}

static int8_t storage_get_max_lun(void)
{
  return STORAGE_LUN_COUNT - 1;
}

USBD_StorageTypeDef USBD_Storage_Interface_fops_HS = {
  storage_init,
  storage_get_capacity,
  storage_is_ready,
  storage_is_write_protected,
  storage_read,
  storage_write,
  storage_get_max_lun,
  (int8_t *)s_inquiry_data
};
