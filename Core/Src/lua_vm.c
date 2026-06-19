#include "lua_vm.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include "ff.h"
#include "fatfs.h"
#include "lua_port.h"
#include "lua_ui.h"
#include "lua_vm_memory.h"
#include "resource_manager.h"
#include "xhgc_cart.h"

#ifndef LUA_RT_PERIOD_MS
#define LUA_RT_PERIOD_MS 10u
#endif

#ifndef LUA_RT_BOOT_BYTECODE_PATH
#define LUA_RT_BOOT_BYTECODE_PATH ""
#endif

#ifndef LUA_RT_BOOT_CART_PATH
#define LUA_RT_BOOT_CART_PATH ""
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

#ifndef LUA_RT_MAX_INSTANCES
#define LUA_RT_MAX_INSTANCES 4u
#endif

#ifndef LUA_RT_FIXED_DT
#define LUA_RT_FIXED_DT (1.0f / 60.0f)
#endif

#ifndef LUA_RT_MAX_FIXED_STEPS
#define LUA_RT_MAX_FIXED_STEPS 5u
#endif

#ifndef LUA_RT_INPUT_QUEUE_CAPACITY
#define LUA_RT_INPUT_QUEUE_CAPACITY 16u
#endif

#ifndef LUA_RT_MESSAGE_QUEUE_CAPACITY
#define LUA_RT_MESSAGE_QUEUE_CAPACITY 16u
#endif

#ifndef LUA_RT_SOURCE_PATH_MAX
#define LUA_RT_SOURCE_PATH_MAX 192u
#endif

/* -------------------- 可覆盖的弱符号：脚本来源、日志、时间源 -------------------- */

/**
 * @brief  获取 Lua runtime 使用的毫秒时间戳
 * @retval 当前毫秒 tick
 * @note   默认弱实现返回 HAL_GetTick，平台可覆盖该符号
 */
__attribute__((weak))
uint32_t lua_rt_time_ms(void)
{
    /* STM32 HAL 的 1ms tick（SysTick 或你自己配置的时间基准） */
    extern uint32_t HAL_GetTick(void);
    return HAL_GetTick();
}

/**
 * @brief  输出 Lua runtime 日志
 * @param  s: 待输出字符串，NULL时直接返回
 * @retval None
 * @note   默认弱实现写入 stdout 并 flush，平台可覆盖该符号
 */
__attribute__((weak))
void lua_rt_log(const char *s)
{
    if (s == NULL) {
        return;
    }
    fputs(s, stdout);
    fflush(stdout);
}

/**
 * @brief  获取默认内嵌启动脚本
 * @param  out_len: 输出脚本长度，可为NULL
 * @return 非NULL=静态 Lua 源码字符串
 * @note   默认弱实现提供内嵌 boot.lua，平台可覆盖该符号
 */
__attribute__((weak))
const char* lua_get_boot_script(size_t *out_len)
{
    static const char kScript[] =
    "-- PA0 input controls the onboard LED on PB1\n"
    "local BUTTON_PIN = 5\n"
    "local LED_PIN = 3\n"
    "\n"
    "function init(self)\n"
    "  self.pressed = false\n"
    "  gpio.pinMode(BUTTON_PIN, gpio.INPUT_PULLUP)\n"
    "  gpio.pinMode(LED_PIN, gpio.OUTPUT)\n"
    "  gpio.digitalWrite(LED_PIN, gpio.LOW)\n"
    "end\n"
    "\n"
    "function update(self, dt)\n"
    "  self.pressed = gpio.digitalRead(BUTTON_PIN) == gpio.LOW\n"
    "  gpio.digitalWrite(LED_PIN, self.pressed and gpio.HIGH or gpio.LOW)\n"
    "end\n"
    "\n"
    "function final(self)\n"
    "  gpio.digitalWrite(LED_PIN, gpio.LOW)\n"
    "end\n";

    if(out_len) *out_len = sizeof(kScript) - 1;
    return kScript;
}

/**
 * @brief  获取默认启动字节码文件路径
 * @return 非NULL=启动字节码路径字符串，可能为空字符串
 * @note   默认弱实现返回 LUA_RT_BOOT_BYTECODE_PATH，平台可覆盖该符号
 */
__attribute__((weak))
const char* lua_get_boot_bytecode_path(void)
{
    return LUA_RT_BOOT_BYTECODE_PATH;
}

/**
 * @brief  获取默认启动 cart 文件路径
 * @return 非NULL=启动 cart 路径字符串，可能为空字符串
 * @note   默认弱实现返回 LUA_RT_BOOT_CART_PATH，平台可覆盖该符号
 */
__attribute__((weak))
const char* lua_get_boot_cart_path(void)
{
    return LUA_RT_BOOT_CART_PATH;
}


/* -------------------- 内部状态 -------------------- */

static lua_State *g_L = NULL;
static bool       g_runtime_started = false;
static uint32_t   g_last_ms = 0;
static float      g_fixed_accumulator = 0.0f;

typedef enum {
    LUA_LIFECYCLE_INIT = 0,
    LUA_LIFECYCLE_FINAL,
    LUA_LIFECYCLE_FIXED_UPDATE,
    LUA_LIFECYCLE_UPDATE,
    LUA_LIFECYCLE_LATE_UPDATE,
    LUA_LIFECYCLE_MESSAGE,
    LUA_LIFECYCLE_INPUT,
    LUA_LIFECYCLE_RELOAD,
    LUA_LIFECYCLE_COUNT
} lua_lifecycle_t;

typedef enum {
    LUA_SCRIPT_SOURCE_NONE = 0,
    LUA_SCRIPT_SOURCE_EMBEDDED,
    LUA_SCRIPT_SOURCE_FILE,
    LUA_SCRIPT_SOURCE_CART,
} lua_script_source_t;

typedef struct {
    bool alive;
    bool initialized;
    bool finalized;
    bool legacy_callbacks;
    int env_ref;
    int self_ref;
    lua_State *thread;
    int thread_ref;
    int callback_refs[LUA_LIFECYCLE_COUNT];
    lua_script_source_t source;
    char source_path[LUA_RT_SOURCE_PATH_MAX];
} lua_script_instance_t;

typedef struct {
    char action_id[LUA_INPUT_ACTION_ID_MAX];
    LuaInputAction action;
} lua_input_event_t;

typedef struct {
    char message_id[LUA_MESSAGE_ID_MAX];
    char sender[LUA_MESSAGE_SENDER_MAX];
} lua_message_event_t;

typedef enum {
    LUA_SCHED_IDLE = 0,
    LUA_SCHED_INIT,
    LUA_SCHED_INPUT,
    LUA_SCHED_FIXED_UPDATE,
    LUA_SCHED_UPDATE,
    LUA_SCHED_LATE_UPDATE,
    LUA_SCHED_MESSAGE,
} lua_scheduler_phase_t;

