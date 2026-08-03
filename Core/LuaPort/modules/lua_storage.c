#include "lua.h"
#include "lauxlib.h"

#include <stdbool.h>
#include <stdint.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "cart_io_service.h"
#include "lua_foundation.h"

#define LUA_STORAGE_MAX_OWNERS 4u
#define LUA_STORAGE_KEY_MAX 64u
#define LUA_STORAGE_STRING_MAX 4096u
#define LUA_STORAGE_QUOTA (16u * 1024u)
#define LUA_STORAGE_MAX_ENTRIES 128u

typedef enum { VALUE_BOOL = 1, VALUE_INTEGER = 2, VALUE_NUMBER = 3,
               VALUE_STRING = 4 } value_type_t;

typedef struct {
  lua_State* vm;
  uint32_t owner_id;
  uint32_t generation;
  uint64_t cart_id;
  uint8_t* data;
  uint32_t used;
  uint16_t count;
  int data_ref;
  bool loaded;
  bool dirty;
  bool load_pending;
  bool commit_pending;
  uint32_t load_request_id;
  uint32_t commit_request_id;
  uint32_t revision;
  uint32_t commit_revision;
  bool active;
} storage_owner_t;

static storage_owner_t g_owners[LUA_STORAGE_MAX_OWNERS];

static uint32_t load_u32_le(const uint8_t* bytes) {
  return (uint32_t)bytes[0] |
         ((uint32_t)bytes[1] << 8u) |
         ((uint32_t)bytes[2] << 16u) |
         ((uint32_t)bytes[3] << 24u);
}

static uint64_t load_u64_le(const uint8_t* bytes) {
  return (uint64_t)load_u32_le(bytes) |
         ((uint64_t)load_u32_le(bytes + 4u) << 32u);
}

static void store_u32_le(uint8_t* bytes, uint32_t value) {
  bytes[0] = (uint8_t)value;
  bytes[1] = (uint8_t)(value >> 8u);
  bytes[2] = (uint8_t)(value >> 16u);
  bytes[3] = (uint8_t)(value >> 24u);
}

static void store_u64_le(uint8_t* bytes, uint64_t value) {
  store_u32_le(bytes, (uint32_t)value);
  store_u32_le(bytes + 4u, (uint32_t)(value >> 32u));
}

static int fail(lua_State* L, const char* message) {
  lua_pushnil(L); lua_pushstring(L, message); return 2;
}

static storage_owner_t* find_owner(lua_State* L, uint32_t id, uint32_t gen) {
  L = lua_foundation_main_thread(L);
  for (size_t i = 0; i < LUA_STORAGE_MAX_OWNERS; ++i) {
    if (g_owners[i].active && g_owners[i].vm == L &&
        g_owners[i].owner_id == id && g_owners[i].generation == gen)
      return &g_owners[i];
  }
  return NULL;
}

bool lua_storage_owner_create(lua_State* L, uint32_t id, uint32_t gen,
                              uint64_t cart_id) {
  L = lua_foundation_main_thread(L);
  if (!L || find_owner(L, id, gen)) return false;
  for (size_t i = 0; i < LUA_STORAGE_MAX_OWNERS; ++i) {
    if (!g_owners[i].active) {
      memset(&g_owners[i], 0, sizeof(g_owners[i]));
      g_owners[i].vm = L; g_owners[i].owner_id = id;
      g_owners[i].generation = gen; g_owners[i].cart_id = cart_id;
      g_owners[i].data_ref = LUA_NOREF; g_owners[i].active = true;
      if (cart_id == 0u) {
        g_owners[i].loaded = true;
        return true;
      }
      cart_task_buffer_t load_buffer = {
        .data = pvPortMalloc(LUA_STORAGE_QUOTA),
        .capacity = LUA_STORAGE_QUOTA,
        .owner_id = id,
        .source = CART_BUFFER_SOURCE_RTOS_HEAP,
      };
      if (!load_buffer.data) {
        memset(&g_owners[i], 0, sizeof(g_owners[i]));
        return false;
      }
      cart_io_request_t request = {
        .request_id = CartIoService_NextRequestId(),
        .owner_id = id,
        .operation = CART_IO_OP_STORAGE_LOAD,
      };
      request.params.storage.cart_id = cart_id;
      request.params.storage.payload = load_buffer;
      if (!CartIoService_Submit(&request, CART_IO_TIMEOUT_SD_READ_MS)) {
        CartTaskBuffer_Release(&load_buffer);
        memset(&g_owners[i], 0, sizeof(g_owners[i]));
        return false;
      }
      g_owners[i].load_pending = true;
      g_owners[i].load_request_id = request.request_id;
      return true;
    }
  }
  return false;
}

