#include "lua_ui.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "lauxlib.h"
#include "lvgl.h"
#include "resource_manager.h"
#include "xhgc_cart.h"

#define UI_IMAGE_MT "ui.image.mt"

#ifndef LUA_UI_IMAGE_DEBUG_DUMP
#define LUA_UI_IMAGE_DEBUG_DUMP 1
#endif

static void clean_dcache_range(const void* ptr, uint32_t size) {
#if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1U)
  if (!ptr || size == 0u) return;
  uintptr_t start = (uintptr_t)ptr & ~(uintptr_t)31u;
  uintptr_t end = ((uintptr_t)ptr + size + 31u) & ~(uintptr_t)31u;
  SCB_CleanDCache_by_Addr((uint32_t*)start, (int32_t)(end - start));
#else
  (void)ptr;
  (void)size;
#endif
}

typedef struct {
  lv_obj_t* img;
  lv_image_dsc_t dsc;
  uint8_t* source_data;
  uint8_t* view_data;
  uint32_t source_size;
  uint32_t view_size;
  res_handle_t image_res;
  uint16_t source_w;
  uint16_t source_h;
  uint16_t format;
  uint8_t bpp;
  int32_t sx;
  int32_t sy;
  int32_t sw;
  int32_t sh;
  bool flip_x;
  bool flip_y;
  bool has_image_res;
  bool view_data_owned;
  char id[LUA_UI_DRAWABLE_ID_MAX];
} ui_image_ud_t;

static ui_image_ud_t* test_image(lua_State* L, int idx) {
  return (ui_image_ud_t*)luaL_testudata(L, idx, UI_IMAGE_MT);
}

static ui_image_ud_t* check_image(lua_State* L, int idx) {
  return (ui_image_ud_t*)luaL_checkudata(L, idx, UI_IMAGE_MT);
}

#if LUA_UI_IMAGE_DEBUG_DUMP
static uint32_t debug_argb_from_bgra(const uint8_t* p) {
  if (!p) return 0u;
  return ((uint32_t)p[3] << 24) |
         ((uint32_t)p[2] << 16) |
         ((uint32_t)p[1] << 8) |
         (uint32_t)p[0];
}

static void debug_dump_image_dsc(const ui_image_ud_t* ud) {
  if (!ud || !ud->view_data) return;

  printf("[ui.image.dump] dsc cf=%u wh=%lux%lu stride=%lu data_size=%lu data=%p region=%ld,%ld %ldx%ld\n",
         (unsigned)ud->dsc.header.cf,
         (unsigned long)ud->dsc.header.w,
         (unsigned long)ud->dsc.header.h,
         (unsigned long)ud->dsc.header.stride,
         (unsigned long)ud->dsc.data_size,
         (const void*)ud->dsc.data,
         (long)ud->sx,
         (long)ud->sy,
         (long)ud->sw,
         (long)ud->sh);

  uint32_t rows[] = {0u, 1u, 2u, ud->dsc.header.h > 0u ? ud->dsc.header.h - 1u : 0u};
  for (uint32_t r = 0u; r < (uint32_t)(sizeof(rows) / sizeof(rows[0])); ++r) {
    uint32_t y = rows[r];
    if (y >= ud->dsc.header.h) continue;
    const uint8_t* row = ud->view_data + y * ud->dsc.header.stride;
    printf("[ui.image.dump] y=%lu argb=%08lX,%08lX,%08lX,%08lX bgra=%02X%02X%02X%02X\n",
           (unsigned long)y,
           (unsigned long)debug_argb_from_bgra(row + 0u),
           (unsigned long)debug_argb_from_bgra(row + 4u),
           (unsigned long)debug_argb_from_bgra(row + 8u),
           (unsigned long)debug_argb_from_bgra(row + 12u),
           (unsigned)row[0], (unsigned)row[1], (unsigned)row[2], (unsigned)row[3]);
  }
}
#endif

static void check_image_valid(lua_State* L, ui_image_ud_t* ud) {
  if (!ud || !ud->img) luaL_error(L, "image has been deleted");
}