__attribute__((section(".ram_runtime"), aligned(32)))
static lua_script_instance_t g_instances[LUA_RT_MAX_INSTANCES];
static size_t g_instance_count = 0;

__attribute__((section(".ram_runtime"), aligned(32)))
static lua_input_event_t g_input_queue[LUA_RT_INPUT_QUEUE_CAPACITY];
static uint8_t g_input_head = 0;
static uint8_t g_input_tail = 0;
static uint8_t g_input_count = 0;
static lua_input_event_t g_current_input;
static bool g_has_current_input = false;

__attribute__((section(".ram_runtime"), aligned(32)))
static lua_message_event_t g_message_queue[LUA_RT_MESSAGE_QUEUE_CAPACITY];
static uint8_t g_message_head = 0;
static uint8_t g_message_tail = 0;
static uint8_t g_message_count = 0;
static lua_message_event_t g_current_message;
static bool g_has_current_message = false;

static lua_scheduler_phase_t g_scheduler_phase = LUA_SCHED_IDLE;
static size_t g_scheduler_instance = 0;
static uint8_t g_fixed_steps_remaining = 0;
static float g_frame_dt = 0.0f;

static lua_State     *g_entry_thread = NULL;
static lua_script_instance_t *g_entry_instance = NULL;
static lua_lifecycle_t g_entry_lifecycle = LUA_LIFECYCLE_COUNT;
static bool           g_entry_sleeping = false;
static uint32_t       g_entry_wake_ms = 0;

typedef enum {
    LUA_VM_RUNTIME_STATE_STOPPED = 0,
    LUA_VM_RUNTIME_STATE_INITIALIZED = 1,
    LUA_VM_RUNTIME_STATE_RUNNING = 2,
    LUA_VM_RUNTIME_STATE_BUSY = 3,
} lua_vm_runtime_state_t;

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

static const char *const k_lifecycle_names[LUA_LIFECYCLE_COUNT] = {
    "init",
    "final",
    "fixed_update",
    "update",
    "late_update",
    "on_message",
    "on_input",
    "on_reload",
};

/**
 * @brief  初始化脚本实例持有的Lua registry引用字段
 * @param  instance: 待初始化的脚本实例
 * @retval None
 * @note   - 本函数只写实例结构体，不访问Lua栈
 *         - 所有引用会置为LUA_NOREF，thread指针置空
 */
static void lua_rt_init_instance_refs(lua_script_instance_t *instance)
{
    instance->env_ref = LUA_NOREF;
    instance->self_ref = LUA_NOREF;
    instance->thread = NULL;
    instance->thread_ref = LUA_NOREF;
    for (size_t i = 0; i < LUA_LIFECYCLE_COUNT; ++i) {
        instance->callback_refs[i] = LUA_NOREF;
    }
}

/**
 * @brief  释放脚本实例持有的Lua registry引用
 * @param  instance: 待释放的脚本实例
 * @retval None
 * @note   - 依赖全局g_L有效，g_L为空时直接返回
 *         - 会释放生命周期回调、env/self和协程引用，并清空thread指针
 *         - 本函数不修改alive/initialized/finalized等生命周期标志
 */
static void lua_rt_unref_instance(lua_script_instance_t *instance)
{
    if (!g_L || !instance) return;

    for (size_t i = 0; i < LUA_LIFECYCLE_COUNT; ++i) {
        if (instance->callback_refs[i] != LUA_NOREF) {
            luaL_unref(g_L, LUA_REGISTRYINDEX, instance->callback_refs[i]);
            instance->callback_refs[i] = LUA_NOREF;
        }
    }
    if (instance->env_ref != LUA_NOREF) {
        luaL_unref(g_L, LUA_REGISTRYINDEX, instance->env_ref);
        instance->env_ref = LUA_NOREF;
    }
    if (instance->self_ref != LUA_NOREF) {
        luaL_unref(g_L, LUA_REGISTRYINDEX, instance->self_ref);
        instance->self_ref = LUA_NOREF;
    }
    if (instance->thread_ref != LUA_NOREF) {
        luaL_unref(g_L, LUA_REGISTRYINDEX, instance->thread_ref);
        instance->thread_ref = LUA_NOREF;
        instance->thread = NULL;
    }
}

static int lua_rt_ref_env_function(lua_script_instance_t *instance, const char *name)
{
    lua_rawgeti(g_L, LUA_REGISTRYINDEX, instance->env_ref);
    lua_pushstring(g_L, name);
    lua_rawget(g_L, -2);
    lua_remove(g_L, -2);

    if (!lua_isfunction(g_L, -1)) {
        lua_pop(g_L, 1);
        return LUA_NOREF;
    }
    return luaL_ref(g_L, LUA_REGISTRYINDEX);
}

/**
 * @brief  从实例环境表缓存Lua生命周期回调引用
 * @param  instance: 目标脚本实例
 * @retval None
 * @note   - 会按k_lifecycle_names登记init/final/fixed_update/update等回调
 *         - init缺失时会兼容旧版start回调，并标记legacy_callbacks
 *         - 本函数会修改Lua栈并通过luaL_ref转移函数所有权到registry
 */
static void lua_rt_cache_callbacks(lua_script_instance_t *instance)
{
    for (size_t i = 0; i < LUA_LIFECYCLE_COUNT; ++i) {
        instance->callback_refs[i] =
            lua_rt_ref_env_function(instance, k_lifecycle_names[i]);
    }

    if (instance->callback_refs[LUA_LIFECYCLE_INIT] == LUA_NOREF) {
        int start_ref = lua_rt_ref_env_function(instance, "start");
        if (start_ref != LUA_NOREF) {
            instance->callback_refs[LUA_LIFECYCLE_INIT] = start_ref;
            instance->legacy_callbacks = true;
        }
    }
}

/**
 * @brief  基于栈顶已加载chunk创建一个脚本实例
 * @param  source: 脚本来源类型
 * @param  source_path: 来源路径或chunk名称，可为NULL
 * @retval 0=创建成功, 负值=Lua状态非法、实例已满、_ENV缺失或chunk执行失败
 * @note   - 调用前栈顶必须是lua_load/luaL_loadbuffer得到的函数
 *         - 会创建独立_ENV、self表和协程，并缓存生命周期回调
 *         - 失败路径会释放已登记的registry引用并恢复Lua栈
 *         - 运行期新增实例且调度器空闲时会重新进入INIT阶段
 */