void lua_storage_owner_destroy(lua_State* L, uint32_t id, uint32_t gen) {
  storage_owner_t* owner = find_owner(L, id, gen);
  if (!owner) return;
  (void)CartIoService_CancelOwner(id);
  if (owner->data_ref != LUA_NOREF)
    luaL_unref(owner->vm, LUA_REGISTRYINDEX, owner->data_ref);
  memset(owner, 0, sizeof(*owner));
}

static storage_owner_t* current_owner(lua_State* L, const char** error) {
  lua_foundation_owner_view_t current;
  if (!lua_foundation_current(L, &current)) {
    *error = "storage requires an active application owner"; return NULL;
  }
  storage_owner_t* owner = find_owner(L, current.owner_id, current.generation);
  if (!owner) *error = "storage owner is unavailable";
  else if (owner->cart_id == 0u) {
    *error = "storage requires a Cart application";
    owner = NULL;
  }
  return owner;
}

static bool validate_payload(storage_owner_t* owner) {
  uint32_t offset = 0u; uint16_t count = 0u;
  while (offset < owner->used) {
    if (owner->used - offset < 4u) return false;
    uint8_t key_len = owner->data[offset];
    uint8_t type = owner->data[offset + 1u];
    uint16_t value_len = (uint16_t)owner->data[offset + 2u] |
                         ((uint16_t)owner->data[offset + 3u] << 8);
    uint32_t size = 4u + key_len + value_len;
    if (key_len == 0u || key_len > LUA_STORAGE_KEY_MAX ||
        type < VALUE_BOOL || type > VALUE_STRING || size > owner->used - offset ||
        (type == VALUE_BOOL && value_len != 1u) ||
        (type == VALUE_INTEGER && value_len != 4u) ||
        (type == VALUE_NUMBER && value_len != 8u) ||
        (type == VALUE_STRING && value_len > LUA_STORAGE_STRING_MAX)) return false;
    offset += size;
    if (++count > LUA_STORAGE_MAX_ENTRIES) return false;
  }
  owner->count = count;
  return offset == owner->used;
}

static bool ensure_buffer(lua_State* L, storage_owner_t* owner) {
  if (owner->data) return true;
  owner->data = (uint8_t*)lua_newuserdatauv(L, LUA_STORAGE_QUOTA, 0);
  if (!owner->data) return false;
  memset(owner->data, 0, LUA_STORAGE_QUOTA);
  owner->data_ref = luaL_ref(L, LUA_REGISTRYINDEX);
  return true;
}

static bool load_owner(lua_State* L, storage_owner_t* owner, const char** error) {
  if (owner->load_pending) { *error = "storage load pending"; return false; }
  if (!owner->loaded) { *error = "storage load failed"; return false; }
  if (!ensure_buffer(L, owner)) { *error = "storage buffer allocation failed"; return false; }
  return true;
}

static bool read_key(lua_State* L, int index, const char** key, size_t* length,
                     const char** error) {
  if (lua_type(L, index) != LUA_TSTRING) { *error = "storage key must be a string"; return false; }
  *key = lua_tolstring(L, index, length);
  if (*length == 0u || *length > LUA_STORAGE_KEY_MAX) {
    *error = "storage key must be 1..64 bytes"; return false;
  }
  return true;
}

static bool find_entry(storage_owner_t* owner, const char* key, size_t key_len,
                       uint32_t* out_offset, uint32_t* out_size) {
  uint32_t offset = 0u;
  while (offset < owner->used) {
    uint8_t stored_key_len = owner->data[offset];
    uint16_t value_len = (uint16_t)owner->data[offset + 2u] |
                         ((uint16_t)owner->data[offset + 3u] << 8);
    uint32_t size = 4u + stored_key_len + value_len;
    if (stored_key_len == key_len && memcmp(owner->data + offset + 4u, key, key_len) == 0) {
      if (out_offset) *out_offset = offset;
      if (out_size) *out_size = size;
      return true;
    }
    offset += size;
  }
  return false;
}

static int push_value(lua_State* L, const uint8_t* entry) {
  uint8_t key_len = entry[0], type = entry[1];
  uint16_t length = (uint16_t)entry[2] | ((uint16_t)entry[3] << 8);
  const uint8_t* value = entry + 4u + key_len;
  if (type == VALUE_BOOL) lua_pushboolean(L, value[0] != 0u);
  else if (type == VALUE_INTEGER) {
    uint32_t bits = load_u32_le(value);
    int32_t v;
    memcpy(&v, &bits, sizeof(v));
    lua_pushinteger(L, v);
  } else if (type == VALUE_NUMBER) {
    uint64_t bits = load_u64_le(value);
    double v;
    memcpy(&v, &bits, sizeof(v));
    lua_pushnumber(L, v);
  }
  else lua_pushlstring(L, (const char*)value, length);
  return 1;
}

