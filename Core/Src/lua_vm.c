#include "lua_vm.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include "ff.h"
#include "lua_port.h"
#include "xhgc_cart.h"

#ifndef LUA_RT_PERIOD_MS
#define LUA_RT_PERIOD_MS 10u
#endif

#ifndef LUA_RT_BOOT_BYTECODE_PATH
#define LUA_RT_BOOT_BYTECODE_PATH "0:/boot.luac"
#endif

#ifndef LUA_RT_BOOT_CART_PATH
#define LUA_RT_BOOT_CART_PATH "0:/cart.bin"
#endif

#ifndef LUA_RT_FILE_CHUNK_SIZE
#define LUA_RT_FILE_CHUNK_SIZE 512u
#endif

#ifndef LUA_RT_SD_DRIVE
#define LUA_RT_SD_DRIVE "0:"
#endif

#ifndef LUA_RT_SD_MOUNT_PATH
#define LUA_RT_SD_MOUNT_PATH LUA_RT_SD_DRIVE
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
    if (s == NULL) {
        return;
    }
    fputs(s, stdout);
    fflush(stdout);
}

__attribute__((weak))
const char* lua_get_boot_script(size_t *out_len)
{
    static const char kScript[] =
    "-- LVGL 完整测试脚本\n"
    "\n"
    "-- UI 对象\n"
    "local btn\n"
    "local slider\n"
    "local btn_clicked = false\n"
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
    "  btn:set_size(150, 60)\n"
    "  \n"
    "  -- 设置按钮位置（屏幕中心上方）\n"
    "  btn:align(\"center\", 0, -50)\n"
    "  \n"
    "  -- 设置按钮样式\n"
    "  btn:set_style_bg_color(0x2196F3, 255)  -- 蓝色背景\n"
    "  btn:set_style_text_color(0xFFFFFF)     -- 白色文本\n"
    "  btn:set_style_border(0x1976D2, 2)      -- 深蓝色边框\n"
    "  btn:set_style_radius(8)                -- 圆角\n"
    "  \n"
    "  -- 设置按钮回调\n"
    "  btn:set_callback(function(obj, event)\n"
    "    if event == \"clicked\" then\n"
    "      btn_clicked = not btn_clicked\n"
    "      if btn_clicked then\n"
    "        obj:set_text(\"Clicked!\")\n"
    "        obj:set_style_bg_color(0x4CAF50, 255)  -- 绿色背景\n"
    "      else\n"
    "        obj:set_text(\"Click Me\")\n"
    "        obj:set_style_bg_color(0x2196F3, 255)  -- 蓝色背景\n"
    "      end\n"
    "      print(\"Button event:\", event, \"Clicked state:\", btn_clicked)\n"
    "    end\n"
    "  end)\n"
    "  \n"
    "  -- 创建滑块\n"
    "  slider = ui.slider.create()\n"
    "  \n"
    "  -- 设置滑块大小\n"
    "  slider:set_size(200, 20)\n"
    "  \n"
    "  -- 设置滑块位置（屏幕中心）\n"
    "  slider:align(\"center\", 0, 50)\n"
    "  \n"
    "  -- 设置滑块范围\n"
    "  slider:set_range(0, 100)\n"
    "  \n"
    "  -- 设置初始值\n"
    "  slider:set_value(50, true)\n"
    "  \n"
    "  -- 设置滑块样式\n"
    "  slider:set_style_bg_color(0xEEEEEE, 255)       -- 灰色背景\n"
    "  slider:set_style_indicator_color(0xFFC107, 255)  -- 黄色指示器\n"
    "  slider:set_style_knob_color(0xFF9800, 255)      -- 橙色旋钮\n"
    "  slider:set_style_border(0xBDBDBD, 1)           -- 浅灰色边框\n"
    "  slider:set_style_radius(10)                    -- 圆角\n"
    "  \n"
    "  -- 设置滑块回调\n"
    "  slider:set_callback(function(obj, event)\n"
    "    if event == \"value_changed\" then\n"
    "      local value = obj:get_value()\n"
    "    end\n"
    "  end)\n"
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

__attribute__((weak))
const char* lua_get_boot_bytecode_path(void)
{
    return LUA_RT_BOOT_BYTECODE_PATH;
}

__attribute__((weak))
const char* lua_get_boot_cart_path(void)
{
    return LUA_RT_BOOT_CART_PATH;
}


/* -------------------- 内部状态 -------------------- */

static lua_State *g_L = NULL;
static bool       g_started = false;
static uint32_t   g_last_ms = 0;
static FATFS      g_lua_file_fs;
static bool       g_lua_file_fs_ready = false;

typedef enum {
    LUA_RT_ENTRY_NONE = 0,
    LUA_RT_ENTRY_START,
    LUA_RT_ENTRY_UPDATE,
} lua_rt_entry_t;

static lua_State     *g_entry_thread = NULL;
static int            g_entry_ref = LUA_NOREF;
static lua_rt_entry_t g_entry_kind = LUA_RT_ENTRY_NONE;
static bool           g_entry_sleeping = false;
static uint32_t       g_entry_wake_ms = 0;

static void lua_rt_openlibs(lua_State *L)
{
    luaL_requiref(L, LUA_GNAME, luaopen_base, 1);
    lua_pop(L, 1);
    luaL_requiref(L, LUA_COLIBNAME, luaopen_coroutine, 1);
    lua_pop(L, 1);
    luaL_requiref(L, LUA_TABLIBNAME, luaopen_table, 1);
    lua_pop(L, 1);
    luaL_requiref(L, LUA_STRLIBNAME, luaopen_string, 1);
    lua_pop(L, 1);
    luaL_requiref(L, LUA_MATHLIBNAME, luaopen_math, 1);
    lua_pop(L, 1);
    luaL_requiref(L, LUA_UTF8LIBNAME, luaopen_utf8, 1);
    lua_pop(L, 1);
}

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

/* -------------------- 字节码加载/执行 -------------------- */

typedef struct {
    FIL file;
    char buf[LUA_RT_FILE_CHUNK_SIZE];
    FRESULT read_result;
} lua_rt_file_reader_t;

typedef enum {
    LUA_RT_CART_SOURCE_NONE = 0,
    LUA_RT_CART_SOURCE_SLOT,
    LUA_RT_CART_SOURCE_FILE,
} lua_rt_cart_source_t;

typedef struct {
    XHGC_CartFatFs cart_file;
    XHGC_CartFile file;
    XHGC_CartSlot slot;
    lua_rt_cart_source_t source;
    uint32_t cursor;
    uint32_t total_size;
    int read_result;
    char buf[LUA_RT_FILE_CHUNK_SIZE];
} lua_rt_cart_reader_t;

static const char *lua_rt_fr2str(FRESULT fr)
{
    switch (fr) {
    case FR_OK:                  return "FR_OK";
    case FR_DISK_ERR:            return "FR_DISK_ERR";
    case FR_INT_ERR:             return "FR_INT_ERR";
    case FR_NOT_READY:           return "FR_NOT_READY";
    case FR_NO_FILE:             return "FR_NO_FILE";
    case FR_NO_PATH:             return "FR_NO_PATH";
    case FR_INVALID_NAME:        return "FR_INVALID_NAME";
    case FR_DENIED:              return "FR_DENIED";
    case FR_EXIST:               return "FR_EXIST";
    case FR_INVALID_OBJECT:      return "FR_INVALID_OBJECT";
    case FR_WRITE_PROTECTED:     return "FR_WRITE_PROTECTED";
    case FR_INVALID_DRIVE:       return "FR_INVALID_DRIVE";
    case FR_NOT_ENABLED:         return "FR_NOT_ENABLED";
    case FR_NO_FILESYSTEM:       return "FR_NO_FILESYSTEM";
    case FR_MKFS_ABORTED:        return "FR_MKFS_ABORTED";
    case FR_TIMEOUT:             return "FR_TIMEOUT";
    case FR_LOCKED:              return "FR_LOCKED";
    case FR_NOT_ENOUGH_CORE:     return "FR_NOT_ENOUGH_CORE";
    case FR_TOO_MANY_OPEN_FILES: return "FR_TOO_MANY_OPEN_FILES";
    case FR_INVALID_PARAMETER:   return "FR_INVALID_PARAMETER";
    default:                     return "FR_?";
    }
}

static const char *lua_rt_cart_rc2str(int rc)
{
    switch (rc) {
    case XHGC_CART_OK:          return "XHGC_CART_OK";
    case XHGC_CART_E_PARAM:     return "XHGC_CART_E_PARAM";
    case XHGC_CART_E_IO:        return "XHGC_CART_E_IO";
    case XHGC_CART_E_RANGE:     return "XHGC_CART_E_RANGE";
    case XHGC_CART_E_MAGIC:     return "XHGC_CART_E_MAGIC";
    case XHGC_CART_E_VERSION:   return "XHGC_CART_E_VERSION";
    case XHGC_CART_E_HEADER:    return "XHGC_CART_E_HEADER";
    case XHGC_CART_E_CRC:       return "XHGC_CART_E_CRC";
    case XHGC_CART_E_NOT_FOUND: return "XHGC_CART_E_NOT_FOUND";
    case XHGC_CART_E_FORMAT:    return "XHGC_CART_E_FORMAT";
    default:                    return "XHGC_CART_E_?";
    }
}

static void lua_rt_make_sd_path(char *out, size_t out_size, const char *path)
{
    if (!out || out_size == 0) return;
    if (!path) {
        out[0] = '\0';
        return;
    }

    if (strchr(path, ':')) {
        snprintf(out, out_size, "%s", path);
    } else if (path[0] == '/') {
        snprintf(out, out_size, "%s%s", LUA_RT_SD_DRIVE, path);
    } else {
        snprintf(out, out_size, "%s/%s", LUA_RT_SD_DRIVE, path);
    }
}

static FRESULT lua_rt_mount_sd(void)
{
    if (g_lua_file_fs_ready) return FR_OK;

    FRESULT fr = f_mount(&g_lua_file_fs, LUA_RT_SD_MOUNT_PATH, 1);
    g_lua_file_fs_ready = (fr == FR_OK);
    return fr;
}

static const char *lua_rt_file_reader(lua_State *L, void *ud, size_t *size)
{
    (void)L;

    lua_rt_file_reader_t *reader = (lua_rt_file_reader_t *)ud;
    UINT br = 0;
    FRESULT fr = f_read(&reader->file, reader->buf, sizeof(reader->buf), &br);
    if (fr != FR_OK) {
        reader->read_result = fr;
        *size = 0;
        return NULL;
    }

    *size = (size_t)br;
    return (br > 0) ? reader->buf : NULL;
}

static int lua_rt_load_file(lua_State *L, const char *path)
{
    if (!path || path[0] == '\0') return -1;

    char fatfs_path[256];
    char chunk_name[260];
    lua_rt_make_sd_path(fatfs_path, sizeof(fatfs_path), path);
    snprintf(chunk_name, sizeof(chunk_name), "@%s", fatfs_path);

    lua_rt_file_reader_t reader;
    memset(&reader, 0, sizeof(reader));
    reader.read_result = FR_OK;

    FRESULT fr = f_open(&reader.file, fatfs_path, FA_READ | FA_OPEN_EXISTING);
    if (fr == FR_NOT_ENABLED || fr == FR_INVALID_DRIVE) {
        fr = lua_rt_mount_sd();
        if (fr == FR_OK) {
            fr = f_open(&reader.file, fatfs_path, FA_READ | FA_OPEN_EXISTING);
        }
    }

    if (fr != FR_OK) {
        lua_rt_log("lua file open failed: ");
        lua_rt_log(fatfs_path);
        lua_rt_log(" (");
        lua_rt_log(lua_rt_fr2str(fr));
        lua_rt_log(")\n");
        return -2;
    }

    int rc = lua_load(L, lua_rt_file_reader, &reader, chunk_name, "b");
    FRESULT close_fr = f_close(&reader.file);

    if (reader.read_result != FR_OK) {
        lua_rt_log("lua file read failed: ");
        lua_rt_log(fatfs_path);
        lua_rt_log(" (");
        lua_rt_log(lua_rt_fr2str(reader.read_result));
        lua_rt_log(")\n");
        lua_pop(L, 1);
        return -3;
    }

    if (close_fr != FR_OK) {
        lua_rt_log("lua file close failed: ");
        lua_rt_log(lua_rt_fr2str(close_fr));
        lua_rt_log("\n");
        lua_pop(L, 1);
        return -4;
    }

    if (rc != LUA_OK) {
        const char *err = lua_tostring(L, -1);
        lua_rt_log("lua bytecode load failed: ");
        lua_rt_log(err ? err : "(null)");
        lua_rt_log("\n");
        lua_pop(L, 1);
        return -5;
    }

    return 0;
}

int lua_run_bytecode(const void *bytecode, uint32_t len, const char *chunk_name)
{
    if (!g_L || !bytecode || len == 0) return -1;
    if (g_entry_thread) return -2;

    int rc = luaL_loadbufferx(g_L,
                              (const char *)bytecode,
                              (size_t)len,
                              chunk_name ? chunk_name : "=(bytecode)",
                              "b");
    if (rc != LUA_OK) {
        const char *err = lua_tostring(g_L, -1);
        lua_rt_log("lua bytecode load failed: ");
        lua_rt_log(err ? err : "(null)");
        lua_rt_log("\n");
        lua_pop(g_L, 1);
        return -3;
    }

    return (lua_rt_pcall(g_L, 0, 0) == 0) ? 0 : -4;
}

int lua_run_file(const char *path)
{
    if (!g_L) return -1;
    if (g_entry_thread) return -2;

    int rc = lua_rt_load_file(g_L, path);
    if (rc != 0) return rc;

    return (lua_rt_pcall(g_L, 0, 0) == 0) ? 0 : -6;
}

static int lua_rt_open_cart(lua_rt_cart_reader_t *reader,
                            const char *path,
                            char *fatfs_path,
                            size_t fatfs_path_size)
{
    if (!reader || !path || !fatfs_path || fatfs_path_size == 0u) return -1;

    lua_rt_make_sd_path(fatfs_path, fatfs_path_size, path);
    int rc = xhgc_cart_open_fatfs(&reader->cart_file, fatfs_path);
    if (rc == XHGC_CART_E_IO) {
        FRESULT fr = lua_rt_mount_sd();
        if (fr != FR_OK) {
            lua_rt_log("cart mount failed: ");
            lua_rt_log(lua_rt_fr2str(fr));
            lua_rt_log("\n");
            return -2;
        }
        rc = xhgc_cart_open_fatfs(&reader->cart_file, fatfs_path);
    }

    if (rc != XHGC_CART_OK) {
        lua_rt_log("cart open failed: ");
        lua_rt_log(fatfs_path);
        lua_rt_log(" (");
        lua_rt_log(lua_rt_cart_rc2str(rc));
        lua_rt_log(")\n");
        return -3;
    }

    return 0;
}

static int lua_rt_select_cart_entry(lua_rt_cart_reader_t *reader,
                                    const char *fatfs_path,
                                    char *chunk_name,
                                    size_t chunk_name_size)
{
    XHGC_Cart *cart = &reader->cart_file.cart;
    int rc = xhgc_cart_get_slot(cart, XHGC_CART_SLOT_ENTRY, &reader->slot);
    if (rc == XHGC_CART_OK) {
        reader->source = LUA_RT_CART_SOURCE_SLOT;
        reader->total_size = reader->slot.size;
        snprintf(chunk_name, chunk_name_size, "@%s#ENTRY", fatfs_path);
        return 0;
    }
    if (rc != XHGC_CART_E_NOT_FOUND) return -1;

    char entry[XHGC_CART_ENTRY_SIZE + 1u];
    snprintf(entry, sizeof(entry), "%s", cart->header.entry);
    if (entry[0] == '\0') {
        rc = xhgc_cart_manf_get_string(cart, 0x06u, entry, sizeof(entry));
        if (rc != XHGC_CART_OK) return -2;
    }

    rc = xhgc_cart_find_file(cart, entry, &reader->file);
    if (rc != XHGC_CART_OK) {
        lua_rt_log("cart entry not found: ");
        lua_rt_log(entry);
        lua_rt_log(" (");
        lua_rt_log(lua_rt_cart_rc2str(rc));
        lua_rt_log(")\n");
        return -3;
    }

    reader->source = LUA_RT_CART_SOURCE_FILE;
    reader->total_size = reader->file.data_size;
    snprintf(chunk_name, chunk_name_size, "@%s:%s", fatfs_path, entry);
    return 0;
}

static const char *lua_rt_cart_reader(lua_State *L, void *ud, size_t *size)
{
    (void)L;

    lua_rt_cart_reader_t *reader = (lua_rt_cart_reader_t *)ud;
    uint32_t remain = reader->total_size - reader->cursor;
    uint32_t want = (uint32_t)sizeof(reader->buf);
    int rc = XHGC_CART_OK;

    if (remain == 0u) {
        *size = 0;
        return NULL;
    }
    if (want > remain) want = remain;

    if (reader->source == LUA_RT_CART_SOURCE_FILE) {
        rc = xhgc_cart_read_file(&reader->cart_file.cart,
                                 &reader->file,
                                 reader->cursor,
                                 reader->buf,
                                 want);
    } else if (reader->source == LUA_RT_CART_SOURCE_SLOT) {
        uint64_t offset = reader->slot.offset + reader->cursor;
        rc = reader->cart_file.cart.read(reader->cart_file.cart.reader_ctx,
                                         offset,
                                         reader->buf,
                                         want) == 0 ? XHGC_CART_OK : XHGC_CART_E_IO;
    } else {
        rc = XHGC_CART_E_PARAM;
    }

    if (rc != XHGC_CART_OK) {
        reader->read_result = rc;
        *size = 0;
        return NULL;
    }

    reader->cursor += want;
    *size = (size_t)want;
    return reader->buf;
}

static int lua_rt_load_cart_entry(lua_State *L, const char *cart_path)
{
    if (!cart_path || cart_path[0] == '\0') return -1;

    char fatfs_path[256];
    char chunk_name[320];
    lua_rt_cart_reader_t reader;
    memset(&reader, 0, sizeof(reader));
    reader.read_result = XHGC_CART_OK;

    int open_rc = lua_rt_open_cart(&reader, cart_path, fatfs_path, sizeof(fatfs_path));
    if (open_rc != 0) return open_rc;

    int select_rc = lua_rt_select_cart_entry(&reader, fatfs_path, chunk_name, sizeof(chunk_name));
    if (select_rc != 0) {
        xhgc_cart_close_fatfs(&reader.cart_file);
        return -4;
    }

    int load_rc = lua_load(L, lua_rt_cart_reader, &reader, chunk_name, "b");
    xhgc_cart_close_fatfs(&reader.cart_file);

    if (reader.read_result != XHGC_CART_OK) {
        lua_rt_log("cart entry read failed: ");
        lua_rt_log(lua_rt_cart_rc2str(reader.read_result));
        lua_rt_log("\n");
        lua_pop(L, 1);
        return -5;
    }

    if (load_rc != LUA_OK) {
        const char *err = lua_tostring(L, -1);
        lua_rt_log("cart bytecode load failed: ");
        lua_rt_log(err ? err : "(null)");
        lua_rt_log("\n");
        lua_pop(L, 1);
        return -6;
    }

    return 0;
}

int lua_run_cart_entry(const char *cart_path)
{
    if (!g_L) return -1;
    if (g_entry_thread) return -2;

    int rc = lua_rt_load_cart_entry(g_L, cart_path);
    if (rc != 0) return rc;

    return (lua_rt_pcall(g_L, 0, 0) == 0) ? 0 : -7;
}

/* -------------------- start/update 协程调度 -------------------- */

static bool lua_rt_time_reached(uint32_t now, uint32_t target)
{
    return (int32_t)(now - target) >= 0;
}

void lua_rt_delay_ms(uint32_t delay_ms)
{
    g_entry_wake_ms = lua_rt_time_ms() + delay_ms;
    g_entry_sleeping = true;
}

static void lua_rt_clear_entry(void)
{
    if (g_entry_thread) {
        lua_settop(g_entry_thread, 0);
    }

    if (g_entry_ref != LUA_NOREF) {
        luaL_unref(g_L, LUA_REGISTRYINDEX, g_entry_ref);
    }

    g_entry_thread = NULL;
    g_entry_ref = LUA_NOREF;
    g_entry_kind = LUA_RT_ENTRY_NONE;
    g_entry_sleeping = false;
    g_entry_wake_ms = 0;
}

static int lua_rt_resume_entry(int nargs)
{
    int nresults = 0;
    g_entry_sleeping = false;

    int rc = lua_resume(g_entry_thread, g_L, nargs, &nresults);
    if (rc == LUA_YIELD) {
        if (nresults > 0) {
            lua_pop(g_entry_thread, nresults);
        }
        return 1;
    }

    if (rc == LUA_OK) {
        if (nresults > 0) {
            lua_pop(g_entry_thread, nresults);
        }
        if (g_entry_kind == LUA_RT_ENTRY_START) {
            g_started = true;
        }
        lua_rt_clear_entry();
        return 0;
    }

    const char *err = lua_tostring(g_entry_thread, -1);
    lua_rt_log(err ? err : "(lua coroutine error)");
    lua_rt_log("\n");
    if (g_entry_kind == LUA_RT_ENTRY_START) {
        g_started = true;
    }
    lua_rt_clear_entry();
    return -1;
}

static int lua_rt_poll_entry(uint32_t now)
{
    if (!g_entry_thread) return 0;
    if (g_entry_sleeping && !lua_rt_time_reached(now, g_entry_wake_ms)) {
        return 1;
    }
    return lua_rt_resume_entry(0);
}

static int lua_rt_begin_global_call(const char *name, lua_rt_entry_t kind, int nargs)
{
    if (g_entry_thread) return 1;

    lua_getglobal(g_L, name);
    if (!lua_isfunction(g_L, -1)) {
        lua_pop(g_L, nargs + 1);
        if (kind == LUA_RT_ENTRY_START) {
            g_started = true;
        }
        return 0;
    }

    if (nargs > 0) {
        lua_insert(g_L, -nargs - 1);
    }

    g_entry_thread = lua_newthread(g_L);
    if (!g_entry_thread) {
        lua_pop(g_L, nargs + 1);
        return -1;
    }
    g_entry_ref = luaL_ref(g_L, LUA_REGISTRYINDEX);
    g_entry_kind = kind;
    g_entry_sleeping = false;

    lua_xmove(g_L, g_entry_thread, nargs + 1);
    return lua_rt_resume_entry(nargs);
}

/* -------------------- 对外：lua_init / lua_update_task -------------------- */

extern int luaopen_gpio(lua_State *L);

static int lua_rt_init_state(void)
{
    if (g_L) return 0;

    g_L = luaL_newstate();
    if (!g_L) {
        lua_rt_log("luaL_newstate failed\n");
        return -1;
    }

    lua_rt_openlibs(g_L);

    lua_port_bind(g_L, NULL);

    return 0;
}

static int lua_rt_run_embedded_boot(void)
{
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

    return 0;
}

static int lua_rt_start_runtime(void)
{
    g_last_ms = lua_rt_time_ms();

    /* 调一次 start()（如果存在）。start/update 运行在可 yield 的协程里。 */
    if (lua_rt_begin_global_call("start", LUA_RT_ENTRY_START, 0) < 0) {
        lua_rt_log("start() failed\n");
        return -5;
    }

    return 0;
}

int lua_init_from_file(const char *path)
{
    int rc = lua_rt_init_state();
    if (rc != 0) return rc;

    rc = lua_run_file(path);
    if (rc != 0) return rc;

    return lua_rt_start_runtime();
}

int lua_init_from_cart(const char *cart_path)
{
    int rc = lua_rt_init_state();
    if (rc != 0) return rc;

    rc = lua_run_cart_entry(cart_path);
    if (rc != 0) return rc;

    return lua_rt_start_runtime();
}

int lua_init(void)
{
    int rc = lua_rt_init_state();
    if (rc != 0) return rc;

    const char *cart_path = lua_get_boot_cart_path();
    if (cart_path && cart_path[0] != '\0') {
        rc = lua_run_cart_entry(cart_path);
        if (rc == 0) {
            return lua_rt_start_runtime();
        }
    }

    const char *boot_path = lua_get_boot_bytecode_path();
    if (boot_path && boot_path[0] != '\0') {
        rc = lua_run_file(boot_path);
        if (rc == 0) {
            return lua_rt_start_runtime();
        }
    }

    rc = lua_rt_run_embedded_boot();
    if (rc != 0) return rc;

    return lua_rt_start_runtime();
}

#define LUA_RT_MAX_DT_MS 100u   // 最大 dt=100ms，按你需求可改 50/100/200

void lua_update_task(void)
{
    if (!g_L) return;

    const uint32_t now = lua_rt_time_ms();

    if (g_entry_thread) {
        (void)lua_rt_poll_entry(now);
        return;
    }

    uint32_t elapsed = (uint32_t)(now - g_last_ms);

    if (elapsed < LUA_RT_PERIOD_MS) return;

    g_last_ms = now;

    if (!g_started) {
        (void)lua_rt_begin_global_call("start", LUA_RT_ENTRY_START, 0);
        return;
    }

    if (elapsed > LUA_RT_MAX_DT_MS) elapsed = LUA_RT_MAX_DT_MS;

    float dt = (float)elapsed / 1000.0f;
    lua_pushnumber(g_L, (lua_Number)dt);
    (void)lua_rt_begin_global_call("update", LUA_RT_ENTRY_UPDATE, 1);
}
