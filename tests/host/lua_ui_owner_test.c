#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lauxlib.h"
#include "lua_ui.h"
#include "lua_vm.h"

static lv_obj_t s_screen;
static unsigned s_delete_count;
static unsigned s_cleanup_count;
static bool s_fail_next_create;
static uint32_t s_input_owner;
static uint32_t s_input_generation;
static char s_input_id[LUA_INPUT_ACTION_ID_MAX];

lv_obj_t* lv_screen_active(void) {
  return &s_screen;
}

lv_obj_t* lv_obj_create(lv_obj_t* parent) {
  if (s_fail_next_create) {
    s_fail_next_create = false;
    return NULL;
  }
  lv_obj_t* object = (lv_obj_t*)calloc(1u, sizeof(*object));
  assert(object != NULL);
  object->parent = parent;
  if (parent) {
    object->next_sibling = parent->first_child;
    parent->first_child = object;
  }
  return object;
}

lv_obj_t* lv_label_create(lv_obj_t* parent) { return lv_obj_create(parent); }
lv_obj_t* lv_button_create(lv_obj_t* parent) { return lv_obj_create(parent); }

static void detach_object(lv_obj_t* object) {
  if (!object || !object->parent) return;
  lv_obj_t** cursor = &object->parent->first_child;
  while (*cursor) {
    if (*cursor == object) {
      *cursor = object->next_sibling;
      break;
    }
    cursor = &(*cursor)->next_sibling;
  }
}

void lv_obj_delete(lv_obj_t* object) {
  if (!object || object->deleted) return;
  while (object->first_child) lv_obj_delete(object->first_child);
  object->deleted = true;
  for (lv_event_dsc_t* descriptor = object->events;
       descriptor; descriptor = descriptor->next) {
    if (descriptor->filter == LV_EVENT_DELETE || descriptor->filter == LV_EVENT_ALL) {
      lv_event_t event = {
          .code = LV_EVENT_DELETE,
          .current_target = object,
          .user_data = descriptor->user_data,
      };
      descriptor->callback(&event);
    }
  }
  detach_object(object);
  while (object->events) {
    lv_event_dsc_t* next = object->events->next;
    free(object->events);
    object->events = next;
  }
  ++s_delete_count;
  free(object);
}

void lv_obj_remove_style_all(lv_obj_t* object) { (void)object; }
void lv_obj_set_pos(lv_obj_t* object, int32_t x, int32_t y) {
  object->x = x; object->y = y;
}
void lv_obj_set_size(lv_obj_t* object, int32_t width, int32_t height) {
  object->width = width; object->height = height;
}
void lv_obj_remove_flag(lv_obj_t* object, uint32_t flags) {
  (void)object; (void)flags;
}
void lv_obj_add_flag(lv_obj_t* object, uint32_t flags) {
  (void)object; (void)flags;
}
void lv_obj_center(lv_obj_t* object) { (void)object; }
void lv_label_set_text(lv_obj_t* object, const char* text) {
  (void)snprintf(object->text, sizeof(object->text), "%s", text ? text : "");
}
void lv_obj_set_style_bg_color(lv_obj_t* object, lv_color_t color, int32_t selector) {
  (void)object; (void)color; (void)selector;
}
void lv_obj_set_style_bg_opa(lv_obj_t* object, lv_opa_t opacity, int32_t selector) {
  (void)object; (void)opacity; (void)selector;
}
void lv_obj_set_style_text_color(lv_obj_t* object, lv_color_t color, int32_t selector) {
  (void)object; (void)color; (void)selector;
}
void lv_obj_set_style_radius(lv_obj_t* object, int32_t radius, int32_t selector) {
  (void)object; (void)radius; (void)selector;
}
void lv_obj_set_style_border_color(lv_obj_t* object, lv_color_t color, int32_t selector) {
  (void)object; (void)color; (void)selector;
}
void lv_obj_set_style_border_width(lv_obj_t* object, int32_t width, int32_t selector) {
  (void)object; (void)width; (void)selector;
}
void lv_obj_set_style_opa(lv_obj_t* object, lv_opa_t opacity, int32_t selector) {
  (void)object; (void)opacity; (void)selector;
}
void lv_obj_add_state(lv_obj_t* object, uint32_t state) {
  (void)object; (void)state;
}
void lv_obj_remove_state(lv_obj_t* object, uint32_t state) {
  (void)object; (void)state;
}
lv_color_t lv_color_hex(uint32_t color) { return color; }

lv_event_dsc_t* lv_obj_add_event_cb(lv_obj_t* object,
                                    lv_event_cb_t callback,
                                    lv_event_code_t filter,
                                    void* user_data) {
  lv_event_dsc_t* descriptor =
      (lv_event_dsc_t*)calloc(1u, sizeof(*descriptor));
  assert(descriptor != NULL);
  descriptor->callback = callback;
  descriptor->filter = filter;
  descriptor->user_data = user_data;
  descriptor->next = object->events;
  object->events = descriptor;
  return descriptor;
}