static int l_has(lua_State* L) {
  const char *error = NULL, *key; size_t key_len;
  storage_owner_t* owner = current_owner(L, &error);
  if (!owner || lua_gettop(L) != 1 || !read_key(L, 1, &key, &key_len, &error) ||
      !load_owner(L, owner, &error)) return fail(L, error);
  lua_pushboolean(L, find_entry(owner, key, key_len, NULL, NULL)); return 1;
}

static int l_get(lua_State* L) {
  const char *error = NULL, *key; size_t key_len; int args = lua_gettop(L);
  storage_owner_t* owner = current_owner(L, &error);
  if (!owner || args < 1 || args > 2 || !read_key(L, 1, &key, &key_len, &error) ||
      !load_owner(L, owner, &error)) return fail(L, error);
  uint32_t offset;
  if (find_entry(owner, key, key_len, &offset, NULL)) return push_value(L, owner->data + offset);
  if (args == 2) lua_pushvalue(L, 2); else lua_pushnil(L);
  return 1;
}

static int l_set(lua_State* L) {
  const char *error = NULL, *key; size_t key_len;
  storage_owner_t* owner = current_owner(L, &error);
  if (!owner || lua_gettop(L) != 2 || !read_key(L, 1, &key, &key_len, &error) ||
      !load_owner(L, owner, &error)) return fail(L, error);
  uint8_t type; uint8_t value[8]; const uint8_t* value_ptr = value; uint16_t value_len;
  if (lua_isboolean(L, 2)) { type = VALUE_BOOL; value[0] = lua_toboolean(L, 2); value_len = 1u; }
  else if (lua_isinteger(L, 2)) {
    type = VALUE_INTEGER;
    int32_t v = (int32_t)lua_tointeger(L, 2);
    uint32_t bits;
    memcpy(&bits, &v, sizeof(bits));
    store_u32_le(value, bits);
    value_len = 4u;
  } else if (lua_isnumber(L, 2)) {
    type = VALUE_NUMBER;
    double v = lua_tonumber(L, 2);
    if (!isfinite(v)) return fail(L, "storage number must be finite");
    uint64_t bits;
    memcpy(&bits, &v, sizeof(bits));
    store_u64_le(value, bits);
    value_len = 8u;
  }
  else if (lua_type(L, 2) == LUA_TSTRING) { size_t n; value_ptr = (const uint8_t*)lua_tolstring(L, 2, &n); if (n > LUA_STORAGE_STRING_MAX) return fail(L, "storage string exceeds 4 KiB"); value_len = (uint16_t)n; type = VALUE_STRING; }
  else return fail(L, "storage value must be boolean, integer, number, or string");
  uint32_t old_offset = 0u, old_size = 0u;
  bool exists = find_entry(owner, key, key_len, &old_offset, &old_size);
  uint32_t new_size = 4u + (uint32_t)key_len + value_len;
  uint32_t base_used = owner->used - (exists ? old_size : 0u);
  if (base_used + new_size > LUA_STORAGE_QUOTA) return fail(L, "storage quota exceeded");
  if (!exists && owner->count >= LUA_STORAGE_MAX_ENTRIES) return fail(L, "storage key limit reached");
  if (exists) memmove(owner->data + old_offset, owner->data + old_offset + old_size,
                      owner->used - old_offset - old_size);
  uint32_t offset = base_used;
  owner->data[offset] = (uint8_t)key_len; owner->data[offset + 1u] = type;
  owner->data[offset + 2u] = (uint8_t)value_len;
  owner->data[offset + 3u] = (uint8_t)(value_len >> 8);
  memcpy(owner->data + offset + 4u, key, key_len);
  memcpy(owner->data + offset + 4u + key_len, value_ptr, value_len);
  owner->used = base_used + new_size; if (!exists) ++owner->count;
  ++owner->revision;
  owner->dirty = true; lua_pushboolean(L, 1); return 1;
}

static int l_remove(lua_State* L) {
  const char *error = NULL, *key; size_t key_len; uint32_t offset, size;
  storage_owner_t* owner = current_owner(L, &error);
  if (!owner || lua_gettop(L) != 1 || !read_key(L, 1, &key, &key_len, &error) ||
      !load_owner(L, owner, &error)) return fail(L, error);
  if (find_entry(owner, key, key_len, &offset, &size)) {
    memmove(owner->data + offset, owner->data + offset + size,
            owner->used - offset - size); owner->used -= size; --owner->count;
    owner->dirty = true; ++owner->revision;
  }
  lua_pushboolean(L, 1); return 1;
}