static bool table_get_int(lua_State* L, int table, const char* key, int32_t* out) {
  table = lua_absindex(L, table);
  lua_getfield(L, table, key);
  if (lua_isnil(L, -1)) {
    lua_pop(L, 1);
    return false;
  }
  *out = (int32_t)luaL_checkinteger(L, -1);
  lua_pop(L, 1);
  return true;
}

static bool table_get_uint(lua_State* L, int table, const char* key, uint32_t* out) {
  int32_t value = 0;
  if (!table_get_int(L, table, key, &value)) return false;
  luaL_argcheck(L, value >= 0, 1, "color must be non-negative");
  *out = (uint32_t)value;
  return true;
}

static bool table_get_bool(lua_State* L, int table, const char* key, bool* out) {
  table = lua_absindex(L, table);
  lua_getfield(L, table, key);
  if (lua_isnil(L, -1)) {
    lua_pop(L, 1);
    return false;
  }
  *out = lua_toboolean(L, -1);
  lua_pop(L, 1);
  return true;
}

static bool table_get_int_index(lua_State* L, int table, lua_Integer index, int32_t* out) {
  table = lua_absindex(L, table);
  lua_geti(L, table, index);
  if (lua_isnil(L, -1)) {
    lua_pop(L, 1);
    return false;
  }
  *out = (int32_t)luaL_checkinteger(L, -1);
  lua_pop(L, 1);
  return true;
}

static bool table_get_string(lua_State* L, int table, const char* key, const char** out) {
  table = lua_absindex(L, table);
  lua_getfield(L, table, key);
  if (lua_isnil(L, -1)) {
    lua_pop(L, 1);
    return false;
  }
  *out = luaL_checkstring(L, -1);
  lua_pop(L, 1);
  return true;
}

static bool config_has_field(lua_State* L, int table, const char* key) {
  table = lua_absindex(L, table);
  lua_getfield(L, table, key);
  bool present = !lua_isnil(L, -1);
  lua_pop(L, 1);
  return present;
}

static bool image_format_info(uint16_t format, lv_color_format_t* cf, uint8_t* bpp) {
  if (format == XHGC_IMG_BGRA8888) {
    *cf = LV_COLOR_FORMAT_ARGB8888;
    *bpp = 4u;
    return true;
  }
  return false;
}

static bool region_in_source(int32_t sx,
                             int32_t sy,
                             int32_t sw,
                             int32_t sh,
                             uint16_t source_w,
                             uint16_t source_h) {
  if (sx < 0 || sy < 0 || sw <= 0 || sh <= 0) return false;
  if ((uint32_t)sx > source_w || (uint32_t)sy > source_h) return false;
  if ((uint32_t)sw > (uint32_t)source_w - (uint32_t)sx) return false;
  if ((uint32_t)sh > (uint32_t)source_h - (uint32_t)sy) return false;
  return true;
}

static bool parse_image_region(lua_State* L,
                               int config_idx,
                               uint16_t source_w,
                               uint16_t source_h,
                               int32_t* sx,
                               int32_t* sy,
                               int32_t* sw,
                               int32_t* sh) {
  config_idx = lua_absindex(L, config_idx);
  *sx = 0;
  *sy = 0;
  *sw = source_w;
  *sh = source_h;

  lua_getfield(L, config_idx, "region");
  if (lua_istable(L, -1)) {
    (void)table_get_int_index(L, -1, 1, sx);
    (void)table_get_int_index(L, -1, 2, sy);
    (void)table_get_int_index(L, -1, 3, sw);
    (void)table_get_int_index(L, -1, 4, sh);
  }
  lua_pop(L, 1);

  return region_in_source(*sx, *sy, *sw, *sh, source_w, source_h);
}

