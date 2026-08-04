#ifdef NDEBUG
#undef NDEBUG
#endif

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "cart_log.h"
#include "lauxlib.h"
#include "lua.h"
#include "lua_foundation.h"
#include "lua_port.h"
#include "lua_ui.h"
#include "lua_vm.h"
#include "lua_vm_memory.h"
#include "resource_manager.h"
#include "xhgc_cart.h"

static uint32_t g_tick;
static const char* g_boot_script;
static bool g_foundation_active;
static bool g_ui_active;
static uint32_t g_foundation_create_count;
static uint32_t g_foundation_destroy_count;
static uint32_t g_ui_create_count;
static uint32_t g_ui_destroy_count;

uint32_t lua_rt_time_ms(void) { return g_tick; }
uint32_t HAL_GetTick(void) { return g_tick; }
const char* lua_get_boot_cart_path(void) { return ""; }
const char* lua_get_boot_bytecode_path(void) { return ""; }

static const char k_init_failure_script[] =
      "function init(self)\n"
      "  self.state.created = true\n"
      "  error('intentional init failure')\n"
      "end\n"
      "function update(self, dt) error('update must not run') end\n";
static const char k_succeeding_script[] =
      "function init(self) self.state.created = true end\n"
      "function update(self, dt) self.state.updated = true end\n"
      "function final(self) self.state.finalized = true end\n";
static const char k_update_failure_script[] =
      "function init(self) self.state.ready = true end\n"
      "function update(self, dt) error('intentional update failure') end\n";
static const char k_input_failure_script[] =
      "function init(self) self.state.ready = true end\n"
      "function update(self, dt) end\n"
      "function on_input(self, id, action) error('intentional input failure') end\n";
static const char k_message_failure_script[] =
      "function init(self) self.state.ready = true end\n"
      "function update(self, dt) end\n"
      "function on_message(self, id, message, sender) "
      "error('intentional message failure') end\n";
static const char k_final_failure_script[] =
      "function init(self) self.state.ready = true end\n"
      "function update(self, dt) end\n"
      "function final(self) error('intentional final failure') end\n";

const char* lua_get_boot_script(size_t* length) {
  assert(g_boot_script != NULL);
  if (length != NULL) *length = strlen(g_boot_script);
  return g_boot_script;
}

void lua_rt_log(const char* message) { (void)message; }

lua_State* lua_vm_newstate(void) { return luaL_newstate(); }
void lua_vm_memory_print_stats(void) {}

void lua_port_bind(lua_State* L, const lua_port_config_t* config) {
  (void)L;
  (void)config;
}

void lua_ui_registry_init(void) {
  g_ui_active = false;
}

bool lua_ui_owner_create(lua_State* L, uint32_t id, uint32_t generation) {
  (void)L;
  assert(id != 0u && generation != 0u && !g_ui_active);
  g_ui_active = true;
  ++g_ui_create_count;
  return true;
}

void lua_ui_owner_destroy(lua_State* L, uint32_t id, uint32_t generation) {
  (void)L;
  (void)id;
  (void)generation;
  if (g_ui_active) {
    g_ui_active = false;
    ++g_ui_destroy_count;
  }
}

void lua_ui_owner_enter(lua_State* L, uint32_t id, uint32_t generation) {
  (void)L;
  (void)id;
  (void)generation;
}
void lua_ui_owner_leave(void) {}

void lua_foundation_registry_init(void) {
  g_foundation_active = false;
}

bool lua_foundation_owner_create(lua_State* L, uint32_t id,
                                 uint32_t generation, uint64_t cart_id,
                                 const char* app_id) {
  (void)L;
  (void)cart_id;
  assert(id != 0u && generation != 0u && app_id != NULL);
  assert(!g_foundation_active);
  g_foundation_active = true;
  ++g_foundation_create_count;
  return true;
}

void lua_foundation_owner_destroy(lua_State* L, uint32_t id,
                                  uint32_t generation) {
  (void)L;
  (void)id;
  (void)generation;
  if (g_foundation_active) {
    g_foundation_active = false;
    ++g_foundation_destroy_count;
  }
}

