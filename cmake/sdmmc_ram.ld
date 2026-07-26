/* SDMMC1 IDMA cannot access Cortex-M7 DTCMRAM. */
.sdmmc_ram (NOLOAD) :
{
  . = ALIGN(32);
  __sdmmc_ram_start__ = .;

  *(.sdmmc_ram_data .sdmmc_ram_data.*)
  *fatfs.c.obj(.bss .bss.* COMMON)
  *sd_diskio.c.obj(.bss .bss.* COMMON)

  . = ALIGN(32);
  __sdmmc_ram_end__ = .;
} >RAM