static int lua_rt_create_instance_from_loaded(lua_script_source_t source,
                                              const char *source_path)
{
    if (!g_L || !lua_isfunction(g_L, -1)) return -1;
    if (g_instance_count >= LUA_RT_MAX_INSTANCES) {
        lua_rt_log("lua script instance limit reached\n");
        lua_pop(g_L, 1);
        return -2;
    }

    const int stack_base = lua_gettop(g_L) - 1;
    const int chunk_index = lua_gettop(g_L);
    lua_script_instance_t *instance = &g_instances[g_instance_count];
    memset(instance, 0, sizeof(*instance));
    lua_rt_init_instance_refs(instance);

    lua_newtable(g_L);
    const int env_index = lua_gettop(g_L);

    lua_newtable(g_L);
    lua_pushglobaltable(g_L);
    lua_setfield(g_L, -2, "__index");
    lua_setmetatable(g_L, env_index);

    lua_pushvalue(g_L, env_index);
    instance->env_ref = luaL_ref(g_L, LUA_REGISTRYINDEX);

    lua_pushvalue(g_L, env_index);
    if (lua_setupvalue(g_L, chunk_index, 1) == NULL) {
        lua_rt_log("lua chunk has no _ENV upvalue\n");
        lua_rt_unref_instance(instance);
        lua_settop(g_L, stack_base);
        return -3;
    }

    lua_remove(g_L, env_index);
    if (lua_rt_pcall(g_L, 0, 0) != 0) {
        lua_rt_unref_instance(instance);
        lua_settop(g_L, stack_base);
        return -4;
    }

    lua_newtable(g_L);
    instance->self_ref = luaL_ref(g_L, LUA_REGISTRYINDEX);
    instance->thread = lua_newthread(g_L);
    instance->thread_ref = luaL_ref(g_L, LUA_REGISTRYINDEX);
    instance->alive = true;
    instance->source = source;
    if (source_path) {
        snprintf(instance->source_path, sizeof(instance->source_path), "%s", source_path);
    }
    lua_rt_cache_callbacks(instance);

    ++g_instance_count;
    if (g_runtime_started && g_scheduler_phase == LUA_SCHED_IDLE) {
        g_scheduler_phase = LUA_SCHED_INIT;
        g_scheduler_instance = 0;
    }
    lua_settop(g_L, stack_base);
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
    return SD_FATFS_Mount();
}

/**
 * @brief  lua_load文件读取回调
 * @param  L: Lua状态机，当前未使用
 * @param  ud: lua_rt_file_reader_t读取上下文
 * @param  size: 输出本次返回缓冲的字节数
 * @return 非NULL=本次读取缓冲, NULL=EOF或读取失败
 * @note   - 读取失败会记录reader->read_result供外层区分错误和EOF
 *         - 返回的缓冲归reader所有，仅在下一次reader调用前有效
 */
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

/**
 * @brief  从FatFs文件加载Lua字节码chunk到Lua栈顶
 * @param  L: Lua状态机
 * @param  path: 文件路径，可为带盘符路径、绝对路径或相对SD路径
 * @retval 0=加载成功且栈顶为chunk函数, 负值=路径非法、打开/读取/关闭或lua_load失败
 * @note   - FR_NOT_ENABLED/FR_INVALID_DRIVE时会尝试挂载SD后重开文件
 *         - 失败路径会弹出lua_load产生的错误对象并保持栈可控
 *         - 成功后调用方负责执行chunk并管理栈顶函数
 */
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

/**
 * @brief  加载并创建一份 Lua 字节码脚本实例
 * @param  bytecode: Lua 字节码缓冲区
 * @param  len: 字节码长度
 * @param  chunk_name: Lua chunk 名称，NULL时使用默认名称
 * @retval 0=加载并创建实例成功
 * @retval -1=Lua state 或参数非法
 * @retval -2=已有生命周期协程正在运行
 * @retval -3=字节码加载失败
 * @retval -4=脚本实例创建失败
 * @note   本函数要求 Lua VM 已初始化，不会启动 runtime frame loop
 */
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

    int create_rc = lua_rt_create_instance_from_loaded(LUA_SCRIPT_SOURCE_NONE,
                                                        chunk_name);
    return create_rc == 0 ? 0 : -4;
}

/**
 * @brief  从 FatFs 文件加载并创建 Lua 脚本实例
 * @param  path: Lua 字节码文件路径
 * @retval 0=加载并创建实例成功
 * @retval 非0=Lua state未初始化、已有生命周期协程运行、挂载/打开/读取/加载或实例创建失败
 * @note   本函数只加载文件实例，不启动 runtime frame loop
 */
int lua_run_file(const char *path)
{
    if (!g_L) return -1;
    if (g_entry_thread) return -2;

    int rc = lua_rt_load_file(g_L, path);
    if (rc != 0) return rc;

    int create_rc = lua_rt_create_instance_from_loaded(LUA_SCRIPT_SOURCE_FILE, path);
    return create_rc == 0 ? 0 : -6;
}

/**
 * @brief  打开cart文件并准备FatFs读取上下文
 * @param  reader: cart读取上下文
 * @param  path: cart路径，可为SD相对路径或带盘符路径
 * @param  fatfs_path: 输出转换后的FatFs路径缓冲
 * @param  fatfs_path_size: fatfs_path缓冲区字节数
 * @retval 0=打开成功, 负值=参数非法、挂载失败或cart打开失败
 * @note   - 会先尝试挂载SD卡
 *         - 遇到XHGC_CART_E_IO时会刷新FatFs挂载状态并重试一次
 *         - 成功后reader持有打开的cart_file，调用方负责关闭
 */
