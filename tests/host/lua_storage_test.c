#ifdef NDEBUG
#undef NDEBUG
#endif

#include <assert.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cart_io_service.h"
#include "cart_storage_format.h"
#include "lauxlib.h"
#include "lua.h"
#include "lua_foundation.h"

int luaopen_storage(lua_State* L);
bool lua_storage_owner_create(lua_State* L, uint32_t id, uint32_t generation,
                              uint64_t cart_id);
void lua_storage_owner_destroy(lua_State* L, uint32_t id, uint32_t generation);
bool lua_storage_handle_io_completion(const cart_io_completion_t* completion);

#define TEST_OWNER_ID 7u
#define TEST_GENERATION 11u
#define TEST_CART_ID UINT64_C(0x1122334455667788)

static lua_State* g_vm;
static cart_io_request_t g_request;
static bool g_has_request;
static bool g_submit_succeeds = true;
static bool g_alloc_fails;
static uint32_t g_next_request_id = 1u;
static uint8_t g_disk_payload[16u * 1024u];
static uint32_t g_disk_payload_size;
static uint16_t g_disk_entry_count;

void* pvPortMalloc(size_t size) {
  return g_alloc_fails ? NULL : malloc(size);
}

void vPortFree(void* pointer) {
  free(pointer);
}

lua_State* lua_foundation_main_thread(lua_State* L) {
  return L;
}

bool lua_foundation_current(lua_State* L,
                            lua_foundation_owner_view_t* owner) {
  if (L != g_vm) return false;
  if (owner != NULL) {
    owner->vm = g_vm;
    owner->owner_id = TEST_OWNER_ID;
    owner->generation = TEST_GENERATION;
    owner->cart_id = TEST_CART_ID;
    owner->app_id = "storage-test";
  }
  return true;
}

uint32_t CartIoService_NextRequestId(void) {
  return g_next_request_id++;
}

bool CartIoService_Submit(const cart_io_request_t* request,
                          uint32_t timeout_ms) {
  (void)timeout_ms;
  if (!g_submit_succeeds || request == NULL || g_has_request) return false;
  g_request = *request;
  g_has_request = true;
  return true;
}

bool CartIoService_CancelOwner(uint32_t owner_id) {
  return owner_id == TEST_OWNER_ID;
}

void CartTaskBuffer_Release(cart_task_buffer_t* buffer) {
  if (buffer == NULL || buffer->data == NULL) return;
  if (buffer->source == CART_BUFFER_SOURCE_RTOS_HEAP) free(buffer->data);
  memset(buffer, 0, sizeof(*buffer));
}

uint32_t CRC32_IEEE_Calculate(const void* data, uint32_t size) {
  const uint8_t* bytes = (const uint8_t*)data;
  uint32_t crc = UINT32_MAX;
  for (uint32_t i = 0u; i < size; ++i) {
    crc ^= bytes[i];
    for (uint32_t bit = 0u; bit < 8u; ++bit) {
      crc = (crc >> 1u) ^
            (UINT32_C(0xEDB88320) & (uint32_t)-(int32_t)(crc & 1u));
    }
  }
  return crc ^ UINT32_MAX;
}

static void complete_load(cart_io_status_t status, const uint8_t* payload,
                          uint32_t payload_size, uint16_t entry_count) {
  assert(g_has_request && g_request.operation == CART_IO_OP_STORAGE_LOAD);
  cart_io_completion_t completion = {
      .request_id = g_request.request_id,
      .owner_id = g_request.owner_id,
      .operation = CART_IO_OP_STORAGE_LOAD,
      .status = status,
  };
  completion.result.storage.buffer = g_request.params.storage.payload;
  completion.result.storage.entry_count = entry_count;
  if (status == CART_IO_STATUS_OK) {
    assert(payload_size <= completion.result.storage.buffer.capacity);
    memcpy(completion.result.storage.buffer.data, payload, payload_size);
    completion.result.storage.buffer.length = payload_size;
  }
  g_has_request = false;
  assert(lua_storage_handle_io_completion(&completion));
}