void lua_foundation_owner_enter(lua_State* L, uint32_t id,
                                uint32_t generation) {
  (void)L;
  (void)id;
  (void)generation;
}
void lua_foundation_owner_leave(void) {}
bool lua_foundation_current(lua_State* L,
                            lua_foundation_owner_view_t* owner) {
  (void)L;
  (void)owner;
  return false;
}
void lua_foundation_process(lua_State* L, uint64_t now_ms) {
  (void)L;
  (void)now_ms;
}
bool lua_foundation_storage_ready(void) { return true; }
uint64_t lua_foundation_platform_uptime_ms(void) { return g_tick; }

void res_manager_init(void) {}
bool res_manager_mount_cart(const char* path) {
  (void)path;
  return false;
}
void res_scene_reset(void) {}
const char* res_last_error(void) { return "not used by lifecycle test"; }

void CartLog_Write(cart_log_level_t level, const char* tag,
                   const char* message) {
  (void)level;
  (void)tag;
  (void)message;
}

FRESULT SD_FATFS_Mount(void) { return FR_NOT_READY; }
void SD_FATFS_InvalidateMount(void) {}
FRESULT f_open(FIL* file, const TCHAR* path, BYTE mode) {
  (void)file;
  (void)path;
  (void)mode;
  return FR_NOT_READY;
}
FRESULT f_read(FIL* file, void* buffer, UINT count, UINT* read_count) {
  (void)file;
  (void)buffer;
  (void)count;
  if (read_count != NULL) *read_count = 0u;
  return FR_NOT_READY;
}
FRESULT f_close(FIL* file) {
  (void)file;
  return FR_OK;
}

int xhgc_cart_open_fatfs(XHGC_CartFatFs* cart_file, const char* path) {
  (void)cart_file;
  (void)path;
  return XHGC_CART_E_IO;
}
void xhgc_cart_close_fatfs(XHGC_CartFatFs* cart_file) { (void)cart_file; }
int xhgc_cart_get_slot(const XHGC_Cart* cart, XHGC_CartSlotId slot_id,
                       XHGC_CartSlot* slot) {
  (void)cart;
  (void)slot_id;
  (void)slot;
  return XHGC_CART_E_NOT_FOUND;
}
int xhgc_cart_manf_get_string(const XHGC_Cart* cart, uint8_t field_id,
                              char* output, uint32_t output_size) {
  (void)cart;
  (void)field_id;
  if (output != NULL && output_size > 0u) output[0] = '\0';
  return XHGC_CART_E_NOT_FOUND;
}
int xhgc_cart_find_file(const XHGC_Cart* cart, const char* path,
                        XHGC_CartFile* file) {
  (void)cart;
  (void)path;
  (void)file;
  return XHGC_CART_E_NOT_FOUND;
}
int xhgc_cart_read_file(const XHGC_Cart* cart, const XHGC_CartFile* file,
                        uint32_t offset, void* buffer, uint32_t size) {
  (void)cart;
  (void)file;
  (void)offset;
  (void)buffer;
  (void)size;
  return XHGC_CART_E_IO;
}

static void run_init_failure_cycle(uint32_t iteration) {
  g_tick = iteration * 10u;
  assert(lua_init() == 0);
  assert(!lua_vm_get_runtime_error(NULL));
  lua_update_task();
  LuaRuntimeErrorInfo error;
  assert(lua_vm_get_runtime_error(&error));
  assert(error.stage == LUA_RUNTIME_ERROR_STAGE_INIT);
  assert(strstr(error.message, "intentional init failure") != NULL);
  assert(strstr(error.traceback, "stack traceback") != NULL);
  assert(!g_foundation_active && !g_ui_active);
  uint32_t foundation_destroyed = g_foundation_destroy_count;
  uint32_t ui_destroyed = g_ui_destroy_count;
  lua_update_task();
  assert(g_foundation_destroy_count == foundation_destroyed);
  assert(g_ui_destroy_count == ui_destroyed);
  assert(lua_shutdown() == 0);
  assert(g_foundation_destroy_count == foundation_destroyed);
  assert(g_ui_destroy_count == ui_destroyed);
}

