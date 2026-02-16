# LuaPort（STM32H743 裸机）接口文档

> 本文档基于你当前工程里的 `lua_vm.c/.h`、`lua_port.c/.h` 以及 `LuaPort/modules/*`（gpio/tim/delay/sd）整理。  
> 目标：在 **裸机主循环**里以 **非阻塞**方式周期调用 Lua 的 `start()` / `update(dt)`，并在 Lua 侧可直接控制硬件（GPIO/延时/微秒计时/SD FatFs）。

---

## 1. 快速上手（裸机主循环）

### 1.1 Lua 脚本约定（boot.lua）

运行时会尝试调用（若存在）：

```lua
function start()
  -- 初始化（只调用一次）
end

function update(dt)
  -- dt: 秒（float），默认固定 0.010
end
```

### 1.2 推荐的 C 侧初始化顺序

**关键点：一定要在执行脚本前把 `gpio/tim/sd/delay` 注册进 Lua。**  
否则会出现：`attempt to index a nil value (global 'gpio')`

推荐把 `lua_port_bind()` 放进 `lua_init()` 里（`luaL_loadbuffer()` 之前）。

伪代码（示意）：

```c
int main(void)
{
  HAL_Init();
  SystemClock_Config();
  MX_GPIO_Init();
  MX_SDMMC1_SD_Init();
  MX_FATFS_Init();
  // ...

  lua_init();      // 内部会加载 boot 脚本，并调用 start()

  while (1) {
    lua_update_task();  // 非阻塞：每 10ms 执行一次 update()
    // 你的其他轮询逻辑…
  }
}
```

---

## 2. Lua 运行时（lua_vm.h / lua_vm.c）

### 2.1 对外 API

#### `int lua_init(void);`

- **作用**：创建 Lua 虚拟机 `lua_State`，加载并执行 boot 脚本，随后调用一次 `start()`（如果脚本里定义了）。
- **返回值**：
  - `0`：成功
  - `<0`：失败（会通过 `lua_rt_log()` 打印原因）

> 目前实现中：脚本内容来自 `lua_get_boot_script()`（弱符号，可由你在工程里覆盖）。

#### `void lua_update_task(void);`

- **作用**：在主循环里反复调用。  
  内部根据 `lua_rt_time_ms()` 的时间差，**每到 10ms** 调用一次 `update(dt)`。
- **非阻塞策略**：未到周期直接 `return`，不会卡住主循环。
- **dt**：默认固定 `0.010f` 秒（由 `LUA_RT_PERIOD_MS` 控制）。

---

### 2.2 可覆盖的弱符号（weak hooks）

这三个函数在 `lua_vm.c` 中标记为 `__attribute__((weak))`，你可以在工程任意位置实现同名函数覆盖默认行为。

#### `uint32_t lua_rt_time_ms(void);`

- **默认实现**：返回 `HAL_GetTick()`（1ms tick）
- **用途**：为 `lua_update_task()` 提供时间基准。

#### `void lua_rt_log(const char *s);`

- **默认实现**：空实现（注释了 `printf`）
- **用途**：输出 Lua 加载/运行错误信息
- **建议**：替换为 RTT / 串口 / ITM 输出。

#### `const char* lua_get_boot_script(size_t *out_len);`

- **默认实现**：返回内置字符串（含 `start()` / `update(dt)`），并在 `update()` 里调用 `gpio.toggle('B', 1)`
- **用途**：提供 boot.lua 内容来源
- **你可以做**：
  - 返回编译进固件的脚本（const char[]）
  - 或返回从 QSPI/SD 读取的脚本（如果你愿意在 init 阶段阻塞加载）

---

### 2.3 周期配置宏

#### `LUA_RT_PERIOD_MS`

- **默认值**：`10`
- **作用**：控制 `lua_update_task()` 调用 `update()` 的周期。

---

### 2.4 常见错误码说明（`luaL_loadbuffer`）

`luaL_loadbuffer()` 的返回值 `rc`：
- `LUA_OK (0)`：成功
- `LUA_ERRSYNTAX (3)`：**语法错误**（最常见）
- `LUA_ERRMEM (4)`：内存不足
- `LUA_ERRGCMM (5)`：GC 元方法错误（较少见）

你之前遇到的 `rc == 3`，通常就是 boot.lua 字符串拼接时漏了 `\n` 或 `end` 位置不合法导致的语法错误。

---

## 3. 端口绑定（lua_port.h / lua_port.c）