void* lv_event_get_user_data(lv_event_t* event) {
  return event->user_data;
}
lv_event_code_t lv_event_get_code(lv_event_t* event) { return event->code; }

static void send_event(lv_obj_t* object, lv_event_code_t code) {
  for (lv_event_dsc_t* descriptor = object->events;
       descriptor; descriptor = descriptor->next) {
    if (descriptor->filter == code || descriptor->filter == LV_EVENT_ALL) {
      lv_event_t event = {
          .code = code,
          .current_target = object,
          .user_data = descriptor->user_data,
      };
      descriptor->callback(&event);
    }
  }
}

bool lua_ui_image_patch(lua_State* L, lua_ui_handle_t* handle,
                        int properties_idx, char* error, size_t error_size) {
  (void)L; (void)handle; (void)properties_idx; (void)error; (void)error_size;
  return true;
}

int lua_post_input_for_owner(uint32_t owner_id,
                             uint32_t generation,
                             const char* action_id,
                             const LuaInputAction* action) {
  (void)action;
  s_input_owner = owner_id;
  s_input_generation = generation;
  (void)snprintf(s_input_id, sizeof(s_input_id), "%s", action_id);
  return 0;
}

static void count_cleanup(lua_ui_handle_t* handle) {
  (void)handle;
  ++s_cleanup_count;
}

static lua_ui_handle_t* create_registered_handle(lua_State* L,
                                                 lua_ui_object_type_t type) {
  lv_obj_t* root = lua_ui_owner_root(L);
  assert(root != NULL);
  lua_ui_handle_t* handle = lua_ui_handle_new(L, sizeof(*handle), type);
  assert(handle != NULL);
  int handle_idx = lua_gettop(L);
  lv_obj_t* object = lv_obj_create(root);
  assert(lua_ui_handle_register(L, handle_idx, handle, object, count_cleanup));
  return handle;
}

