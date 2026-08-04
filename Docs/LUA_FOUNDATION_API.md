# CartDesk Lua Foundation API

## 应用实例 self

宿主在 `init(self)` 前创建五个互不共享的普通 table：`state` 保存业务数据，
`ui` 保存 UI handle，`assets` 保存资源路径或 handle，`timers` 保存 timer handle，
`services` 保存异步服务状态。它们都不是宿主资源 registry；应用重启会得到全新 table。
固件与 host `luavm` 统一使用 64 位 `lua_Integer`；重新生成的 bytecode 必须与该 ABI
一致，旧的 32 位 integer bytecode 不兼容。

`init(self)` 在 protected call 中执行。若加载或任一生命周期回调抛错，宿主保存有上限
的错误阶段、摘要和 traceback，停止后续 update/input/timer/message 派发并按 owner 清理
UI、asset、timer、storage 与 Lua registry 引用。`LuaRuntimeTask` 随后在 app task 安全点
进入 `ERROR`，Launcher 显示“应用启动失败”、应用 ID、阶段、摘要和“返回 Launcher”；
不会在 Lua C binding 调用栈内销毁 runtime screen。

## 错误返回规范

查询成功返回值，执行成功返回 `true`，正常判断返回布尔值。参数、资源或系统错误
返回 `nil, "可读错误"`。核心模块由只读代理发布，不能替换其函数。

## ui

- `ui.root()`：当前应用内容根容器 handle。
- `ui.container(options)`、`ui.label(options)`、`ui.button(options)`、
  `ui.image(options)`：创建 full userdata handle；`parent` 缺省为应用 root。
- `ui.patch(handle, properties)`：唯一修改入口。
- `ui.delete(handle)`：删除当前 owner 对象；应用 root 不允许删除。

公共属性为 `rect={x,y,w,h}`、`hidden`、`enabled`、`selected`、`opacity=0..255`。
label/button 支持 `text`，image 支持 `src`；`src` 可为 Cart 逻辑路径或
`assets.image()` handle。button 的 `input` 产生 `action_id`，`id` 仅用于调试。
未知属性、类型错误、跨应用、已删除 handle 都返回错误。父对象收到
`LV_EVENT_DELETE` 时，全部子 handle 随 LVGL 递归删除立即失效。

```lua
local panel = assert(ui.container({ id="panel", rect={0,0,400,300} }))
local title = assert(ui.label({ parent=panel, text="Hello", rect={20,20,200,40} }))
assert(ui.patch(title, { text="Updated", opacity=220 }))
```

## assets

- `assets.exists(path)` 返回 `true/false`。
- `assets.image(path)` 返回 image asset full userdata。
- `assets.data(path)` 返回准确长度的 Lua binary string，单次上限 256 KiB。

路径只允许当前 Cart INDEX 内的相对逻辑路径；拒绝绝对路径、`..` 和越界 DATA。
图片继续复用 `resource_manager`/`RESOURCE_ARENA`，不暴露像素地址。

## storage

`storage.has/get/set/remove/clear` 修改当前应用的 pending KV；`storage.commit()` 才写入
QFlash littlefs。支持 boolean、`INT32_MIN..INT32_MAX` integer、number、string；key 最长 64 B，string
最长 4 KiB，最多 128 键，总 payload 16 KiB。文件位于宿主私有的
`/apps/<cart_id>/storage.bin`，Lua 无法看到路径。格式含 magic、version、entry count、
payload size 和 IEEE CRC-32；提交经临时文件、sync、close、rename 完成，损坏文件返回
错误，可用 `clear()` 后 `commit()` 恢复。整数及浮点 payload 使用明确的小端逐字节
编解码，不要求变长 key 后的 value 地址按 4/8 字节对齐，可在启用 `UNALIGN_TRP` 的
Cortex-M7 上安全读取。

Lua integer 超出 `-2147483648..2147483647` 时，`storage.set()` 返回
`nil, "integer value is outside int32 range"`，不会修改 pending store，也不会在后续
`commit()` 中写入截断值。当前磁盘格式仍为有符号 32 位小端 integer，不自动转 double。