static void drive_until_error(LuaRuntimeErrorStage expected_stage,
                              const char* expected_message) {
  for (uint32_t attempt = 0u; attempt < 20u &&
                             !lua_vm_get_runtime_error(NULL); ++attempt) {
    g_tick += 20u;
    lua_update_task();
  }
  LuaRuntimeErrorInfo error;
  assert(lua_vm_get_runtime_error(&error));
  assert(error.stage == expected_stage);
  assert(strstr(error.message, expected_message) != NULL);
  assert(strstr(error.traceback, "stack traceback") != NULL);
  assert(!g_foundation_active && !g_ui_active);
}

static void run_callback_failure(const char* script,
                                 LuaRuntimeErrorStage expected_stage,
                                 const char* expected_message) {
  g_boot_script = script;
  assert(lua_init() == 0);
  drive_until_error(expected_stage, expected_message);
  uint32_t foundation_destroyed = g_foundation_destroy_count;
  uint32_t ui_destroyed = g_ui_destroy_count;
  lua_update_task();
  assert(g_foundation_destroy_count == foundation_destroyed);
  assert(g_ui_destroy_count == ui_destroyed);
  assert(lua_shutdown() == 0);
  assert(g_foundation_destroy_count == foundation_destroyed);
  assert(g_ui_destroy_count == ui_destroyed);
}

int main(void) {
  g_boot_script = k_init_failure_script;
  for (uint32_t i = 0u; i < 50u; ++i) run_init_failure_cycle(i);
  assert(g_foundation_create_count == 50u);
  assert(g_foundation_destroy_count == 50u);
  assert(g_ui_create_count == 50u);
  assert(g_ui_destroy_count == 50u);

  run_callback_failure(k_update_failure_script,
                       LUA_RUNTIME_ERROR_STAGE_UPDATE,
                       "intentional update failure");

  g_boot_script = k_input_failure_script;
  assert(lua_init() == 0);
  g_tick += 20u;
  lua_update_task();
  LuaInputAction action = {.event = "pressed", .pressed = true};
  assert(lua_post_input("confirm", &action) == 0);
  drive_until_error(LUA_RUNTIME_ERROR_STAGE_INPUT,
                    "intentional input failure");
  assert(lua_shutdown() == 0);

  g_boot_script = k_message_failure_script;
  assert(lua_init() == 0);
  g_tick += 20u;
  lua_update_task();
  assert(lua_post_message("test-message", "host-test") == 0);
  drive_until_error(LUA_RUNTIME_ERROR_STAGE_MESSAGE,
                    "intentional message failure");
  assert(lua_shutdown() == 0);

  g_boot_script = k_final_failure_script;
  assert(lua_init() == 0);
  g_tick += 20u;
  lua_update_task();
  assert(lua_shutdown() == 0);
  LuaRuntimeErrorInfo final_error;
  assert(lua_vm_get_runtime_error(&final_error));
  assert(final_error.stage == LUA_RUNTIME_ERROR_STAGE_FINAL);
  assert(strstr(final_error.message, "intentional final failure") != NULL);
  assert(!g_foundation_active && !g_ui_active);

  g_boot_script = k_succeeding_script;
  g_tick += 10u;
  assert(lua_init() == 0);
  lua_update_task();
  assert(!lua_vm_get_runtime_error(NULL));
  assert(g_foundation_active && g_ui_active);
  lua_update_task();
  assert(lua_shutdown() == 0);
  assert(!g_foundation_active && !g_ui_active);
  assert(g_foundation_create_count == g_foundation_destroy_count);
  assert(g_ui_create_count == g_ui_destroy_count);

  puts("lua_vm_lifecycle_test: ok");
  return 0;
}