### 3.1 C API

#### `void lua_port_bind(lua_State* L, const lua_port_config_t* cfg);`

- **作用**：把硬件相关的 Lua 模块注册到全局环境：
  - 全局表 `gpio`
  - 全局表 `tim`
  - 全局表 `sd`
  - 全局函数 `delay(ms)`
- **参数**：
  - `L`：Lua 虚拟机实例
  - `cfg`：端口配置（当前 `lua_port.c` 会把它塞进 registry 的 `"port.cfg"`，用于“单 LED 版 gpio 模块”读取端口/引脚；如果你用“全 GPIO 版模块”，可传 `NULL`。）

> 建议：如果你已经切到 **全 GPIO 版**（见下文 4.2），`lua_port_config_t` 可以保留但不必使用。

---

## 4. Lua 模块：gpio

你现在工程里存在 **两种 gpio 模块实现**：

- **A. 单 pin 版**（你根目录的 `lua_gpio.c`）：只控制 `cfg->gpio.led_port/led_pin`
- **B. 全 GPIO 版（推荐）**（`LuaPort/modules/lua_gpio.c`）：可控制 A~H（以及 I/J/K 若芯片有）所有端口的 0~15 引脚

### 4.1 gpio（全 GPIO 版，推荐）

#### `gpio.write(port, pin, value)`

- **参数**：
  - `port`：端口标识（字符串），支持 `"B"` / `"PB"` / `"GPIOB"` 等（内部会提取 A~K 的第一个字母）
  - `pin`：0~15
  - `value`：任意 Lua 真值（建议用 `1/0` 或 `true/false`）
- **行为**：使用 `BSRR` 原子写（置位/复位不会影响同端口其他 pin）

示例：

```lua
gpio.write("B", 1, 1)        -- PB1 = HIGH
gpio.write("GPIOB", 1, 0)    -- PB1 = LOW
```

#### `gpio.toggle(port, pin)`

```lua
gpio.toggle("B", 1)          -- PB1 翻转
```

#### `gpio.read(port, pin) -> boolean`

```lua
local v = gpio.read("B", 1)  -- true=高, false=低
```

#### 端口常量（可选）

模块会导出：

- `gpio.PORTA = "A"`
- `gpio.PORTB = "B"`
- ...
- `gpio.PORTH = "H"`
- （若存在）`gpio.PORTI/J/K`

因此你想写的 **不加引号** 风格，可以这样写：

```lua
gpio.write(gpio.PORTB, 1, 1)      -- ✅
-- gpio.write(B, 1, 1)            -- ❌ B 会被当成变量（通常是 nil）
```

#### HIGH / LOW 的写法

当前实现用 `lua_toboolean()`，所以推荐：

```lua
gpio.write(gpio.PORTB, 1, true)
gpio.write(gpio.PORTB, 1, false)
gpio.write(gpio.PORTB, 1, 1)      -- 1 -> true
gpio.write(gpio.PORTB, 1, 0)      -- 0 -> false
```

如果你想要 **字面量常量**（`gpio.HIGH` / `gpio.LOW`），建议在 `luaopen_gpio()` 末尾加：

```c
lua_pushinteger(L, 1); lua_setfield(L, -2, "HIGH");
lua_pushinteger(L, 0); lua_setfield(L, -2, "LOW");
```

然后 Lua 侧就可以写：

```lua
gpio.write(gpio.PORTB, 1, gpio.HIGH)
gpio.write(gpio.PORTB, 1, gpio.LOW)
```

---

### 4.2 gpio（单 pin 版：cfg->gpio.led_port/led_pin）

> 这一版只适合你最开始“只控 PB1 LED”的阶段；你现在想控全 GPIO，建议用 4.1。

#### `gpio.write(value)`

- value：真值 => SET；假值 => RESET

#### `gpio.toggle()`

#### `gpio.read() -> boolean`

---

## 5. Lua 全局函数：delay

来自 `LuaPort/modules/lua_delay.c`

### `delay(ms)`

- **参数**：毫秒（整数）
- **行为**：直接调用 `HAL_Delay(ms)`（这会阻塞当前线程/主循环！）

示例：

```lua
delay(100) -- 延时 100ms（阻塞）
```

> 提醒：你想“非阻塞”，主循环里尽量不要在 `update()` 里频繁使用 `delay()`；更推荐用 `tim.us()` 做时间差，自己写状态机。

---

## 6. Lua 模块：tim（微秒计时/延时）