static int rebuild_view(lua_State* L, ui_image_ud_t* ud) {
  lv_color_format_t cf = LV_COLOR_FORMAT_UNKNOWN;
  uint8_t bpp = 0;
  uint32_t stride = 0;
  uint32_t size = 0;
  uint32_t source_stride = 0;
  uint32_t row_bytes = 0;
  bool needs_copy = false;

  check_image_valid(L, ud);
  if (!image_format_info(ud->format, &cf, &bpp)) {
    return luaL_error(L, "unsupported image format");
  }
  if (!region_in_source(ud->sx, ud->sy, ud->sw, ud->sh, ud->source_w, ud->source_h)) {
    return luaL_error(L, "invalid image region");
  }

  source_stride = (uint32_t)ud->source_w * bpp;
  stride = lv_draw_buf_width_to_stride((uint32_t)ud->sw, cf);
  row_bytes = (uint32_t)ud->sw * bpp;
  size = stride * (uint32_t)ud->sh;
  needs_copy = ud->flip_x || ud->flip_y ||
               ud->sx != 0 || ud->sy != 0 ||
               ud->sw != ud->source_w || ud->sh != ud->source_h ||
               stride != source_stride;

  if (ud->view_data_owned && ud->view_data) {
    lv_free(ud->view_data);
  }
  ud->view_data = NULL;
  ud->view_data_owned = false;

  if (needs_copy) {
    ud->view_data = (uint8_t*)lv_malloc(size);
    if (!ud->view_data) return luaL_error(L, "out of memory");
    memset(ud->view_data, 0, size);
    ud->view_data_owned = true;
    for (int32_t y = 0; y < ud->sh; ++y) {
      int32_t src_y = ud->flip_y ? (ud->sy + ud->sh - 1 - y) : (ud->sy + y);
      const uint8_t* src_row = ud->source_data + (uint32_t)src_y * source_stride;
      uint8_t* dst_row = ud->view_data + (uint32_t)y * stride;
      if (ud->flip_x) {
        for (int32_t x = 0; x < ud->sw; ++x) {
          int32_t src_x = ud->sx + ud->sw - 1 - x;
          memcpy(dst_row + (uint32_t)x * bpp,
                 src_row + (uint32_t)src_x * bpp,
                 bpp);
        }
      } else {
        memcpy(dst_row,
               src_row + (uint32_t)ud->sx * bpp,
               row_bytes);
      }
    }
  } else {
    stride = source_stride;
    size = ud->source_size;
    ud->view_data = ud->source_data;
  }

  ud->view_size = size;
  memset(&ud->dsc, 0, sizeof(ud->dsc));
  ud->dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
  ud->dsc.header.cf = cf;
  ud->dsc.header.flags = 0u;
  ud->dsc.header.w = (uint32_t)ud->sw;
  ud->dsc.header.h = (uint32_t)ud->sh;
  ud->dsc.header.stride = stride;
  ud->dsc.data_size = size;
  ud->dsc.data = ud->view_data;
  clean_dcache_range(ud->view_data, size);
  lv_image_set_src(ud->img, &ud->dsc);
  lv_obj_invalidate(ud->img);
  return 0;
}