static void complete_commit(void) {
  assert(g_has_request && g_request.operation == CART_IO_OP_STORAGE_COMMIT);
  assert(g_request.params.storage.payload.length <= sizeof(g_disk_payload));
  g_disk_payload_size = g_request.params.storage.payload.length;
  g_disk_entry_count = g_request.params.storage.entry_count;
  memcpy(g_disk_payload, g_request.params.storage.payload.data,
         g_disk_payload_size);

  cart_io_completion_t completion = {
      .request_id = g_request.request_id,
      .owner_id = g_request.owner_id,
      .operation = CART_IO_OP_STORAGE_COMMIT,
      .status = CART_IO_STATUS_OK,
  };
  completion.result.storage.buffer = g_request.params.storage.payload;
  g_has_request = false;
  assert(lua_storage_handle_io_completion(&completion));
}

static void create_owner_with_payload(const uint8_t* payload,
                                      uint32_t payload_size,
                                      uint16_t entry_count) {
  assert(lua_storage_owner_create(g_vm, TEST_OWNER_ID, TEST_GENERATION,
                                  TEST_CART_ID));
  complete_load(payload == NULL ? CART_IO_STATUS_NOT_FOUND : CART_IO_STATUS_OK,
                payload, payload_size, entry_count);
}

static int push_storage_function(const char* name) {
  int base = lua_gettop(g_vm);
  lua_getglobal(g_vm, "storage");
  lua_getfield(g_vm, -1, name);
  lua_remove(g_vm, -2);
  assert(lua_gettop(g_vm) == base + 1);
  return base;
}

static void set_success(const char* key, lua_Integer value) {
  int base = push_storage_function("set");
  lua_pushstring(g_vm, key);
  lua_pushinteger(g_vm, value);
  assert(lua_pcall(g_vm, 2, LUA_MULTRET, 0) == LUA_OK);
  assert(lua_gettop(g_vm) == base + 1 && lua_toboolean(g_vm, -1));
  lua_settop(g_vm, base);
}

static void set_failure(const char* key, lua_Integer value) {
  int base = push_storage_function("set");
  lua_pushstring(g_vm, key);
  lua_pushinteger(g_vm, value);
  assert(lua_pcall(g_vm, 2, LUA_MULTRET, 0) == LUA_OK);
  assert(lua_gettop(g_vm) == base + 2 && lua_isnil(g_vm, -2));
  assert(strcmp(lua_tostring(g_vm, -1),
                "integer value is outside int32 range") == 0);
  lua_settop(g_vm, base);
}

static lua_Integer get_integer(const char* key) {
  int base = push_storage_function("get");
  lua_pushstring(g_vm, key);
  assert(lua_pcall(g_vm, 1, 1, 0) == LUA_OK);
  assert(lua_isinteger(g_vm, -1));
  lua_Integer value = lua_tointeger(g_vm, -1);
  lua_settop(g_vm, base);
  return value;
}

static void commit(void) {
  int base = push_storage_function("commit");
  assert(lua_pcall(g_vm, 0, 1, 0) == LUA_OK && lua_toboolean(g_vm, -1));
  lua_settop(g_vm, base);
  complete_commit();
}

static void commit_queue_full(void) {
  int base = push_storage_function("commit");
  g_submit_succeeds = false;
  assert(lua_pcall(g_vm, 0, LUA_MULTRET, 0) == LUA_OK);
  g_submit_succeeds = true;
  assert(lua_gettop(g_vm) == base + 2 && lua_isnil(g_vm, -2));
  assert(strcmp(lua_tostring(g_vm, -1), "storage commit queue is full") == 0);
  assert(!g_has_request);
  lua_settop(g_vm, base);
}

static void commit_allocation_failure(void) {
  int base = push_storage_function("commit");
  g_alloc_fails = true;
  assert(lua_pcall(g_vm, 0, LUA_MULTRET, 0) == LUA_OK);
  g_alloc_fails = false;
  assert(lua_gettop(g_vm) == base + 2 && lua_isnil(g_vm, -2));
  assert(strcmp(lua_tostring(g_vm, -1),
                "storage commit buffer unavailable") == 0);
  assert(!g_has_request);
  lua_settop(g_vm, base);
}