来自 `LuaPort/modules/lua_tim.c`，基于 **DWT->CYCCNT**（不占用 TIM 外设）。

### `tim.us() -> integer`

- 返回：当前时间（微秒），从 DWT 启动后累计

```lua
local t = tim.us()
```

### `tim.delay_us(us)`

- **忙等** 微秒延时（阻塞）

```lua
tim.delay_us(50)
```

---

## 7. Lua 模块：sd（FatFs 文件读写）

来自 `LuaPort/modules/lua_sd.c`（需要你工程已接入 FatFs：`ff.h`、`f_mount/f_open/...`）。

### 7.1 初始化与挂载

模块内部会在第一次 `sd.open()` 时 `f_mount()` 自动挂载；也提供手动接口：

- `sd.mount() -> boolean`
- `sd.umount() -> boolean`

### 7.2 打开文件

#### `sd.open(path, mode?) -> file`

- `path`：例如 `"test.txt"` 或 `"/dir/a.txt"`  
  - 若未写盘符，会自动拼 `"0:/..."`（可通过宏 `LUA_SD_DRIVE` 修改）
- `mode`：
  - `"r"` / `"r+"`
  - `"w"` / `"w+"`
  - `"a"` / `"a+"`

示例：

```lua
local f = sd.open("log.txt", "a")
```

### 7.3 文件对象方法（`file:`）

- `file:close()`
- `file:write(data) -> written_len`
- `file:read(n) -> string`
- `file:seek(pos) -> new_pos`
- `file:size() -> size`

> 兼容拼写：`wirte`（故意保留了 typo alias）

示例：

```lua
local f = sd.open("hello.txt", "w")
f:write("hello\n")
f:close()
```

### 7.4 模块级包装函数（`sd.xxx(f, ...)`）

- `sd.close(f)` 等价 `f:close()`
- `sd.write(f, data)` 等价 `f:write(data)`
- `sd.read(f, n)` 等价 `f:read(n)`
- `sd.seek(f, pos)` 等价 `f:seek(pos)`
- `sd.size(f)` 等价 `f:size()`

示例：

```lua
local f = sd.open("a.bin", "r")
local s = sd.read(f, 16)
sd.close(f)
```

---

## 8. 如何在 C 里新增一个 Lua 库（模块/常量/函数）

### 8.1 C 函数签名规则

Lua 调用的 C 函数基本都长这样：

```c
static int myfunc(lua_State* L)
{
  // 从栈取参数：luaL_checkinteger / luaL_checkstring / lua_toboolean ...
  // 往栈压返回值：lua_pushinteger / lua_pushboolean / lua_pushlstring ...
  // return 返回值数量
  return 1;
}
```

- `L`：Lua 虚拟机“栈”的句柄（opaque pointer），**所有入参/出参都通过栈**传递。
- 返回值：你往栈里 push 了几个返回值，就 `return` 几。

### 8.2 导出成模块（`luaopen_xxx`）

```c
static const luaL_Reg lib[] = {
  {"foo", l_foo},
  {"bar", l_bar},
  {NULL, NULL}
};

int luaopen_my(lua_State* L)
{
  luaL_newlib(L, lib);
  // 可在这里 lua_setfield 导出常量
  return 1; // 返回这个 table
}
```

然后在 `lua_port_bind()` 里：

```c
luaopen_my(L);
lua_setglobal(L, "my");
```

---

## 9. 可选：做 VSCode + LVGL + Lua 的 PC 仿真平台（思路）

可以做：
- **Host 端（PC）**编译同一套 Lua + “LuaPort API”，但把 `gpio/tim/sd` 换成 PC stub（打印日志/写文件/模拟寄存器），
- LVGL 在 PC 端用 SDL 驱动（官方有示例），  
这样你在 VSCode 里写 Lua + UI 逻辑，就能在电脑上跑，最后再切回 STM32 的真实 HAL 实现。

---

## 10. 版本/文件索引

- `lua_vm.h / lua_vm.c`：裸机运行时（init + 10ms update）
- `lua_port.h / lua_port.c`：把模块注册进 Lua（gpio/tim/sd + delay）
- `LuaPort/modules/lua_gpio.c`：全 GPIO 版模块（推荐）
- `LuaPort/modules/lua_tim.c`：DWT 微秒计时与延时
- `LuaPort/modules/lua_delay.c`：全局 delay(ms)
- `LuaPort/modules/lua_sd.c`：FatFs 文件读写