static int lua_rt_open_cart(lua_rt_cart_reader_t *reader,
                            const char *path,
                            char *fatfs_path,
                            size_t fatfs_path_size)
{
    if (!reader || !path || !fatfs_path || fatfs_path_size == 0u) return -1;

    lua_rt_make_sd_path(fatfs_path, fatfs_path_size, path);
    FRESULT fr = lua_rt_mount_sd();
    if (fr != FR_OK) {
        lua_rt_log("cart mount failed: ");
        lua_rt_log(lua_rt_fr2str(fr));
        lua_rt_log("\n");
        return -2;
    }

    int rc = xhgc_cart_open_fatfs(&reader->cart_file, fatfs_path);
    if (rc == XHGC_CART_E_IO) {
        SD_FATFS_InvalidateMount();
        fr = lua_rt_mount_sd();
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

/**
 * @brief  选择cart入口字节码来源并生成Lua chunk名称
 * @param  reader: 已打开的cart读取上下文
 * @param  fatfs_path: FatFs cart路径，用于生成chunk名称
 * @param  chunk_name: 输出Lua chunk名称缓冲
 * @param  chunk_name_size: chunk_name缓冲区字节数
 * @retval 0=入口选择成功, 负值=入口slot/header/manifest/file查找失败
 * @note   - 优先使用XHGC_CART_SLOT_ENTRY
 *         - slot不存在时回退到header.entry或manifest中的entry文件
 *         - 成功时会写入reader->source、total_size和对应slot/file元信息
 */
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

/**
 * @brief  lua_load的cart字节码流式读取回调
 * @param  L: Lua状态机，当前未使用
 * @param  ud: lua_rt_cart_reader_t读取上下文
 * @param  size: 输出本次返回缓冲的字节数
 * @return 非NULL=本次读取缓冲, NULL=EOF或读取失败
 * @note   - 支持从ENTRY slot或cart文件资源两种来源读取
 *         - 读取失败会写入reader->read_result供外层错误处理
 *         - 返回缓冲归reader所有，仅在下一次reader调用前有效
 */
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

/**
 * @brief  从cart入口加载Lua字节码chunk到Lua栈顶
 * @param  L: Lua状态机
 * @param  cart_path: cart文件路径
 * @retval 0=加载成功且栈顶为chunk函数, 负值=路径非法、打开/选择/读取或lua_load失败
 * @note   - 会打开cart、选择入口、通过lua_rt_cart_reader流式喂给lua_load
 *         - 无论lua_load成功与否都会关闭cart FatFs文件
 *         - 失败路径会弹出lua_load错误对象，避免Lua栈残留错误值
 */
static int lua_rt_load_cart_entry(lua_State *L, const char *cart_path)
{
    if (!cart_path || cart_path[0] == '\0') return -1;

    char fatfs_path[256];
    char chunk_name[400];
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

/**
 * @brief  从 cart 入口加载并创建 Lua 脚本实例
 * @param  cart_path: cart 文件路径
 * @retval 0=加载并创建实例成功
 * @retval 非0=Lua state未初始化、已有生命周期协程运行、cart打开/读取/加载或实例创建失败
 * @note   成功加载脚本前会尝试挂载 cart 资源索引；资源索引失败只记录日志，不阻止脚本实例创建
 */
int lua_run_cart_entry(const char *cart_path)
{
    if (!g_L) return -1;
    if (g_entry_thread) return -2;

    int rc = lua_rt_load_cart_entry(g_L, cart_path);
    if (rc != 0) return rc;

    if (!res_manager_mount_cart(cart_path)) {
        lua_rt_log("cart resource index mount failed: ");
        lua_rt_log(res_last_error() ? res_last_error() : "unknown");
        lua_rt_log("\n");
    }

    int create_rc =
        lua_rt_create_instance_from_loaded(LUA_SCRIPT_SOURCE_CART, cart_path);
    return create_rc == 0 ? 0 : -7;
}

/* -------------------- 生命周期函数与帧调度 -------------------- */

#define LUA_RT_MAX_DT_MS 100u

static bool lua_rt_time_reached(uint32_t now, uint32_t target)
{
    return (int32_t)(now - target) >= 0;
}

/**
 * @brief  让当前 Lua 生命周期协程延迟恢复
 * @param  delay_ms: 延迟毫秒数
 * @retval None
 * @note   本函数设置全局 entry wake 时间和 sleeping 标志，由 lua_update_task 轮询恢复
 */
void lua_rt_delay_ms(uint32_t delay_ms)
{
    g_entry_wake_ms = lua_rt_time_ms() + delay_ms;
    g_entry_sleeping = true;
}

/**
 * @brief  获取当前入口脚本对应的 cart 路径
 * @return 非NULL=当前 cart 路径, NULL=当前无 cart 入口实例
 */
const char *lua_current_cart_path(void)
{
    if (!g_entry_instance || g_entry_instance->source != LUA_SCRIPT_SOURCE_CART) {
        return NULL;
    }
    return g_entry_instance->source_path[0] != '\0' ? g_entry_instance->source_path : NULL;
}

static void lua_rt_clear_entry(void)
{
    if (g_entry_thread) {
        (void)lua_closethread(g_entry_thread, g_L);
        lua_settop(g_entry_thread, 0);
    }

    g_entry_thread = NULL;
    g_entry_instance = NULL;
    g_entry_lifecycle = LUA_LIFECYCLE_COUNT;
    g_entry_sleeping = false;
    g_entry_wake_ms = 0;
}

static void lua_rt_finish_lifecycle(bool success)
{
    if (g_entry_instance && g_entry_lifecycle == LUA_LIFECYCLE_INIT) {
        g_entry_instance->initialized = success;
        if (!success) {
            lua_rt_log("lua init() failed; instance disabled\n");
        }
    }
}

/**
 * @brief  恢复当前生命周期协程并处理yield/完成/错误
 * @param  nargs: 传递给协程入口函数的参数数量
 * @retval 1=协程yield等待后续tick恢复
 * @retval 0=生命周期回调执行完成
 * @retval -1=协程执行错误并已清理当前entry状态
 * @note   - yield路径会丢弃协程返回值但保留entry状态
 *         - 成功/失败路径会更新init生命周期完成状态并调用lua_rt_clear_entry
 *         - 错误路径会生成traceback并写入runtime日志
 */
static int lua_rt_resume_entry(int nargs)
{
    int nresults = 0;
    g_entry_sleeping = false;

    int rc = lua_resume(g_entry_thread, g_L, nargs, &nresults);
    if (rc == LUA_YIELD) {
        if (nresults > 0) lua_pop(g_entry_thread, nresults);
        return 1;
    }

    if (rc == LUA_OK) {
        if (nresults > 0) lua_pop(g_entry_thread, nresults);
        lua_rt_finish_lifecycle(true);
        lua_rt_clear_entry();
        return 0;
    }

    const char *err = lua_tostring(g_entry_thread, -1);
    luaL_traceback(g_L, g_entry_thread,
                   err ? err : "(lua coroutine error)", 1);
    lua_rt_log(lua_tostring(g_L, -1));
    lua_rt_log("\n");
    lua_pop(g_L, 1);
    lua_rt_finish_lifecycle(false);
    lua_rt_clear_entry();
    return -1;
}

/**
 * @brief  轮询当前生命周期协程是否可以恢复
 * @param  now: 当前毫秒tick
 * @retval 1=仍在sleep/yield等待
 * @retval 0=无当前协程或本次恢复完成
 * @retval -1=恢复时发生Lua错误
 * @note   - sleep未到期时不会恢复协程
 *         - 恢复前后会保存并恢复主Lua栈高度
 */
static int lua_rt_poll_entry(uint32_t now)
{
    if (!g_entry_thread) return 0;
    if (g_entry_sleeping && !lua_rt_time_reached(now, g_entry_wake_ms)) {
        return 1;
    }
    const int stack_base = lua_gettop(g_L);
    int rc = lua_rt_resume_entry(0);
    lua_settop(g_L, stack_base);
    return rc;
}

static void lua_rt_push_input_action(lua_State *L, const LuaInputAction *action)
{
    lua_createtable(L, 0, 9);

    lua_pushstring(L, action->event);
    lua_setfield(L, -2, "event");
    lua_pushboolean(L, action->pressed);
    lua_setfield(L, -2, "pressed");
    lua_pushboolean(L, action->released);
    lua_setfield(L, -2, "released");
    lua_pushboolean(L, action->repeated);
    lua_setfield(L, -2, "repeated");
    lua_pushnumber(L, action->value);
    lua_setfield(L, -2, "value");
    lua_pushnumber(L, action->x);
    lua_setfield(L, -2, "x");
    lua_pushnumber(L, action->y);
    lua_setfield(L, -2, "y");
    lua_pushnumber(L, action->dx);
    lua_setfield(L, -2, "dx");
    lua_pushnumber(L, action->dy);
    lua_setfield(L, -2, "dy");
}

/**
 * @brief  启动指定脚本实例的生命周期回调协程
 * @param  instance: 脚本实例
 * @param  lifecycle: 要调度的生命周期类型
 * @param  dt: update/fixed_update/late_update使用的时间步长
 * @retval 1=回调yield等待后续tick恢复
 * @retval 0=回调不存在或执行完成
 * @retval -1=状态非法、协程缺失或回调执行错误
 * @note   - 非init生命周期要求实例已initialized
 *         - 会按生命周期压入self、dt、input或message参数
 *         - 会复用实例thread并设置全局entry状态，调度期间不允许并发entry
 */
static int lua_rt_begin_lifecycle(lua_script_instance_t *instance,
                                  lua_lifecycle_t lifecycle,
                                  float dt)
{
    if (!g_L || !instance || !instance->alive || g_entry_thread) return -1;
    if (lifecycle != LUA_LIFECYCLE_INIT && !instance->initialized) return 0;

    int callback_ref = instance->callback_refs[lifecycle];
    if (callback_ref == LUA_NOREF) {
        if (lifecycle == LUA_LIFECYCLE_INIT) {
            instance->initialized = true;
        }
        return 0;
    }

    const int stack_base = lua_gettop(g_L);
    int nargs = 0;
    lua_rawgeti(g_L, LUA_REGISTRYINDEX, callback_ref);

    bool legacy_no_self =
        instance->legacy_callbacks &&
        (lifecycle == LUA_LIFECYCLE_INIT || lifecycle == LUA_LIFECYCLE_UPDATE);
    if (!legacy_no_self) {
        lua_rawgeti(g_L, LUA_REGISTRYINDEX, instance->self_ref);
        ++nargs;
    }

    switch (lifecycle) {
    case LUA_LIFECYCLE_FIXED_UPDATE:
    case LUA_LIFECYCLE_UPDATE:
    case LUA_LIFECYCLE_LATE_UPDATE:
        lua_pushnumber(g_L, (lua_Number)dt);
        ++nargs;
        break;
    case LUA_LIFECYCLE_INPUT:
        lua_pushstring(g_L, g_current_input.action_id);
        lua_rt_push_input_action(g_L, &g_current_input.action);
        nargs += 2;
        break;
    case LUA_LIFECYCLE_MESSAGE:
        lua_pushstring(g_L, g_current_message.message_id);
        lua_pushnil(g_L);
        if (g_current_message.sender[0] != '\0') {
            lua_pushstring(g_L, g_current_message.sender);
        } else {
            lua_pushnil(g_L);
        }
        nargs += 3;
        break;
    default:
        break;
    }

    g_entry_thread = instance->thread;
    if (!g_entry_thread) {
        lua_settop(g_L, stack_base);
        return -1;
    }
    (void)lua_closethread(g_entry_thread, g_L);
    lua_settop(g_entry_thread, 0);
    g_entry_instance = instance;
    g_entry_lifecycle = lifecycle;
    g_entry_sleeping = false;

    lua_xmove(g_L, g_entry_thread, nargs + 1);
    int rc = lua_rt_resume_entry(nargs);
    lua_settop(g_L, stack_base);
    return rc;
}

/**
 * @brief  直接同步调用脚本实例生命周期回调
 * @param  instance: 脚本实例
 * @param  lifecycle: 要调用的生命周期类型
 * @retval 0=回调不存在或调用成功, -1=状态非法或Lua调用失败
 * @note   - 当前用于final/on_reload等不走调度协程的生命周期
 *         - 调用前后会恢复主Lua栈高度
 */
static int lua_rt_call_direct(lua_script_instance_t *instance,
                              lua_lifecycle_t lifecycle)
{
    if (!g_L || !instance || !instance->alive) return -1;
    int callback_ref = instance->callback_refs[lifecycle];
    if (callback_ref == LUA_NOREF) return 0;

    const int stack_base = lua_gettop(g_L);
    lua_rawgeti(g_L, LUA_REGISTRYINDEX, callback_ref);
    lua_rawgeti(g_L, LUA_REGISTRYINDEX, instance->self_ref);
    int rc = lua_rt_pcall(g_L, 1, 0);
    lua_settop(g_L, stack_base);
    return rc;
}

/**
 * @brief  删除脚本实例self.children持有的UI子节点
 * @param  instance: 脚本实例
 * @retval None
 * @note   - 依赖全局Lua state和实例self_ref有效
 *         - 会调用lua_ui_delete_children并将self.children置为nil
 *         - 调用前后会恢复主Lua栈高度
 */
static void lua_rt_delete_instance_children(lua_script_instance_t *instance)
{
    if (!g_L || !instance || instance->self_ref == LUA_NOREF) return;

    const int stack_base = lua_gettop(g_L);
    lua_rawgeti(g_L, LUA_REGISTRYINDEX, instance->self_ref);
    if (lua_istable(g_L, -1)) {
        lua_getfield(g_L, -1, "children");
        if (!lua_isnil(g_L, -1)) {
            lua_ui_delete_children(g_L, -1);
        }
        lua_pop(g_L, 1);

        lua_pushnil(g_L);
        lua_setfield(g_L, -2, "children");
    }
    lua_settop(g_L, stack_base);
}

static bool lua_rt_pop_input(lua_input_event_t *event)
{
    if (!event || g_input_count == 0u) return false;
    *event = g_input_queue[g_input_head];
    g_input_head = (uint8_t)((g_input_head + 1u) % LUA_RT_INPUT_QUEUE_CAPACITY);
    --g_input_count;
    return true;
}

static bool lua_rt_pop_message(lua_message_event_t *event)
{
    if (!event || g_message_count == 0u) return false;
    *event = g_message_queue[g_message_head];
    g_message_head =
        (uint8_t)((g_message_head + 1u) % LUA_RT_MESSAGE_QUEUE_CAPACITY);
    --g_message_count;
    return true;
}

/**
 * @brief  向 Lua runtime 输入队列投递一条输入事件
 * @param  action_id: 输入动作ID，不能为空字符串
 * @param  action: 输入动作数据
 * @retval 0=投递成功
 * @retval -1=参数非法
 * @retval -2=输入队列已满
 * @note   事件会在后续 lua_update_task 的 input 阶段分发给 on_input
 */
int lua_post_input(const char *action_id, const LuaInputAction *action)
{
    if (!action_id || !action || action_id[0] == '\0') return -1;
    if (g_input_count >= LUA_RT_INPUT_QUEUE_CAPACITY) {
        lua_rt_log("lua input queue full\n");
        return -2;
    }

    lua_input_event_t *event = &g_input_queue[g_input_tail];
    snprintf(event->action_id, sizeof(event->action_id), "%s", action_id);
    event->action = *action;
    g_input_tail = (uint8_t)((g_input_tail + 1u) % LUA_RT_INPUT_QUEUE_CAPACITY);
    ++g_input_count;
    return 0;
}

/**
 * @brief  向 Lua runtime 消息队列投递一条消息
 * @param  message_id: 消息ID，不能为空字符串
 * @param  sender: 发送者字符串，NULL时记录为空字符串
 * @retval 0=投递成功
 * @retval -1=参数非法
 * @retval -2=消息队列已满
 * @note   事件会在后续 lua_update_task 的 message 阶段分发给 on_message
 */
int lua_post_message(const char *message_id, const char *sender)
{
    if (!message_id || message_id[0] == '\0') return -1;
    if (g_message_count >= LUA_RT_MESSAGE_QUEUE_CAPACITY) {
        lua_rt_log("lua message queue full\n");
        return -2;
    }

    lua_message_event_t *event = &g_message_queue[g_message_tail];
    snprintf(event->message_id, sizeof(event->message_id), "%s", message_id);
    snprintf(event->sender, sizeof(event->sender), "%s", sender ? sender : "");
    g_message_tail =
        (uint8_t)((g_message_tail + 1u) % LUA_RT_MESSAGE_QUEUE_CAPACITY);
    ++g_message_count;
    return 0;
}

static void lua_rt_scheduler_next_phase(lua_scheduler_phase_t phase)
{
    g_scheduler_phase = phase;
    g_scheduler_instance = 0;
}

/**
 * @brief  推进Lua runtime生命周期调度状态机
 * @retval None
 * @note   - 调度顺序为init、input、fixed_update、update、late_update、message
 *         - 生命周期回调yield时立即返回，后续tick继续恢复
 *         - 会消费输入/消息队列并遍历所有已初始化且alive的实例
 *         - 本函数只在没有当前entry协程时推进新生命周期
 */
static void lua_rt_drive_scheduler(void)
{
    while (g_L && !g_entry_thread) {
        lua_script_instance_t *instance = NULL;
        int rc = 0;

        switch (g_scheduler_phase) {
        case LUA_SCHED_INIT:
            if (g_scheduler_instance >= g_instance_count) {
                lua_rt_scheduler_next_phase(LUA_SCHED_IDLE);
                continue;
            }
            instance = &g_instances[g_scheduler_instance++];
            if (!instance->alive || instance->initialized) continue;
            rc = lua_rt_begin_lifecycle(instance, LUA_LIFECYCLE_INIT, 0.0f);
            if (rc == 1) return;
            continue;

        case LUA_SCHED_INPUT:
            if (!g_has_current_input) {
                if (!lua_rt_pop_input(&g_current_input)) {
                    lua_rt_scheduler_next_phase(LUA_SCHED_FIXED_UPDATE);
                    continue;
                }
                g_has_current_input = true;
            }
            if (g_scheduler_instance >= g_instance_count) {
                g_has_current_input = false;
                g_scheduler_instance = 0;
                continue;
            }
            instance = &g_instances[g_scheduler_instance++];
            if (!instance->alive || !instance->initialized) continue;
            rc = lua_rt_begin_lifecycle(instance, LUA_LIFECYCLE_INPUT, 0.0f);
            if (rc == 1) return;
            continue;

        case LUA_SCHED_FIXED_UPDATE:
            if (g_fixed_steps_remaining == 0u) {
                lua_rt_scheduler_next_phase(LUA_SCHED_UPDATE);
                continue;
            }
            if (g_scheduler_instance >= g_instance_count) {
                --g_fixed_steps_remaining;
                g_scheduler_instance = 0;
                continue;
            }
            instance = &g_instances[g_scheduler_instance++];
            if (!instance->alive || !instance->initialized) continue;
            rc = lua_rt_begin_lifecycle(instance, LUA_LIFECYCLE_FIXED_UPDATE,
                                        LUA_RT_FIXED_DT);
            if (rc == 1) return;
            continue;

        case LUA_SCHED_UPDATE:
            if (g_scheduler_instance >= g_instance_count) {
                lua_rt_scheduler_next_phase(LUA_SCHED_LATE_UPDATE);
                continue;
            }
            instance = &g_instances[g_scheduler_instance++];
            if (!instance->alive || !instance->initialized) continue;
            rc = lua_rt_begin_lifecycle(instance, LUA_LIFECYCLE_UPDATE, g_frame_dt);
            if (rc == 1) return;
            continue;

        case LUA_SCHED_LATE_UPDATE:
            if (g_scheduler_instance >= g_instance_count) {
                lua_rt_scheduler_next_phase(LUA_SCHED_MESSAGE);
                continue;
            }
            instance = &g_instances[g_scheduler_instance++];
            if (!instance->alive || !instance->initialized) continue;
            rc = lua_rt_begin_lifecycle(instance, LUA_LIFECYCLE_LATE_UPDATE,
                                        g_frame_dt);
            if (rc == 1) return;
            continue;

        case LUA_SCHED_MESSAGE:
            if (!g_has_current_message) {
                if (!lua_rt_pop_message(&g_current_message)) {
                    lua_rt_scheduler_next_phase(LUA_SCHED_IDLE);
                    continue;
                }
                g_has_current_message = true;
            }
            if (g_scheduler_instance >= g_instance_count) {
                g_has_current_message = false;
                g_scheduler_instance = 0;
                continue;
            }
            instance = &g_instances[g_scheduler_instance++];
            if (!instance->alive || !instance->initialized) continue;
            rc = lua_rt_begin_lifecycle(instance, LUA_LIFECYCLE_MESSAGE, 0.0f);
            if (rc == 1) return;
            continue;

        case LUA_SCHED_IDLE:
        default:
            return;
        }
    }
}

/* -------------------- 对外：初始化、重载、销毁与 tick -------------------- */

/**
 * @brief  初始化Lua runtime全局状态
 * @retval 0=初始化成功或已经初始化
 * @retval -2=Lua state创建失败
 * @note   - 会创建Lua state、打开库、绑定LuaPort模块并初始化resource_manager
 *         - 会清空脚本实例表、输入/消息队列和调度状态
 *         - 会打印Lua VM内存统计
 */
static int lua_rt_init_state(void)
{
    if (g_L) return 0;

    g_L = lua_vm_newstate();
    if (!g_L) {
        lua_rt_log("lua_vm_newstate failed (allocator: lua_vm_alloc)\n");
        lua_vm_memory_print_stats();
        return -2;
    }

    lua_rt_openlibs(g_L);
    lua_port_bind(g_L, NULL);
    res_manager_init();

    memset(g_instances, 0, sizeof(g_instances));
    memset(g_input_queue, 0, sizeof(g_input_queue));
    memset(g_message_queue, 0, sizeof(g_message_queue));
    g_instance_count = 0;
    g_runtime_started = false;
    g_scheduler_phase = LUA_SCHED_IDLE;
    g_fixed_accumulator = 0.0f;
    lua_vm_memory_print_stats();
    return 0;
}

/**
 * @brief  加载并创建内嵌boot.lua脚本实例
 * @retval 0=实例创建成功, 负值=内嵌脚本为空、加载失败或实例创建失败
 * @note   - 脚本文本来自可覆盖的lua_get_boot_script
 *         - 本函数只创建实例，不启动runtime调度
 */
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

    return lua_rt_create_instance_from_loaded(LUA_SCRIPT_SOURCE_EMBEDDED,
                                               "boot.lua");
}

/**
 * @brief  启动Lua runtime调度循环状态
 * @retval 0=启动状态设置完成
 * @note   - 会记录当前tick、清空fixed accumulator并置runtime_started
 *         - 会进入INIT调度阶段并立即驱动一次调度
 */
static int lua_rt_start_runtime(void)
{
    g_last_ms = lua_rt_time_ms();
    g_fixed_accumulator = 0.0f;
    g_runtime_started = true;
    lua_rt_scheduler_next_phase(LUA_SCHED_INIT);
    lua_rt_drive_scheduler();
    return 0;
}

/**
 * @brief  初始化 Lua runtime 并从文件启动入口脚本
 * @param  path: Lua 字节码文件路径
 * @retval 0=初始化、加载并启动成功
 * @retval 非0=Lua state初始化、文件加载、实例创建或 runtime 启动失败
 * @note   本函数会初始化 Lua VM、绑定模块、初始化 resource_manager，并启动 init 调度
 */
int lua_init_from_file(const char *path)
{
    int rc = lua_rt_init_state();
    if (rc != 0) return rc;

    rc = lua_run_file(path);
    if (rc != 0) return rc;
    return lua_rt_start_runtime();
}

/**
 * @brief  初始化 Lua runtime 并从 cart 入口启动脚本
 * @param  cart_path: cart 文件路径
 * @retval 0=初始化、加载并启动成功
 * @retval 非0=Lua state初始化、cart加载、实例创建或 runtime 启动失败
 * @note   本函数会初始化 Lua VM、绑定模块、初始化 resource_manager，并启动 init 调度
 */
int lua_init_from_cart(const char *cart_path)
{
    int rc = lua_rt_init_state();
    if (rc != 0) return rc;

    rc = lua_run_cart_entry(cart_path);
    if (rc != 0) return rc;
    return lua_rt_start_runtime();
}

/**
 * @brief  按默认优先级初始化并启动 Lua runtime
 * @retval 0=初始化并启动成功
 * @retval 非0=Lua state初始化或所有启动源加载失败
 * @note   启动源优先级为 boot cart、boot bytecode 文件、内嵌 boot.lua
 */
int lua_init(void)
{
    int rc = lua_rt_init_state();
    if (rc != 0) return rc;

    const char *cart_path = lua_get_boot_cart_path();
    if (cart_path && cart_path[0] != '\0') {
        rc = lua_run_cart_entry(cart_path);
        if (rc == 0) return lua_rt_start_runtime();
    }

    const char *boot_path = lua_get_boot_bytecode_path();
    if (boot_path && boot_path[0] != '\0') {
        rc = lua_run_file(boot_path);
        if (rc == 0) return lua_rt_start_runtime();
    }

    rc = lua_rt_run_embedded_boot();
    if (rc != 0) return rc;
    return lua_rt_start_runtime();
}

/**
 * @brief  释放脚本实例缓存的生命周期回调引用
 * @param  instance: 脚本实例
 * @retval None
 * @note   - 仅释放callback_refs数组，不释放env/self/thread引用
 *         - 调用方需保证全局g_L有效
 */
static void lua_rt_unref_callbacks(lua_script_instance_t *instance)
{
    for (size_t i = 0; i < LUA_LIFECYCLE_COUNT; ++i) {
        if (instance->callback_refs[i] != LUA_NOREF) {
            luaL_unref(g_L, LUA_REGISTRYINDEX, instance->callback_refs[i]);
            instance->callback_refs[i] = LUA_NOREF;
        }
    }
}

/**
 * @brief  清除脚本环境表中的生命周期回调字段
 * @param  instance: 脚本实例
 * @retval None
 * @note   - 会把init/final/update等生命周期字段以及兼容start字段置为nil
 *         - 调用方需保证instance->env_ref有效并在调用后维护Lua栈平衡
 */
static void lua_rt_clear_env_callbacks(lua_script_instance_t *instance)
{
    lua_rawgeti(g_L, LUA_REGISTRYINDEX, instance->env_ref);
    for (size_t i = 0; i < LUA_LIFECYCLE_COUNT; ++i) {
        lua_pushstring(g_L, k_lifecycle_names[i]);
        lua_pushnil(g_L);
        lua_rawset(g_L, -3);
    }
    lua_pushliteral(g_L, "start");
    lua_pushnil(g_L);
    lua_rawset(g_L, -3);
    lua_pop(g_L, 1);
}

/**
 * @brief  用栈顶已加载chunk重载脚本实例
 * @param  instance: 待重载脚本实例
 * @retval 0=重载并调用on_reload成功
 * @retval -1=新chunk缺少_ENV upvalue
 * @retval -2=重新执行chunk失败
 * @retval 其它负值=on_reload调用失败
 * @note   - 调用前栈顶必须是新加载的chunk函数
 *         - 会释放旧生命周期回调引用、清空环境中的旧回调字段并复用原_ENV
 *         - 成功执行chunk后会重新缓存生命周期回调并直接调用on_reload
 */
static int lua_rt_reload_instance_from_loaded(lua_script_instance_t *instance)
{
    const int stack_base = lua_gettop(g_L) - 1;
    const int chunk_index = lua_gettop(g_L);

    lua_rt_unref_callbacks(instance);
    lua_rt_clear_env_callbacks(instance);
    instance->legacy_callbacks = false;

    lua_rawgeti(g_L, LUA_REGISTRYINDEX, instance->env_ref);
    if (lua_setupvalue(g_L, chunk_index, 1) == NULL) {
        lua_rt_log("reload chunk has no _ENV upvalue\n");
        lua_settop(g_L, stack_base);
        return -1;
    }

    if (lua_rt_pcall(g_L, 0, 0) != 0) {
        lua_settop(g_L, stack_base);
        return -2;
    }

    lua_rt_cache_callbacks(instance);
    lua_settop(g_L, stack_base);
    return lua_rt_call_direct(instance, LUA_LIFECYCLE_RELOAD);
}

/**
 * @brief  重新加载当前 Lua 脚本实例
 * @retval 0=所有可重载实例处理完成
 * @retval -1=Lua state不可用、生命周期协程运行中或调度器非空闲
 * @retval -2=存在不支持 reload 的字节码实例
 * @retval -3=至少一个实例重新加载或 on_reload 调用失败
 * @note   本函数会重新执行实例来源脚本并调用 on_reload，不改变 cart/bin 文件格式
 */
int lua_reload(void)
{
    if (!g_L || g_entry_thread || g_scheduler_phase != LUA_SCHED_IDLE) return -1;

    int result = 0;
    for (size_t i = 0; i < g_instance_count; ++i) {
        lua_script_instance_t *instance = &g_instances[i];
        if (!instance->alive) continue;

        int rc = -1;
        if (instance->source == LUA_SCRIPT_SOURCE_FILE) {
            rc = lua_rt_load_file(g_L, instance->source_path);
        } else if (instance->source == LUA_SCRIPT_SOURCE_CART) {
            rc = lua_rt_load_cart_entry(g_L, instance->source_path);
        } else if (instance->source == LUA_SCRIPT_SOURCE_EMBEDDED) {
            size_t len = 0;
            const char *code = lua_get_boot_script(&len);
            rc = (code && len > 0u)
                ? luaL_loadbuffer(g_L, code, len, "boot.lua")
                : LUA_ERRSYNTAX;
            if (rc != LUA_OK && lua_gettop(g_L) > 0) lua_pop(g_L, 1);
        } else {
            lua_rt_log("lua reload unsupported for bytecode instance\n");
            result = -2;
            continue;
        }

        if (rc != 0 || lua_rt_reload_instance_from_loaded(instance) != 0) {
            lua_rt_log("lua reload failed\n");
            result = -3;
        }
    }
    return result;
}

/**
 * @brief  关闭 Lua runtime 并释放场景资源
 * @retval 0=关闭完成或 runtime 原本未初始化
 * @note   - 对已初始化且未 finalized 的实例会调用 final，然后删除其 UI 子节点
 * @note   - 本函数会 res_scene_reset、lua_close，并清空输入/消息队列和 runtime 状态
 */
int lua_shutdown(void)
{
    if (!g_L) return 0;

    if (g_entry_thread) {
        lua_rt_clear_entry();
    }
    g_scheduler_phase = LUA_SCHED_IDLE;

    for (size_t i = 0; i < g_instance_count; ++i) {
        lua_script_instance_t *instance = &g_instances[i];
        if (instance->alive && instance->initialized && !instance->finalized) {
            instance->finalized = true;
            (void)lua_rt_call_direct(instance, LUA_LIFECYCLE_FINAL);
            lua_rt_delete_instance_children(instance);
        }
        instance->alive = false;
        lua_rt_unref_instance(instance);
    }
    res_scene_reset();

    lua_close(g_L);
    g_L = NULL;
    lua_vm_memory_print_stats();
    g_instance_count = 0;
    g_runtime_started = false;
    g_input_head = g_input_tail = g_input_count = 0;
    g_message_head = g_message_tail = g_message_count = 0;
    g_has_current_input = false;
    g_has_current_message = false;
    return 0;
}

/**
 * @brief  驱动 Lua runtime 单次帧调度
 * @retval None
 * @note   - runtime 未启动时直接返回
 * @note   - 帧阶段顺序为 on_input、fixed_update、update、late_update、on_message
 * @note   - 生命周期协程 sleep 或 yield 时，本函数会在后续 tick 中继续轮询恢复
 */
void lua_update_task(void)
{
    if (!g_L || !g_runtime_started) return;

    const uint32_t now = lua_rt_time_ms();
    if (g_entry_thread) {
        if (lua_rt_poll_entry(now) == 1) {
            return;
        }
    }

    if (g_scheduler_phase != LUA_SCHED_IDLE) {
        lua_rt_drive_scheduler();
        if (g_entry_thread || g_scheduler_phase != LUA_SCHED_IDLE) {
            return;
        }
    }

    uint32_t elapsed = (uint32_t)(now - g_last_ms);
    if (elapsed < LUA_RT_PERIOD_MS) return;
    g_last_ms = now;
    if (elapsed > LUA_RT_MAX_DT_MS) elapsed = LUA_RT_MAX_DT_MS;

    g_frame_dt = (float)elapsed / 1000.0f;
    g_fixed_accumulator += g_frame_dt;
    g_fixed_steps_remaining = 0;
    while (g_fixed_accumulator >= LUA_RT_FIXED_DT &&
           g_fixed_steps_remaining < LUA_RT_MAX_FIXED_STEPS) {
        g_fixed_accumulator -= LUA_RT_FIXED_DT;
        ++g_fixed_steps_remaining;
    }
    if (g_fixed_steps_remaining == LUA_RT_MAX_FIXED_STEPS &&
        g_fixed_accumulator >= LUA_RT_FIXED_DT) {
        g_fixed_accumulator = 0.0f;
    }

    lua_rt_scheduler_next_phase(LUA_SCHED_INPUT);
    lua_rt_drive_scheduler();
}

uint32_t lua_vm_input_queue_len(void)
{
    return (uint32_t)g_input_count;
}

uint32_t lua_vm_input_queue_capacity(void)
{
    return (uint32_t)LUA_RT_INPUT_QUEUE_CAPACITY;
}

uint32_t lua_vm_message_queue_len(void)
{
    return (uint32_t)g_message_count;
}

uint32_t lua_vm_message_queue_capacity(void)
{
    return (uint32_t)LUA_RT_MESSAGE_QUEUE_CAPACITY;
}

uint32_t lua_vm_runtime_state(void)
{
    if (!g_L) {
        return LUA_VM_RUNTIME_STATE_STOPPED;
    }
    if (!g_runtime_started) {
        return LUA_VM_RUNTIME_STATE_INITIALIZED;
    }
    if (g_entry_thread != NULL || g_scheduler_phase != LUA_SCHED_IDLE) {
        return LUA_VM_RUNTIME_STATE_BUSY;
    }
    return LUA_VM_RUNTIME_STATE_RUNNING;
}
