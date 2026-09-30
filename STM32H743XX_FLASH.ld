/*
******************************************************************************
**

**  File        : LinkerScript.ld
**
**  Author		: STM32CubeMX
**
**  Abstract    : Linker script for STM32H743XIHx series
**                2048Kbytes FLASH and 1056Kbytes RAM
**
**                Set heap size, stack size and stack location according
**                to application requirements.
**
**                Set memory bank area and size if external memory is used.
**
**  Target      : STMicroelectronics STM32
**
**  Distribution: The file is distributed “as is,” without any warranty
**                of any kind.
**
*****************************************************************************
** @attention
**
** <h2><center>&copy; COPYRIGHT(c) 2025 STMicroelectronics</center></h2>
**
** Redistribution and use in source and binary forms, with or without modification,
** are permitted provided that the following conditions are met:
**   1. Redistributions of source code must retain the above copyright notice,
**      this list of conditions and the following disclaimer.
**   2. Redistributions in binary form must reproduce the above copyright notice,
**      this list of conditions and the following disclaimer in the documentation
**      and/or other materials provided with the distribution.
**   3. Neither the name of STMicroelectronics nor the names of its contributors
**      may be used to endorse or promote products derived from this software
**      without specific prior written permission.
**
** THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
** AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
** IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
** DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
** FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
** DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
** SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
** CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
** OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
** OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
**
*****************************************************************************
*/

/* Entry Point */
ENTRY(Reset_Handler)

/* Specify the memory areas */
MEMORY
{
DTCMRAM (xrw)      : ORIGIN = 0x20000000, LENGTH = 128K
RAM (xrw)      : ORIGIN = 0x24000000, LENGTH = 512K
RAM_D2 (xrw)      : ORIGIN = 0x30000000, LENGTH = 288K
RAM_D3 (xrw)      : ORIGIN = 0x38000000, LENGTH = 64K
ITCMRAM (xrw)      : ORIGIN = 0x00000000, LENGTH = 64K
FLASH (rx)      : ORIGIN = 0x8000000, LENGTH = 2048K
/* SDRAM 固定分区 - 依据 Core/Inc/sdram_layout.h, 物理范围: 0xD0000000~0xD3FFFFFF
 * 数值必须与 sdram_layout.h 保持一致；前四块顺序不可调整。 */
SDRAM_LAYER0_FB  (xrw) : ORIGIN = 0xD0000000, LENGTH = 0x00177000  /* 1.46 MiB - Layer0 静态帧缓冲 (Launcher 固定 UI) */
SDRAM_LVGL_FB_A  (xrw) : ORIGIN = 0xD0177000, LENGTH = 0x00177000  /* 1.46 MiB - LVGL 双缓冲 A */
SDRAM_LVGL_FB_B  (xrw) : ORIGIN = 0xD02EE000, LENGTH = 0x00177000  /* 1.46 MiB - LVGL 双缓冲 B */
SDRAM_LAUNCHER_STRIP (xrw) : ORIGIN = 0xD0465000, LENGTH = 0x00400000  /* 4 MiB - Launcher 水平 cached strip 预留 (实际用 0x38E8C0) */
SDRAM_LVGL_HEAP  (xrw) : ORIGIN = 0xD0865000, LENGTH = 0x01000000  /* 16   MiB - 预留/未来LVGL堆 */
SDRAM_DMA_POOL   (xrw) : ORIGIN = 0xD1865000, LENGTH = 0x00400000  /*  4   MiB - DMA专用区   */
SDRAM_LAUNCHER   (xrw) : ORIGIN = 0xD1C65000, LENGTH = 0x00400000  /*  4   MiB - 图标缓存    */
SDRAM_APP_ARENA  (xrw) : ORIGIN = 0xD2065000, LENGTH = 0x01F9B000  /* ~31.6MiB - APP_ARENA_REST (Lua heap + resource arena + cold pool) */
}

/* Highest address of the user mode stack */
_estack = ORIGIN(DTCMRAM) + LENGTH(DTCMRAM);    /* end of RAM */
/* Generate a link error if heap and stack don't fit into RAM */
_Min_Heap_Size = 0x200;      /* required amount of heap  */
_Min_Stack_Size = 0x400; /* required amount of stack */