static void test_integer_boundaries(void) {
  create_owner_with_payload(NULL, 0u, 0u);
  set_success("minimum", INT32_MIN);
  set_success("negative", -1);
  set_success("zero", 0);
  set_success("one", 1);
  set_success("maximum", INT32_MAX);

  set_success("stable", 123);
  set_failure("stable", (lua_Integer)INT32_MIN - 1);
  assert(get_integer("stable") == 123);
  set_failure("stable", (lua_Integer)INT32_MAX + 1);
  assert(get_integer("stable") == 123);
  set_failure("stable", LUA_MININTEGER);
  assert(get_integer("stable") == 123);
  set_failure("stable", LUA_MAXINTEGER);
  assert(get_integer("stable") == 123);

  commit_allocation_failure();
  assert(get_integer("stable") == 123);
  commit_queue_full();
  assert(get_integer("stable") == 123);
  commit();
  lua_storage_owner_destroy(g_vm, TEST_OWNER_ID, TEST_GENERATION);

  create_owner_with_payload(g_disk_payload, g_disk_payload_size,
                            g_disk_entry_count);
  assert(get_integer("minimum") == INT32_MIN);
  assert(get_integer("maximum") == INT32_MAX);
  assert(get_integer("stable") == 123);
  lua_storage_owner_destroy(g_vm, TEST_OWNER_ID, TEST_GENERATION);
}

static void test_pending_load_exit(void) {
  for (uint32_t i = 0u; i < 100u; ++i) {
    assert(lua_storage_owner_create(g_vm, TEST_OWNER_ID, TEST_GENERATION,
                                    TEST_CART_ID));
    assert(g_has_request && g_request.operation == CART_IO_OP_STORAGE_LOAD);
    cart_io_completion_t stale = {
        .request_id = g_request.request_id,
        .owner_id = g_request.owner_id,
        .operation = CART_IO_OP_STORAGE_LOAD,
        .status = CART_IO_STATUS_CANCELLED,
    };
    stale.result.storage.buffer = g_request.params.storage.payload;
    lua_storage_owner_destroy(g_vm, TEST_OWNER_ID, TEST_GENERATION);
    g_has_request = false;
    assert(!lua_storage_handle_io_completion(&stale));
    CartTaskBuffer_Release(&stale.result.storage.buffer);
  }
}

static void test_fixed_encoding_and_crc(void) {
  create_owner_with_payload(NULL, 0u, 0u);
  set_success("value", INT32_C(0x01020304));
  commit();

  const uint8_t expected[] = {
      5u, 2u, 4u, 0u, 'v', 'a', 'l', 'u', 'e', 4u, 3u, 2u, 1u,
  };
  assert(g_disk_payload_size == sizeof(expected));
  assert(memcmp(g_disk_payload, expected, sizeof(expected)) == 0);

  uint8_t header[CART_STORAGE_HEADER_SIZE];
  CartStorageFormat_EncodeHeader(header, g_disk_entry_count, g_disk_payload,
                                 g_disk_payload_size);
  const uint8_t header_prefix[] = {
      0x43u, 0x4Bu, 0x53u, 0x56u, 0x01u, 0x00u, 0x01u, 0x00u,
      0x0Du, 0x00u, 0x00u, 0x00u,
  };
  assert(memcmp(header, header_prefix, sizeof(header_prefix)) == 0);
  cart_storage_metadata_t metadata;
  assert(CartStorageFormat_DecodeHeader(header, sizeof(g_disk_payload),
                                        &metadata));
  assert(metadata.entry_count == 1u);
  assert(metadata.payload_size == sizeof(expected));
  assert(CartStorageFormat_VerifyPayload(&metadata, g_disk_payload,
                                         g_disk_payload_size));
  header[0] ^= 1u;
  assert(!CartStorageFormat_DecodeHeader(header, sizeof(g_disk_payload),
                                         &metadata));
  header[0] ^= 1u;
  g_disk_payload[0] ^= 1u;
  assert(!CartStorageFormat_VerifyPayload(&metadata, g_disk_payload,
                                          g_disk_payload_size));
  g_disk_payload[0] ^= 1u;
  lua_storage_owner_destroy(g_vm, TEST_OWNER_ID, TEST_GENERATION);
}

int main(void) {
  g_vm = luaL_newstate();
  assert(g_vm != NULL);
  luaL_requiref(g_vm, "storage", luaopen_storage, 1);
  lua_pop(g_vm, 1);

  test_integer_boundaries();
  test_fixed_encoding_and_crc();
  test_pending_load_exit();

  assert(!g_has_request);
  lua_close(g_vm);
  puts("lua_storage_test: ok");
  return 0;
}