static int l_clear(lua_State* L) {
  const char* error = NULL; storage_owner_t* owner = current_owner(L, &error);
  if (!owner) return fail(L, error);
  if (lua_gettop(L) != 0) return fail(L, "storage.clear expects no arguments");
  if (!ensure_buffer(L, owner)) return fail(L, "storage buffer allocation failed");
  owner->used = 0u; owner->count = 0u; owner->loaded = true; owner->dirty = true;
  ++owner->revision;
  lua_pushboolean(L, 1); return 1;
}

static int l_commit(lua_State* L) {
  const char* error = NULL; storage_owner_t* owner = current_owner(L, &error);
  if (!owner || lua_gettop(L) != 0 || !load_owner(L, owner, &error)) return fail(L, error);
  if (!owner->dirty) { lua_pushboolean(L, 1); return 1; }
  if (owner->commit_pending) { lua_pushboolean(L, 1); return 1; }
  size_t allocation_size = owner->used == 0u ? 1u : owner->used;
  cart_task_buffer_t payload = {
    .data = pvPortMalloc(allocation_size),
    .capacity = (uint32_t)allocation_size,
    .length = owner->used,
    .owner_id = owner->owner_id,
    .source = CART_BUFFER_SOURCE_RTOS_HEAP,
  };
  if (!payload.data) return fail(L, "storage commit buffer unavailable");
  if (owner->used > 0u) memcpy(payload.data, owner->data, owner->used);
  cart_io_request_t request = {
    .request_id = CartIoService_NextRequestId(),
    .owner_id = owner->owner_id,
    .operation = CART_IO_OP_STORAGE_COMMIT,
  };
  request.params.storage.cart_id = owner->cart_id;
  request.params.storage.entry_count = owner->count;
  request.params.storage.payload = payload;
  if (!CartIoService_Submit(&request, CART_IO_TIMEOUT_LFS_COMMIT_MS)) {
    CartTaskBuffer_Release(&payload);
    return fail(L, "storage commit queue is full");
  }
  owner->commit_pending = true;
  owner->commit_request_id = request.request_id;
  owner->commit_revision = owner->revision;
  lua_pushboolean(L, 1); return 1;
}

bool lua_storage_handle_io_completion(const cart_io_completion_t* completion) {
  if (!completion || (completion->operation != CART_IO_OP_STORAGE_LOAD &&
      completion->operation != CART_IO_OP_STORAGE_COMMIT &&
      completion->operation != CART_IO_OP_STORAGE_CLEAR)) return false;

  storage_owner_t* owner = NULL;
  for (size_t i = 0u; i < LUA_STORAGE_MAX_OWNERS; ++i) {
    if (g_owners[i].active && g_owners[i].owner_id == completion->owner_id) {
      owner = &g_owners[i]; break;
    }
  }
  cart_task_buffer_t buffer = completion->result.storage.buffer;
  if (!owner) {
    return false;
  }
  if (completion->operation == CART_IO_OP_STORAGE_LOAD &&
      completion->request_id == owner->load_request_id) {
    owner->load_pending = false;
    owner->used = 0u; owner->count = 0u;
    if ((completion->status == CART_IO_STATUS_OK ||
         completion->status == CART_IO_STATUS_NOT_FOUND) &&
        ensure_buffer(owner->vm, owner)) {
      if (completion->status == CART_IO_STATUS_OK && buffer.length <= LUA_STORAGE_QUOTA) {
        uint16_t expected_count = completion->result.storage.entry_count;
        memcpy(owner->data, buffer.data, buffer.length);
        owner->used = buffer.length;
        owner->count = expected_count;
        if (!validate_payload(owner) || owner->count != expected_count) {
          owner->used = 0u; owner->count = 0u;
        }
      }
      owner->loaded = true;
    }
    CartTaskBuffer_Release(&buffer);
    return true;
  }
  if (completion->operation == CART_IO_OP_STORAGE_COMMIT &&
      completion->request_id == owner->commit_request_id) {
    owner->commit_pending = false;
    if (completion->status == CART_IO_STATUS_OK &&
        owner->revision == owner->commit_revision) owner->dirty = false;
    CartTaskBuffer_Release(&buffer);
    return true;
  }
  return false;
}

bool lua_storage_all_ready(void) {
  for (size_t i = 0u; i < LUA_STORAGE_MAX_OWNERS; ++i) {
    if (g_owners[i].active && g_owners[i].load_pending) return false;
  }
  return true;
}

int luaopen_storage(lua_State* L) {
  static const luaL_Reg functions[] = {{"has", l_has}, {"get", l_get},
      {"set", l_set}, {"remove", l_remove}, {"commit", l_commit},
      {"clear", l_clear}, {NULL, NULL}};
  luaL_newlib(L, functions); return 1;
}