/* Define output sections */
SECTIONS
{
  /* The startup code goes first into FLASH */
  .isr_vector :
  {
    . = ALIGN(4);
    KEEP(*(.isr_vector)) /* Startup code */
    . = ALIGN(4);
  } >FLASH

  /* The program code and other data goes into FLASH */
  .text :
  {
    . = ALIGN(4);
    *(.text)           /* .text sections (code) */
    *(.text*)          /* .text* sections (code) */
    *(.glue_7)         /* glue arm to thumb code */
    *(.glue_7t)        /* glue thumb to arm code */
    *(.eh_frame)

    KEEP (*(.init))
    KEEP (*(.fini))

    . = ALIGN(4);
    _etext = .;        /* define a global symbols at end of code */
  } >FLASH

  /* Constant data goes into FLASH */
  .rodata :
  {
    . = ALIGN(4);
    *(.rodata)         /* .rodata sections (constants, strings, etc.) */
    *(.rodata*)        /* .rodata* sections (constants, strings, etc.) */
    . = ALIGN(4);
  } >FLASH

  /* ------------------------------------------------------------------ */
  /* SDRAM 固定分区 sections - 依据 Core/Inc/sdram_layout.h              */
  /* ------------------------------------------------------------------ */

  /* Layer0_FB / LVGL_FB_A / LVGL_FB_B / LAUNCHER_STRIP 由运行期绝对指针使用
   * （LTDC CFBAR 与 LVGL DIRECT buffer），不需要 linker section。
   * 其边界与对齐由 sdram_layout.h 的编译期断言保证，MEMORY 区域在下方
   * SDRAM_LVGL_HEAP / SDRAM_DMA_POOL / SDRAM_LAUNCHER / SDRAM_APP_ARENA
   * 中仅用于校验剩余固定分区的容量。 */

  /* LVGL_HEAP: LVGL核心堆放到片内 AXI SRAM，避免占用 DTCM 或外部 SDRAM */
  .lvgl_heap_ram (NOLOAD) :
  {
    . = ALIGN(32);
    _sLvglHeap = .;
    *(.lvgl_heap)
    *(.lvgl_heap*)
    _eLvglHeap = .;
  } >RAM

  /* Large non-DMA runtime state: RTOS heap and Lua scheduler buffers. */
  .ram_runtime (NOLOAD) :
  {
    . = ALIGN(32);
    _sRamRuntime = .;
    *(.ram_runtime)
    *(.ram_runtime*)
    . = ALIGN(32);
    _eRamRuntime = .;
  } >RAM

  /* Debug-only render-audit scratch.  Placed in RAM_D2 so the per-transfer
   * DMA2D ring never competes with the DTCM budget that production state uses,
   * and so it stays out of the DMA-reachable AXI SRAM path.  NOLOAD: the audit
   * zeroes its own state in RenderAudit_Reset(). */
  .render_audit_noinit (NOLOAD) :
  {
    . = ALIGN(32);
    _sRenderAuditNoinit = .;
    *(.render_audit_noinit)
    *(.render_audit_noinit*)
    . = ALIGN(32);
    _eRenderAuditNoinit = .;
  } >RAM_D2

  /* DMA_POOL: DMA专用区  4 MiB  64字节对齐  0xD1865000~0xD1C65000 */
  .sdram_dma_pool (NOLOAD) :
  {
    . = ALIGN(64);
    _sDmaPool = .;
    *(.dma_pool)
    *(.dma_pool*)
    _eDmaPool = .;
  } >SDRAM_DMA_POOL

  /* LAUNCHER_CACHE: 图标缓存  4 MiB  32字节对齐  0xD1C65000~0xD2065000 */
  .sdram_launcher_cache (NOLOAD) :
  {
    . = ALIGN(32);
    _sLauncherCache = .;
    *(.launcher_cache)
    *(.launcher_cache*)
    _eLauncherCache = .;
  } >SDRAM_LAUNCHER

  /* APP_ARENA_REST: 资源区 ~31.6 MiB  32字节对齐  线性分配  0xD2065000~0xD3FFFFFF
   * 内部划分: Lua heap 2 MiB / resource arena 0x159B000 / cold pool 8 MiB */
  .sdram_app_arena (NOLOAD) :
  {
    . = ALIGN(32);
    _sAppArena = .;
    *(.app_arena)
    *(.app_arena*)
    _eAppArena = .;
  } >SDRAM_APP_ARENA


  .ARM.extab (READONLY) : /* The "READONLY" keyword is only supported in GCC11 and later, remove it if using GCC10 or earlier. */
  {
    . = ALIGN(4);
    *(.ARM.extab* .gnu.linkonce.armextab.*)
    . = ALIGN(4);
  } >FLASH

  .ARM (READONLY) : /* The "READONLY" keyword is only supported in GCC11 and later, remove it if using GCC10 or earlier. */
  {
    . = ALIGN(4);
    __exidx_start = .;
    *(.ARM.exidx*)
    __exidx_end = .;
    . = ALIGN(4);
  } >FLASH

  .preinit_array (READONLY) : /* The "READONLY" keyword is only supported in GCC11 and later, remove it if using GCC10 or earlier. */
  {
    . = ALIGN(4);
    PROVIDE_HIDDEN (__preinit_array_start = .);
    KEEP (*(.preinit_array*))
    PROVIDE_HIDDEN (__preinit_array_end = .);
    . = ALIGN(4);
  } >FLASH

  .init_array (READONLY) : /* The "READONLY" keyword is only supported in GCC11 and later, remove it if using GCC10 or earlier. */
  {
    . = ALIGN(4);
    PROVIDE_HIDDEN (__init_array_start = .);
    KEEP (*(SORT(.init_array.*)))
    KEEP (*(.init_array*))
    PROVIDE_HIDDEN (__init_array_end = .);
    . = ALIGN(4);
  } >FLASH

  .fini_array (READONLY) : /* The "READONLY" keyword is only supported in GCC11 and later, remove it if using GCC10 or earlier. */
  {
    . = ALIGN(4);
    PROVIDE_HIDDEN (__fini_array_start = .);
    KEEP (*(SORT(.fini_array.*)))
    KEEP (*(.fini_array*))
    PROVIDE_HIDDEN (__fini_array_end = .);
    . = ALIGN(4);
  } >FLASH

  /* used by the startup to initialize data */
  _sidata = LOADADDR(.data);

  /* Initialized data sections goes into RAM, load LMA copy after code */
  .data :
  {
    . = ALIGN(4);
    _sdata = .;        /* create a global symbol at data start */
    *(.data)           /* .data sections */
    *(.data*)          /* .data* sections */
    *(.RamFunc)        /* .RamFunc sections */
    *(.RamFunc*)       /* .RamFunc* sections */

    . = ALIGN(4);
  } >DTCMRAM AT> FLASH

 /* Initialized TLS data section */
  .tdata : ALIGN(4)
  {
    *(.tdata .tdata.* .gnu.linkonce.td.*)
    . = ALIGN(4);
    _edata = .;        /* define a global symbol at data end */
    PROVIDE(__data_end = .);
    PROVIDE(__tdata_end = .);
  } >DTCMRAM AT> FLASH

  PROVIDE( __tdata_start = ADDR(.tdata) );
  PROVIDE( __tdata_size = __tdata_end - __tdata_start );

  PROVIDE( __data_start = ADDR(.data) );
  PROVIDE( __data_size = __data_end - __data_start );

  PROVIDE( __tdata_source = LOADADDR(.tdata) );
  PROVIDE( __tdata_source_end = LOADADDR(.tdata) + SIZEOF(.tdata) );
  PROVIDE( __tdata_source_size = __tdata_source_end - __tdata_source );

  PROVIDE( __data_source = LOADADDR(.data) );
  PROVIDE( __data_source_end = __tdata_source_end );
  PROVIDE( __data_source_size = __data_source_end - __data_source );
  /* Uninitialized data section */
  .tbss (NOLOAD) : ALIGN(4)
  {
     /* This is used by the startup in order to initialize the .bss secion */
    _sbss = .;         /* define a global symbol at bss start */
    __bss_start__ = _sbss;
    *(.tbss .tbss.*)
    . = ALIGN(4);
    PROVIDE( __tbss_end = . );
  } >DTCMRAM

  PROVIDE( __tbss_start = ADDR(.tbss) );
  PROVIDE( __tbss_size = __tbss_end - __tbss_start );
  PROVIDE( __tbss_offset = ADDR(.tbss) - ADDR(.tdata) );

  PROVIDE( __tls_base = __tdata_start );
  PROVIDE( __tls_end = __tbss_end );
  PROVIDE( __tls_size = __tls_end - __tls_base );
  PROVIDE( __tls_align = MAX(ALIGNOF(.tdata), ALIGNOF(.tbss)) );
  PROVIDE( __tls_size_align = (__tls_size + __tls_align - 1) & ~(__tls_align - 1) );
  PROVIDE( __arm32_tls_tcb_offset = MAX(8, __tls_align) );
  PROVIDE( __arm64_tls_tcb_offset = MAX(16, __tls_align) );

  INCLUDE cmake/usb_ram.ld
  INCLUDE cmake/sdmmc_ram.ld

  .bss (NOLOAD) : ALIGN(4)
  {
    *(.bss)
    *(.bss*)
    *(COMMON)

      . = ALIGN(4);
    _ebss = .;         /* define a global symbol at bss end */
    __bss_end__ = _ebss;
      PROVIDE( __bss_end = .);
  } >DTCMRAM
  PROVIDE( __non_tls_bss_start = ADDR(.bss) );

  PROVIDE( __bss_start = __tbss_start );
  PROVIDE( __bss_size = __bss_end - __bss_start );

  /* User_heap_stack section, used to check that there is enough RAM left */
  ._user_heap_stack (NOLOAD) :
  {
    . = ALIGN(8);
    PROVIDE ( end = . );
    PROVIDE ( _end = . );
    . = . + _Min_Heap_Size;
    . = . + _Min_Stack_Size;
    . = ALIGN(8);
  } >DTCMRAM



  /* Remove information from the standard libraries */
  /DISCARD/ :
  {
    libc.a:* ( * )
    libm.a:* ( * )
    libgcc.a:* ( * )
  }


}
