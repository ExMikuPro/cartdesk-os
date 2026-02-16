#include "lua_vm.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include "lua_port.h"

#ifndef LUA_RT_PERIOD_MS
#define LUA_RT_PERIOD_MS 10u
#endif

/* -------------------- 可覆盖的弱符号：脚本来源、日志、时间源 -------------------- */

__attribute__((weak))
uint32_t lua_rt_time_ms(void)
{
    /* STM32 HAL 的 1ms tick（SysTick 或你自己配置的时间基准） */
    extern uint32_t HAL_GetTick(void);
    return HAL_GetTick();
}

__attribute__((weak))
void lua_rt_log(const char *s)
{
    /* 你可以换成 SEGGER RTT / 串口打印 / ITM */
    (void)s;
    // printf("%s", s);
}

__attribute__((weak))
const char* lua_get_boot_script(size_t *out_len)
{
    static const char kScript[] =
    "-- 最小 LVGL 按钮测试脚本\n"
    "\n"
    "-- 按钮对象\n"
    "local btn\n"
    "\n"
    "-- 初始化函数\n"
    "function start()\n"
    "  \n"
    "  -- 创建按钮\n"
    "  btn = ui.button.create()\n"
    "  \n"
    "  -- 设置按钮文本\n"
    "  btn:set_text(\"Click Me\")\n"
    "  \n"
    "  -- 设置按钮大小\n"
    "  btn:set_size(120, 50)\n"
    "  \n"
    "  -- 设置按钮位置（屏幕中心）\n"
    "  btn:align(\"center\", 0, 0)\n"
    "  \n"
    "  -- 设置按钮样式\n"
    "  btn:set_style_bg_color(0x2196F3, 255)  -- 蓝色背景\n"
    "  btn:set_style_text_color(0xFFFFFF)     -- 白色文本\n"
    "  btn:set_style_border(0x1976D2, 2)      -- 深蓝色边框\n"
    "  btn:set_style_radius(8)                -- 圆角\n"
    "  \n"
    "end\n"
    "\n"
    "-- 更新函数\n"
    "function update(dt)\n"
    "  -- 每帧执行，这里留空\n"
    "end\n";

    if(out_len) *out_len = sizeof(kScript) - 1;
    return kScript;
}



/* -------------------- 内部状态 -------------------- */

static lua_State *g_L = NULL;
static bool       g_started = false;
static uint32_t   g_last_ms = 0;

/* -------------------- 错误/traceback 工具 -------------------- */

static int lua_rt_traceback(lua_State *L)
{
    const char *msg = lua_tostring(L, 1);
    if (msg) luaL_traceback(L, L, msg, 1);
    else lua_pushliteral(L, "(no error message)");
    return 1;
}

static int lua_rt_pcall(lua_State *L, int nargs, int nrets)
{
    /* 在函数下面插入 traceback handler */
    int base = lua_gettop(L) - nargs;
    lua_pushcfunction(L, lua_rt_traceback);
    lua_insert(L, base);

    int rc = lua_pcall(L, nargs, nrets, base);

    /* 移除 traceback handler */
    lua_remove(L, base);

    if (rc != LUA_OK) {
        const char *err = lua_tostring(L, -1);
        if (!err) err = "(lua error: null)";
        lua_rt_log(err);
        lua_rt_log("\n");
        lua_pop(L, 1);
        return -1;
    }
    return 0;
}

/* -------------------- 调用 start/update（若存在则调用） -------------------- */

static int lua_rt_call_start(lua_State *L)
{
    lua_getglobal(L, "start");
    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 1);
        return 0; /* 没有start也算成功 */
    }
    return lua_rt_pcall(L, 0, 0);
}

static int lua_rt_call_update(lua_State *L, float dt_sec)
{
    lua_getglobal(L, "update");
    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 1);
        return 0; /* 没有update也算成功 */
    }
    lua_pushnumber(L, (lua_Number)dt_sec);
    return lua_rt_pcall(L, 1, 0);
}

/* -------------------- 对外：lua_init / lua_update_task -------------------- */

extern int luaopen_gpio(lua_State *L);

int lua_init(void)
{
    if (g_L) return 0;

    g_L = luaL_newstate();
    if (!g_L) {
        lua_rt_log("luaL_newstate failed\n");
        return -1;
    }

    // 关键：使用 lua_port_bind 绑定所有模块，包括 lvgl_btn
    lua_port_bind(g_L, NULL);

    /* 你如果追求极简，可换成只开部分库 */
    // luaL_openlibs(g_L);

    /* 加载启动脚本 */
    size_t len = 0;
    const char *code = lua_get_boot_script(&len);
    if (!code || len == 0) {
        lua_rt_log("boot script is empty\n");
        return -2;
    }

    int rc = luaL_loadbuffer(g_L, code, len, "boot.lua");
    if (rc != LUA_OK) {
        const char *err = lua_tostring(g_L, -1);
        lua_rt_log("lua load failed: ");
        lua_rt_log(err ? err : "(null)");
        lua_rt_log("\n");
        lua_pop(g_L, 1);
        return -3;
    }

    if (lua_rt_pcall(g_L, 0, 0) != 0) {
        lua_rt_log("boot script run failed\n");
        return -4;
    }

    /* 记录时间基准 */
    g_last_ms = lua_rt_time_ms();

    /* 调一次 start()（如果存在） */
    if (lua_rt_call_start(g_L) != 0) {
        lua_rt_log("start() failed\n");
        return -5;
    }
    g_started = true;

    return 0;
}

#define LUA_RT_MAX_DT_MS 100u   // 最大 dt=100ms，按你需求可改 50/100/200

void lua_update_task(void)
{
    if (!g_L) return;

    const uint32_t now = lua_rt_time_ms();
    uint32_t elapsed = (uint32_t)(now - g_last_ms);

    if (elapsed < LUA_RT_PERIOD_MS) return;

    g_last_ms = now;

    if (!g_started) {
        (void)lua_rt_call_start(g_L);
        g_started = true;
    }

    if (elapsed > LUA_RT_MAX_DT_MS) elapsed = LUA_RT_MAX_DT_MS;

    float dt = (float)elapsed / 1000.0f;
    (void)lua_rt_call_update(g_L, dt);
}