storage 文件由 io task 异步预载；`init(self)` 只在该 owner 的 load completion 返回后
开始。`storage.commit()` 的 `true` 表示快照已成功加入 IO request queue，不表示 QFlash
已经写完；失败、取消和旧 owner completion 由宿主回收，不会从 worker 调用 Lua。

## timer

- `timer.now_ms()` 返回单调毫秒时间。
- `timer.after(delay_ms, callback)`、`timer.every(interval_ms, callback)` 返回 full userdata。
- `timer.cancel(handle)`、`timer.active(handle)`。

每应用最多 32 个，间隔 5 ms～24 h，每帧最多执行 8 个到期回调。回调只在 app
任务每帧安全点执行，不从 ISR 或 RTOS timer 回调进入 Lua。回调可取消自身或创建新
timer；抛错会记录 traceback 并停止该 timer。退出时宿主统一取消并释放 registry 引用。

## system

`screen_size()` 查询 LVGL 默认 display；`firmware_version()` 使用统一构建宏；
`uptime_ms()` 返回单调时间；`memory_info()` 返回 Lua、resource arena 与 FreeRTOS heap
的真实快照；`sd_status()` 读取 SD 初始化和 FatFs mount 状态；`usb_status()` 读取 USB
device state。`exit()`、`restart_app()` 只提交请求，当前 Lua 回调返回后由
`LuaRuntimeTask` 在安全点 final、清理并退出或重建 VM。

## random

`random.integer(min,max)` 含两端，可选结果数量必须为 `1..2^32`。普通跨度使用
rejection sampling；跨度恰为 `2^32` 时直接映射完整 `uint32_t`，不会执行 `% 0`。
跨度大于 `2^32` 返回 `nil, "random range exceeds 32-bit entropy"`。
`random.number()` 返回 `[0,1)`，`random.bytes(length)` 返回最多 4096 B 的 binary string。
三者仅调用统一 `RNG_GetU32/RNG_Fill` 驱动，不向 Lua 暴露 HAL handle；host 测试通过
仅在测试构建可见的 provider 注入固定序列与 RNG 失败。

## log

`log.debug/info/warn/error(...)` 经 `CartLog_Write` 输出
`[uptime][level][app_id] message`。每次最多 16 参数、消息 256 B、每应用每秒 32 条；
USART1 后端使用四槽异步发送队列，不在 Lua 调用栈内进行 100 ms 阻塞发送。队列满时
累计 dropped 数并在后续日志中报告。userdata 只输出 `<ui_handle>` 等安全摘要，不输出地址。

## crc

`crc.crc32(data)` 和 `crc.verify32(data, expected)` 使用项目 Cart 与打包器一致的
CRC-32/ISO-HDLC：polynomial `0x04C11DB7`（反射形式 `0xEDB88320`）、init
`0xFFFFFFFF`、RefIn/RefOut=true、XorOut=`0xFFFFFFFF`，字节序按输入顺序，空数据为
`0x00000000`。输入必须为 binary string，单次上限 1 MiB，不使用 `strlen()`。
固件 Lua 使用 64 位 integer；CRC 直接以非负 Lua integer 返回完整 32 位结果。

## 对象生命周期

UI、asset、timer handle 都记录 VM、owner、owner generation、对象 generation 和
alive。宿主 registry 独立于 `self.*` table。退出顺序为停止调度、调用 final、取消
timer、失效 asset、删除应用 UI root、释放回调、重置 resource arena、销毁 VM。
因此即使 Lua 清空 `self.ui/self.assets/self.timers`，资源仍会被回收。

生命周期回调可以运行在主 Lua VM 创建的 coroutine 上。owner 校验会先归一化到
registry 中的主线程，因此同一 VM 的 coroutine 可访问本应用资源，而其他 VM 仍会被拒绝。

## 资源和安全限制

Lua 不取得 HAL、FreeRTOS、FatFs、LVGL、QFlash、SDRAM 裸指针；不开放任意文件系统，
API 不创建任务，不在中断执行 Lua。`services` 首版仅创建空 table，尚无异步服务 API。

## 完整示例

见 `examples/lua/foundation_api_example.lua`。
