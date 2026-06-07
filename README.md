<p align="center">
  <img src="Docs/assets/cartdesk-os-logo.png" alt="cartdesk os logo" width="900">
</p>

# cartdesk-os

`cartdesk-os` 是一个运行在 STM32H743 上的嵌入式桌面/启动器固件。它以 LVGL 9.5 为图形层，使用 LTDC + SDRAM 做 800x480 ARGB8888 显示，内置 Lua 运行时，并通过 SD 卡里的 `cart.bin` 读取应用标题、预览图和入口脚本。

打包器仓库：[ExMikuPro/xhgc-pack](https://github.com/ExMikuPro/xhgc-pack)

这个项目目前更像一台小型掌机/桌面终端的固件底座：上电后初始化板级外设，进入 LVGL 启动器界面，用户点击卡带槽后再启动 Lua 应用。

## 当前能力

- LVGL 9.5 图形栈，包含显示、tick、输入设备移植层。
- LTDC 双缓冲显示链路，配合 VBlank/page flip 降低撕裂。
- 64 MiB 外部 SDRAM 固定分区，用于 framebuffer、LVGL heap、DMA pool、launcher cache 和应用资源区。
- SD 卡 `cart.bin` 读取，launcher 可显示卡带标题和 200x200 ARGB8888 预览图。
- Lua VM 运行时，支持生命周期函数和 GPIO/PWM/SD/UI 等宿主 API。
- FreeRTOS/CMSIS-RTOS2 任务模型，LVGL 和 Lua 在独立任务路径中调度。
- CMake/Ninja 构建，按模块拆成显示、存储、GPIO、Lua、UI、任务等静态库。

## 硬件目标

当前工程面向以下硬件配置：

| 项目 | 配置 |
| --- | --- |
| MCU | STM32H743 |
| 屏幕 | 800x480，ARGB8888 |
| 显示 | LTDC + DMA2D |
| 外部内存 | 64 MiB SDRAM，起始地址 `0xD0000000` |
| 存储 | SD/FatFs，卡带镜像路径 `0:/cart.bin` |
| 触摸 | GT911 路径已接入 LVGL 输入层 |
| 日志 | 标准输出重定向到板级串口路径 |

硬件管脚和 CubeMX 派生配置以仓库根目录的 `cartdesk-os.ioc`、`Core/Inc/main.h` 和 `Core/Src/*` 初始化文件为准。

## 构建

需要准备：

- CMake 3.22+
- Ninja
- `arm-none-eabi-gcc` 工具链
- Python 3，用于构建后打印 SDRAM 使用情况

配置并构建 Debug 固件：

```sh
cmake --preset Debug
cmake --build --preset Debug
```

Release 构建：

```sh
cmake --preset Release
cmake --build --preset Release
```

主要产物位于：

```text
build/Debug/cartdesk-os.elf
build/Debug/cartdesk-os.map
```

构建结束后，如果本机能找到 Python 3，CMake 会自动解析 map 文件并打印 SDRAM 各分区的静态使用情况。

### Host LuaVM 工具

固件预设使用 `arm-none-eabi-gcc` 交叉编译。PC 端 `luavm` 工具通过独立的 host CMake 子构建生成，复用 `Core/LuaPort/src` 里的同版本 Lua 源码，不会加入 STM32 固件镜像。

```sh
cmake --build build/Debug --target luavm_tool
cmake --build build/Debug --target copy_luavm_to_packer
```

工具产物路径：

```text
build/host_tools/bin/luavm
packer/tools/luavm
```

Windows 下文件名为 `luavm.exe`。

常用命令：

```sh
build/host_tools/bin/luavm --compile input.lua output.luac
build/host_tools/bin/luavm --check script.lua
```

## 运行入口

固件启动后的主要路径如下：

```text
Core/Src/main.c
  -> 初始化 HAL、时钟、GPIO、LTDC、DMA2D、FMC、SDMMC、FreeRTOS 等外设
  -> StartLvglTask()
      -> lv_init()
      -> lv_port_disp_init()
      -> lv_port_indev_init()
      -> LCD_DisplayON()
      -> Launcher_Init()
      -> 周期调用 lvgl_task_handler()
      -> 周期调用 Task_LUA()，未点击卡带槽时不会初始化 Lua VM
```

`Launcher_Init()` 会创建启动器页面，并尝试从 `0:/cart.bin` 读取第一个卡带槽的标题和预览图。开机默认不创建 Lua VM；点击卡带槽后，启动器会切换到空白运行屏并保留系统 `EXIT` 按钮，`Task_LUA_StartCart("0:/cart.bin")` 会请求启动脚本，随后 `Task_LUA()` 从 `cart.bin` 的 ENTRY 段加载 luac 并启动 Lua 运行时。点击 `EXIT` 会同步停止 Lua VM、清理运行屏上的 LVGL 对象，并回到 launcher。

## cart.bin

`cart.bin` 是项目里的“卡带镜像”。当前固件会从 SD 卡根目录读取：

- Header 里的标题字段，用于 launcher 显示。
- 200x200 BGRA8888 预览图，用于启动器卡槽图标。
- ENTRY/INDEX/DATA 等段，用于 Lua 入口脚本和资源扩展。

格式细节见 [Docs/cart/XHGC_cart_bin_v2_格式规范.md](Docs/cart/XHGC_cart_bin_v2_格式规范.md)。

## Lua 脚本

Lua 脚本推荐使用生命周期函数：

```lua
function init(self)
end

function update(self, dt)
end

function final(self)
end
```

当前宿主环境暴露了 GPIO、PWM、delay、SD、声明式 UI children（button / slider）等 API。脚本示例在 [examples/lua](examples/lua)，完整 API 文档在 [Docs/lua/lua_api.md](Docs/lua/lua_api.md)。

## 目录结构

```text
Core/
  APPS/LVGL/        LVGL 9.5 源码、配置和移植层
  APPS/TASK/        FreeRTOS 任务封装：LVGL、Lua、LED
  Cart/             cart.bin / XHGC 卡带格式解析
  Driver/           LCD、SDRAM、触摸、Flash、EEPROM、GPIO、RNG 等驱动
  LuaPort/          Lua VM、宿主 API 和硬件绑定模块
  Screen/           启动器 UI、图标缓存、预览图工具
  Src/              CubeMX 生成代码、main.c、系统调用、Lua VM 封装

Docs/
  cart/             cart.bin 格式规范
  display/          DMA2D / 显示链路说明
  lua/              Lua 生命周期和 API 文档
  memory/           SDRAM 固定分区规范

cmake/              工具链、CubeMX 子工程和构建后脚本
examples/lua/       Lua 示例脚本
tests/              host 侧解析测试和 Lua smoke test
```

## 重要文档

- [Docs/memory/SDRAM_Layout_Spec_v1.0.md](Docs/memory/SDRAM_Layout_Spec_v1.0.md)：SDRAM 固定分区。
- [Docs/display/DMA2D_适配逻辑.md](Docs/display/DMA2D_适配逻辑.md)：DMA2D 与显示链路说明。
- [Docs/cart/XHGC_cart_bin_v2_格式规范.md](Docs/cart/XHGC_cart_bin_v2_格式规范.md)：卡带镜像格式。
- [Docs/lua/lua_runtime_contract.md](Docs/lua/lua_runtime_contract.md)：Lua 运行时约定。
- [Core/LuaPort/LuaPort_API.md](Core/LuaPort/LuaPort_API.md)：LuaPort C 侧 API。
- [Core/Driver/TOUCH/INTEGRATION_GUIDE.md](Core/Driver/TOUCH/INTEGRATION_GUIDE.md)：触摸驱动接入说明。

## 开发提示

- `Core/Driver/LCD/lcd.c` 是当前显示提交和 page flip 的主要所有者，改 LTDC/VBlank 时优先从这里追链路。
- `Core/APPS/LVGL/port/` 是 LVGL 与板级显示/输入之间的移植层。
- `Core/Screen/Page/ui_screen_launcher.c` 负责 launcher 页面、卡槽和 `cart.bin` 预览图接入。
- `Core/APPS/TASK/LUA.c` 负责 Lua 启停请求；默认卡带路径是 `0:/cart.bin`。
- `Core/Src/lua_vm.c` 负责 Lua VM、cart entry 加载和生命周期调度。
- `Docs/memory/SDRAM_Layout_Spec_v1.0.md` 与链接脚本/`sdram_layout.h` 应保持一致。

如果要临时开启板级 bring-up 测试，可以在配置时打开：

```sh
cmake --preset Debug -DCARTDESK_ENABLE_BOARD_TESTS=ON
```

如果只想构建 host 侧卡带解析测试，可以使用本机工具链单独配置 `CARTDESK_BUILD_HOST_TESTS=ON`。

## 项目状态

这是一个开发中的固件工程，README 描述的是当前代码库的真实结构和主要运行路径。硬件显示、触摸、SD 卡和 Lua 行为最终仍以实际板子验证为准。
