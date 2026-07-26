# QFLASH A8 字体

工程默认字体使用 QFLASH 中的霞鹜臻楷 A8 字库。字体包包含字体 cmap
实际覆盖的全部 7,431 个 Unicode 码位，生成 16、20、24 px 三套字号，
其中 20 px 是 LVGL 默认字体。

## 存储布局

| QFLASH 偏移 | 大小 | 用途 |
| --- | ---: | --- |
| `0x00000000` | 最大 16 MiB | 只读 QFNT 字体包 |
| `0x01000000` | 48 MiB | littlefs |

字体通过 QUADSPI Memory-Mapped 模式从 `0x90000000` 直接读取，不复制进
MCU Flash 或 SDRAM。QFNT 包含文件头、各字号记录、按 Unicode 排序的字形
索引和逐像素 A8 位图。

## 生成字体包

主机需要 Python 3、Pillow 和 fontconfig 的 `fc-query`：

```bash
python3 tools/qflash_font/build_qflash_font.py build \
  "Font/霞鹜臻楷.ttf" \
  "Font/霞鹜臻楷-16-20-24-a8.qfnt"
```

校验已有字体包：

```bash
python3 tools/qflash_font/build_qflash_font.py inspect \
  "Font/霞鹜臻楷-16-20-24-a8.qfnt"
```

将生成的 `.qfnt` 作为二进制写入外部 QFLASH 偏移 `0x00000000`
（Memory-Mapped 地址 `0x90000000`）。具体烧录命令依赖开发板对应的
STM32CubeProgrammer External Loader；不要把文件写进 MCU 内部 Flash。

## 使用 CMake 写入字库

工程通过独立配置文件 `cmake/QFlashFont.cmake` 提供以下目标：

```bash
# 只重新生成 QFNT
cmake --build --preset Debug --target qflash_font_pack

# 构建固件，通过 OpenOCD/GDB 写入并逐块回读校验
cmake --build --preset Debug --target flash_qflash_font
```

`flash_qflash_font` 会先把当前固件烧入 MCU 内部 Flash，在
`QFlashFont_ProgrammerReady()` 处暂停，然后以 128 KiB 为单位把字体包传入
SDRAM。板端依次执行 QFLASH 擦除、页编程和回读比较，写入范围被限制在
`0x00000000..0x00FFFFFF`，不会触及从 `0x01000000` 开始的 littlefs。

这里使用 128 KiB 逻辑擦除步长，是因为当前 QUADSPI 工作在 Dual-Flash
模式：一次 64 KiB Block Erase 会同时擦除两颗 W25Q256 各自的 64 KiB。

可以在配置阶段覆盖文件路径：

```bash
cmake --preset Debug \
  -DQFLASH_FONT_SOURCE="/path/to/font.ttf" \
  -DQFLASH_FONT_PACK="/path/to/font.qfnt"
```

该目标会修改开发板的 MCU 内部 Flash 和 QFLASH，只有在开发板通过 ST-Link
连接后才应主动执行。

## 固件接口

启动时 `main.c` 在 `MX_QUADSPI_Init()` 之后初始化 QSPI NOR、进入
Memory-Mapped 模式并挂载 QFNT。挂载失败时系统继续启动，默认字体回退到
内置 Montserrat，串口会输出失败原因。

默认字体为 `qflash_font_20`。需要显式选择其他字号时：

```c
#include "qflash_font.h"

const lv_font_t *font = QFlashFont_Get(16);
if(font != NULL) {
    lv_obj_set_style_text_font(label, font, 0);
}
```

QFLASH 处于 Memory-Mapped 模式时，写入或擦除会先退出映射模式。若以后同时
启用 littlefs 写入，需要在写操作结束后重新进入映射模式，才能继续读取字体。