static void apply_image_style(lua_State* L, ui_image_ud_t* ud, int style_idx, bool* rebuild) {
  int32_t alpha = 255;
  uint32_t tint = 0;
  bool flip = false;
  style_idx = lua_absindex(L, style_idx);

  if (table_get_int(L, style_idx, "alpha", &alpha)) {
    luaL_argcheck(L, alpha >= 0 && alpha <= 255, 1, "style.alpha must be 0..255");
    lv_obj_set_style_image_opa(ud->img, (lv_opa_t)alpha, LV_PART_MAIN | LV_STATE_DEFAULT);
  }
  if (table_get_uint(L, style_idx, "tint", &tint)) {
    lv_obj_set_style_image_recolor(ud->img, lv_color_hex(tint), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor_opa(ud->img, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
  }
  if (table_get_bool(L, style_idx, "flip_x", &flip) && ud->flip_x != flip) {
    ud->flip_x = flip;
    *rebuild = true;
  }
  if (table_get_bool(L, style_idx, "flip_y", &flip) && ud->flip_y != flip) {
    ud->flip_y = flip;
    *rebuild = true;
  }
}

static int apply_image_config(lua_State* L, ui_image_ud_t* ud, int config_idx) {
  const char* id = NULL;
  int32_t x = lv_obj_get_x(ud->img);
  int32_t y = lv_obj_get_y(ud->img);
  int32_t w = ud->sw > 0 ? ud->sw : ud->source_w;
  int32_t h = ud->sh > 0 ? ud->sh : ud->source_h;
  bool rebuild = false;
  config_idx = lua_absindex(L, config_idx);

  check_image_valid(L, ud);
  lua_getfield(L, config_idx, "rect");
  if (lua_istable(L, -1)) {
    (void)table_get_int_index(L, -1, 1, &x);
    (void)table_get_int_index(L, -1, 2, &y);
    (void)table_get_int_index(L, -1, 3, &w);
    (void)table_get_int_index(L, -1, 4, &h);
  }
  lua_pop(L, 1);

  lua_getfield(L, config_idx, "pos");
  if (lua_istable(L, -1)) {
    (void)table_get_int_index(L, -1, 1, &x);
    (void)table_get_int_index(L, -1, 2, &y);
  }
  lua_pop(L, 1);

  lua_getfield(L, config_idx, "size");
  if (lua_istable(L, -1)) {
    (void)table_get_int_index(L, -1, 1, &w);
    (void)table_get_int_index(L, -1, 2, &h);
  }
  lua_pop(L, 1);

  (void)table_get_int(L, config_idx, "x", &x);
  (void)table_get_int(L, config_idx, "y", &y);
  (void)table_get_int(L, config_idx, "w", &w);
  (void)table_get_int(L, config_idx, "h", &h);

  lua_getfield(L, config_idx, "region");
  if (lua_istable(L, -1)) {
    int32_t sx = ud->sx;
    int32_t sy = ud->sy;
    int32_t sw = ud->sw;
    int32_t sh = ud->sh;
    (void)table_get_int_index(L, -1, 1, &sx);
    (void)table_get_int_index(L, -1, 2, &sy);
    (void)table_get_int_index(L, -1, 3, &sw);
    (void)table_get_int_index(L, -1, 4, &sh);
    if (sx != ud->sx || sy != ud->sy || sw != ud->sw || sh != ud->sh) {
      ud->sx = sx;
      ud->sy = sy;
      ud->sw = sw;
      ud->sh = sh;
      rebuild = true;
      if (!config_has_field(L, config_idx, "w") && !config_has_field(L, config_idx, "size") && !config_has_field(L, config_idx, "rect")) w = sw;
      if (!config_has_field(L, config_idx, "h") && !config_has_field(L, config_idx, "size") && !config_has_field(L, config_idx, "rect")) h = sh;
    }
  }
  lua_pop(L, 1);

  if (table_get_string(L, config_idx, "id", &id)) {
    snprintf(ud->id, sizeof(ud->id), "%s", id);
  }
  lua_getfield(L, config_idx, "hidden");
  if (!lua_isnil(L, -1)) {
    if (lua_toboolean(L, -1)) lv_obj_add_flag(ud->img, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_remove_flag(ud->img, LV_OBJ_FLAG_HIDDEN);
  }
  lua_pop(L, 1);

  lua_getfield(L, config_idx, "style");
  if (lua_istable(L, -1)) {
    apply_image_style(L, ud, -1, &rebuild);
  }
  lua_pop(L, 1);

  if (rebuild) (void)rebuild_view(L, ud);
  lv_obj_set_pos(ud->img, x, y);
  lv_obj_set_size(ud->img, w, h);
  lv_image_set_inner_align(ud->img,
                           (w == ud->sw && h == ud->sh)
                               ? LV_IMAGE_ALIGN_DEFAULT
                               : LV_IMAGE_ALIGN_STRETCH);
  lv_obj_invalidate(ud->img);
  return 0;
}

static int l_image_call(lua_State* L) {
  luaL_checktype(L, 2, LUA_TTABLE);

  const char* src = NULL;
  const char* err = NULL;
  res_handle_t handle;
  const image_resource_t* image = NULL;
  lv_color_format_t cf = LV_COLOR_FORMAT_UNKNOWN;
  uint8_t bpp = 0;
  int32_t sx = 0;
  int32_t sy = 0;
  int32_t sw = 0;
  int32_t sh = 0;

  if (!table_get_string(L, 2, "src", &src) || !src || src[0] == '\0') {
    luaL_error(L, "ui.image src is required");
  }
  if (!cart_path_is_valid(src)) {
    luaL_error(L, "invalid cart resource path");
  }

  handle = res_acquire_image(src, RES_LIFE_SCENE);
  image = res_get_image(handle);
  if (!image) {
    err = res_last_error();
    lua_pushnil(L);
    lua_pushstring(L, err ? err : "failed to load image");
    return 2;
  }
  if (!image_format_info(image->format, &cf, &bpp)) {
    res_release(handle);
    lua_pushnil(L);
    lua_pushliteral(L, "unsupported image format");
    return 2;
  }
  if (image->width == 0u || image->height == 0u ||
      (uint64_t)image->width * image->height * bpp > image->size) {
    res_release(handle);
    lua_pushnil(L);
    lua_pushliteral(L, "invalid image resource");
    return 2;
  }
  if (!parse_image_region(L, 2, image->width, image->height, &sx, &sy, &sw, &sh)) {
    res_release(handle);
    lua_pushnil(L);
    lua_pushliteral(L, "invalid image region");
    return 2;
  }

  ui_image_ud_t* ud = (ui_image_ud_t*)lua_newuserdatauv(L, sizeof(ui_image_ud_t), 0);
  memset(ud, 0, sizeof(*ud));
  ud->source_data = (uint8_t*)image->pixels;
  ud->source_size = image->size;
  ud->image_res = handle;
  ud->source_w = image->width;
  ud->source_h = image->height;
  ud->format = image->format;
  ud->bpp = bpp;
  ud->has_image_res = true;
  ud->sx = sx;
  ud->sy = sy;
  ud->sw = sw;
  ud->sh = sh;

  ud->img = lv_image_create(lv_screen_active());
  if (!ud->img) {
    res_release(handle);
    ud->has_image_res = false;
    lua_pushnil(L);
    lua_pushliteral(L, "failed to create image");
    return 2;
  }

  lv_obj_set_user_data(ud->img, ud);
  luaL_getmetatable(L, UI_IMAGE_MT);
  lua_setmetatable(L, -2);
  (void)rebuild_view(L, ud);
  apply_image_config(L, ud, 2);
#if LUA_UI_IMAGE_DEBUG_DUMP
  debug_dump_image_dsc(ud);
#endif
  return 1;
}

static int l_image_gc(lua_State* L) {
  lua_ui_image_delete(L, 1);
  return 0;
}

bool lua_ui_image_is(lua_State* L, int idx) {
  return test_image(L, idx) != NULL;
}

const char* lua_ui_image_id(lua_State* L, int idx) {
  ui_image_ud_t* ud = test_image(L, idx);
  return ud ? ud->id : NULL;
}

int lua_ui_image_patch(lua_State* L, int drawable_idx, int patch_idx) {
  ui_image_ud_t* ud = check_image(L, drawable_idx);
  luaL_checktype(L, patch_idx, LUA_TTABLE);
  if (config_has_field(L, patch_idx, "src")) return -1;
  return apply_image_config(L, ud, patch_idx);
}

void lua_ui_image_delete(lua_State* L, int idx) {
  ui_image_ud_t* ud = test_image(L, idx);
  if (!ud) return;

  if (ud->img) {
    lv_obj_delete(ud->img);
    ud->img = NULL;
  }
  if (ud->view_data_owned && ud->view_data) {
    lv_free(ud->view_data);
  }
  ud->view_data = NULL;
  ud->view_data_owned = false;
  if (ud->has_image_res) {
    res_release(ud->image_res);
    ud->has_image_res = false;
  }
  ud->source_data = NULL;
}

static void create_image_metatable(lua_State* L) {
  if (luaL_newmetatable(L, UI_IMAGE_MT)) {
    lua_pushcfunction(L, l_image_gc);
    lua_setfield(L, -2, "__gc");
  }
  lua_pop(L, 1);
}

int luaopen_ui_image(lua_State* L) {
  create_image_metatable(L);

  lua_newtable(L);
  lua_newtable(L);
  lua_pushcfunction(L, l_image_call);
  lua_setfield(L, -2, "__call");
  lua_setmetatable(L, -2);
  return 1;
}
