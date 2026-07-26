/* Included inside the main SECTIONS block after RAM_D2 has been declared. */
.usb_ram (NOLOAD) :
{
  . = ALIGN(32);
  __usb_ram_start__ = .;

  *(.usb_ram_data .usb_ram_data.*)
  *USB_DEVICE*(.bss .bss.* COMMON)
  *STM32_USB_Device_Library*(.bss .bss.* COMMON)
  *stm32h7xx_hal_pcd*.obj(.bss .bss.* COMMON)
  *stm32h7xx_ll_usb*.obj(.bss .bss.* COMMON)

  . = ALIGN(32);
  __usb_ram_end__ = .;
} >RAM_D2