int main(void) {
  lua_State* L = luaL_newstate();
  assert(L != NULL);
  lua_ui_registry_init();

  assert(lua_ui_owner_create(L, 1u, 10u));
  assert(lua_ui_owner_create(L, 2u, 20u));
  lua_ui_owner_enter(L, 1u, 10u);

  assert(lua_ui_root(L) == 1);
  lua_ui_handle_t* root_handle = lua_ui_handle_test(L, -1);
  assert(root_handle && root_handle->object_type == LUA_UI_OBJECT_ROOT);
  assert(lua_ui_delete(L) == 2);
  assert(lua_isnil(L, -2));
  assert(strstr(lua_tostring(L, -1), "cannot be deleted") != NULL);
  lua_settop(L, 0);

  luaopen_ui_container(L);
  lua_newtable(L);
  lua_call(L, 1, 1);
  lua_ui_handle_t* container = lua_ui_handle_test(L, -1);
  assert(container && container->object_type == LUA_UI_OBJECT_CONTAINER);
  assert(lua_ui_delete(L) == 1 && lua_toboolean(L, -1));
  assert(!container->alive);
  lua_settop(L, 0);

  luaopen_ui_label(L);
  lua_newtable(L);
  lua_pushliteral(L, "title");
  lua_setfield(L, -2, "id");
  lua_pushliteral(L, "Hello");
  lua_setfield(L, -2, "text");
  lua_call(L, 1, 1);
  lua_ui_handle_t* actual_label = lua_ui_handle_test(L, -1);
  assert(actual_label != NULL && actual_label->object_type == LUA_UI_OBJECT_LABEL);
  assert(strcmp(actual_label->object->text, "Hello") == 0);
  lua_newtable(L);
  lua_pushliteral(L, "Updated");
  lua_setfield(L, -2, "text");
  assert(lua_ui_patch(L) == 1);
  assert(lua_toboolean(L, -1));
  assert(strcmp(actual_label->object->text, "Updated") == 0);
  lua_settop(L, 0);

  luaopen_ui_button(L);
  lua_newtable(L);
  lua_pushliteral(L, "run");
  lua_setfield(L, -2, "id");
  lua_pushliteral(L, "Run");
  lua_setfield(L, -2, "text");
  lua_pushliteral(L, "run_action");
  lua_setfield(L, -2, "input");
  lua_call(L, 1, 1);
  lua_ui_handle_t* actual_button = lua_ui_handle_test(L, -1);
  assert(actual_button != NULL && actual_button->object_type == LUA_UI_OBJECT_BUTTON);
  send_event(actual_button->object, LV_EVENT_CLICKED);
  assert(s_input_owner == 1u && s_input_generation == 10u);
  assert(strcmp(s_input_id, "run_action") == 0);
  lua_settop(L, 0);

  lua_ui_handle_t* deleted_handle =
      create_registered_handle(L, LUA_UI_OBJECT_LABEL);
  lv_obj_t* deleted_object = deleted_handle->object;
  lua_ui_owner_leave();

  char error[LUA_UI_ERROR_MAX];
  lua_ui_owner_enter(L, 2u, 20u);
  assert(lua_ui_handle_validate(L, -1, error, sizeof(error)) == NULL);
  assert(strstr(error, "another application") != NULL);
  lua_newtable(L);
  assert(lua_ui_patch(L) == 2);
  assert(lua_isnil(L, -2));
  assert(strstr(lua_tostring(L, -1), "another application") != NULL);
  lua_settop(L, 1);
  lua_ui_owner_leave();

  lua_ui_owner_enter(L, 1u, 10u);
  assert(lua_ui_handle_validate(L, -1, error, sizeof(error)) == deleted_handle);
  lua_newtable(L);
  assert(lua_ui_patch(L) == 1);
  assert(lua_toboolean(L, -1));
  lua_settop(L, 1);
  lua_newtable(L);
  lua_pushboolean(L, 1);
  lua_setfield(L, -2, "unknown");
  assert(lua_ui_patch(L) == 2);
  assert(lua_isnil(L, -2));
  assert(strstr(lua_tostring(L, -1), "unsupported") != NULL);
  lua_settop(L, 1);
  lua_newtable(L);
  lua_pushliteral(L, "yes");
  lua_setfield(L, -2, "hidden");
  assert(lua_ui_patch(L) == 2);
  assert(lua_isnil(L, -2));
  assert(strstr(lua_tostring(L, -1), "expects boolean") != NULL);
  lua_settop(L, 1);
  lv_obj_delete(deleted_object);
  assert(!deleted_handle->alive && deleted_handle->object == NULL);
  assert(lua_ui_handle_validate(L, -1, error, sizeof(error)) == NULL);
  assert(strstr(error, "deleted") != NULL);
  lua_newtable(L);
  assert(lua_ui_patch(L) == 2);
  assert(lua_isnil(L, -2));
  assert(strstr(lua_tostring(L, -1), "deleted") != NULL);
  lua_settop(L, 1);
  lua_ui_owner_leave();
  assert(s_cleanup_count == 1u);

  lua_ui_owner_enter(L, 1u, 10u);
  lua_ui_handle_t* parent_deleted_handle =
      create_registered_handle(L, LUA_UI_OBJECT_IMAGE);
  lua_ui_owner_leave();
  lua_settop(L, 0);
  lua_ui_owner_destroy(L, 1u, 10u);
  assert(!parent_deleted_handle->alive && parent_deleted_handle->object == NULL);
  assert(!actual_label->alive && actual_label->object == NULL);
  assert(!actual_button->alive && actual_button->object == NULL);
  assert(s_cleanup_count == 2u);
  unsigned deletes_after_first_cleanup = s_delete_count;
  lua_ui_owner_destroy(L, 1u, 10u);
  assert(s_delete_count == deletes_after_first_cleanup);
  assert(s_cleanup_count == 2u);
  (void)lua_gc(L, LUA_GCCOLLECT, 0);
  assert(s_delete_count == deletes_after_first_cleanup);
  assert(s_cleanup_count == 2u);

  lua_newtable(L);
  assert(lua_ui_handle_test(L, -1) == NULL);
  lua_newtable(L);
  assert(lua_ui_patch(L) == 2);
  assert(lua_isnil(L, -2));
  assert(strstr(lua_tostring(L, -1), "expected") != NULL);
  lua_settop(L, 0);

  lua_ui_owner_destroy(L, 2u, 20u);

  assert(lua_ui_owner_create(L, 3u, 30u));
  lua_ui_owner_enter(L, 3u, 30u);
  lua_settop(L, 0);
  luaopen_ui_container(L);
  lua_newtable(L);
  s_fail_next_create = true;
  lua_call(L, 1, LUA_MULTRET);
  assert(lua_isnil(L, -2));
  assert(strstr(lua_tostring(L, -1), "failed to create container") != NULL);
  lua_settop(L, 0);
  (void)lua_gc(L, LUA_GCCOLLECT, 0);

  for (unsigned i = 0u; i < 1000u; ++i) {
    lua_settop(L, 0);
    luaopen_ui_container(L);
    lua_newtable(L);
    lua_call(L, 1, 1);
    lua_ui_handle_t* stress_handle = lua_ui_handle_test(L, -1);
    assert(stress_handle != NULL && stress_handle->alive);
    assert(lua_ui_delete(L) == 1 && lua_toboolean(L, -1));
    assert(!stress_handle->alive && stress_handle->object == NULL);
  }
  lua_settop(L, 0);
  lua_ui_owner_leave();
  lua_ui_owner_destroy(L, 3u, 30u);
  lua_close(L);
  return 0;
}
