#include "lua_ui.h"
#include "lauxlib.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "resource_manager.h"
#include "lua_assets.h"
#include "xhgc_cart.h"

#define UI_IMAGE_VIEW_ALIGN 32u

typedef struct {
  lua_ui_handle_t handle;
  lv_image_dsc_t descriptor;
  uint8_t* source_data;
  uint8_t* view_data;
  uint8_t* scratch_data;
  uint32_t source_size;
  uint32_t scratch_capacity;
  res_handle_t resource;
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
  bool has_resource;
} lua_ui_image_t;

typedef struct {
  res_handle_t resource;
  uint8_t* pixels;
  uint32_t size;
  uint16_t width;
  uint16_t height;
  uint16_t format;
  uint8_t bpp;
} loaded_image_t;

static const char* const k_create_properties[] = {
    "id", "parent", "src", "rect", "hidden", "region", "style",
    "enabled", "selected", "opacity",
};
static const char* const k_patch_properties[] = {
    "src", "rect", "hidden", "region", "style", "enabled", "selected", "opacity",
};
static const char* const k_style_properties[] = {
    "alpha", "tint", "flip_x", "flip_y",
};

static void clean_dcache_range(const void* pointer, uint32_t size) {
#if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1U)
  if (!pointer || size == 0u) return;
  uintptr_t start = (uintptr_t)pointer & ~(uintptr_t)31u;
  uintptr_t end = ((uintptr_t)pointer + size + 31u) & ~(uintptr_t)31u;
  SCB_CleanDCache_by_Addr((uint32_t*)start, (int32_t)(end - start));
#else
  (void)pointer;
  (void)size;
#endif
}

static bool image_format_info(uint16_t format,
                              lv_color_format_t* color_format,
                              uint8_t* bpp) {
  if (format != XHGC_IMG_BGRA8888) return false;
  *color_format = LV_COLOR_FORMAT_ARGB8888;
  *bpp = 4u;
  return true;
}

static bool region_valid(int32_t sx,
                         int32_t sy,
                         int32_t sw,
                         int32_t sh,
                         uint16_t width,
                         uint16_t height) {
  if (sx < 0 || sy < 0 || sw <= 0 || sh <= 0) return false;
  if ((uint32_t)sx > width || (uint32_t)sy > height) return false;
  return (uint32_t)sw <= (uint32_t)width - (uint32_t)sx &&
         (uint32_t)sh <= (uint32_t)height - (uint32_t)sy;
}

static bool read_integer(lua_State* L,
                         int table_idx,
                         lua_Integer key,
                         int32_t* value,
                         const char* property,
                         char* error,
                         size_t error_size) {
  lua_geti(L, table_idx, key);
  if (!lua_isinteger(L, -1)) {
    lua_pop(L, 1);
    (void)snprintf(error, error_size,
                   "property '%s' expects four integers", property);
    return false;
  }
  lua_Integer raw = lua_tointeger(L, -1);
  lua_pop(L, 1);
  if (raw < INT32_MIN || raw > INT32_MAX) {
    (void)snprintf(error, error_size,
                   "property '%s' integer is out of range", property);
    return false;
  }
  *value = (int32_t)raw;
  return true;
}

static bool parse_region(lua_State* L,
                         int properties_idx,
                         lua_ui_image_t* image,
                         bool reset_to_source,
                         bool* present,
                         char* error,
                         size_t error_size) {
  properties_idx = lua_absindex(L, properties_idx);
  lua_getfield(L, properties_idx, "region");
  if (lua_isnil(L, -1)) {
    lua_pop(L, 1);
    *present = false;
    if (reset_to_source) {
      image->sx = 0;
      image->sy = 0;
      image->sw = image->source_w;
      image->sh = image->source_h;
    }
    return true;
  }
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    (void)snprintf(error, error_size, "property 'region' expects a table");
    return false;
  }
  int region_idx = lua_gettop(L);
  int32_t sx, sy, sw, sh;
  bool ok = read_integer(L, region_idx, 1, &sx, "region", error, error_size) &&
            read_integer(L, region_idx, 2, &sy, "region", error, error_size) &&
            read_integer(L, region_idx, 3, &sw, "region", error, error_size) &&
            read_integer(L, region_idx, 4, &sh, "region", error, error_size);
  lua_pop(L, 1);
  if (!ok) return false;
  if (!region_valid(sx, sy, sw, sh, image->source_w, image->source_h)) {
    (void)snprintf(error, error_size, "property 'region' is outside the image");
    return false;
  }
  image->sx = sx;
  image->sy = sy;
  image->sw = sw;
  image->sh = sh;
  *present = true;
  return true;
}

static bool load_image(const char* src,
                       loaded_image_t* loaded,
                       char* error,
                       size_t error_size) {
  memset(loaded, 0, sizeof(*loaded));
  if (!src || src[0] == '\0') {
    (void)snprintf(error, error_size, "property 'src' must not be empty");
    return false;
  }
  if (!cart_path_is_valid(src)) {
    (void)snprintf(error, error_size, "property 'src' is not a valid cart path");
    return false;
  }

  loaded->resource = res_acquire_image(src, RES_LIFE_SCENE);
  const image_resource_t* resource = res_get_image(loaded->resource);
  if (!resource) {
    const char* detail = res_last_error();
    (void)snprintf(error, error_size, "%s",
                   detail ? detail : "failed to load image");
    return false;
  }
  lv_color_format_t ignored;
  if (!image_format_info(resource->format, &ignored, &loaded->bpp)) {
    res_release(loaded->resource);
    (void)snprintf(error, error_size, "unsupported image format");
    return false;
  }
  if (resource->width == 0u || resource->height == 0u ||
      (uint64_t)resource->width * resource->height * loaded->bpp > resource->size) {
    res_release(loaded->resource);
    (void)snprintf(error, error_size, "invalid image resource");
    return false;
  }
  loaded->pixels = (uint8_t*)resource->pixels;
  loaded->size = resource->size;
  loaded->width = resource->width;
  loaded->height = resource->height;
  loaded->format = resource->format;
  return true;
}

static bool load_image_handle(lua_State* L, int index, loaded_image_t* loaded,
                              char* error, size_t error_size) {
  memset(loaded, 0, sizeof(*loaded));
  const image_resource_t* resource = NULL;
  const char* detail = NULL;
  if (!lua_asset_image_acquire(L, index, &loaded->resource, &resource, &detail)) {
    (void)snprintf(error, error_size, "%s", detail ? detail : "image asset failed");
    return false;
  }
  lv_color_format_t ignored;
  if (!image_format_info(resource->format, &ignored, &loaded->bpp)) {
    res_release(loaded->resource);
    (void)snprintf(error, error_size, "unsupported image format");
    return false;
  }
  if (resource->width == 0u || resource->height == 0u ||
      (uint64_t)resource->width * resource->height * loaded->bpp >
          resource->size) {
    res_release(loaded->resource);
    (void)snprintf(error, error_size, "invalid image asset");
    return false;
  }
  loaded->pixels = (uint8_t*)resource->pixels;
  loaded->size = resource->size;
  loaded->width = resource->width;
  loaded->height = resource->height;
  loaded->format = resource->format;
  return true;
}

static bool rebuild_view(lua_ui_image_t* image,
                         char* error,
                         size_t error_size) {
  lv_color_format_t color_format;
  uint8_t bpp;
  if (!image_format_info(image->format, &color_format, &bpp) ||
      !region_valid(image->sx, image->sy, image->sw, image->sh,
                    image->source_w, image->source_h)) {
    (void)snprintf(error, error_size, "invalid image view");
    return false;
  }

  uint32_t source_stride = (uint32_t)image->source_w * bpp;
  uint32_t stride = lv_draw_buf_width_to_stride((uint32_t)image->sw,
                                                 color_format);
  uint64_t required64 = (uint64_t)stride * (uint32_t)image->sh;
  if (required64 > UINT32_MAX) {
    (void)snprintf(error, error_size, "image view is too large");
    return false;
  }
  uint32_t required = (uint32_t)required64;
  bool copy = image->flip_x || image->flip_y || image->sx != 0 ||
              image->sy != 0 || image->sw != image->source_w ||
              image->sh != image->source_h || stride != source_stride;

  if (copy) {
    if (!image->scratch_data || image->scratch_capacity < required) {
      uint8_t* scratch =
          (uint8_t*)res_alloc_image_view_buffer(required, UI_IMAGE_VIEW_ALIGN);
      if (!scratch) {
        const char* detail = res_last_error();
        (void)snprintf(error, error_size, "%s",
                       detail ? detail : "image view allocation failed");
        return false;
      }
      image->scratch_data = scratch;
      image->scratch_capacity = required;
    }
    image->view_data = image->scratch_data;
    memset(image->view_data, 0, required);
    for (int32_t y = 0; y < image->sh; ++y) {
      int32_t source_y = image->flip_y
          ? image->sy + image->sh - 1 - y : image->sy + y;
      const uint8_t* source_row =
          image->source_data + (uint32_t)source_y * source_stride;
      uint8_t* target_row = image->view_data + (uint32_t)y * stride;
      for (int32_t x = 0; x < image->sw; ++x) {
        int32_t source_x = image->flip_x
            ? image->sx + image->sw - 1 - x : image->sx + x;
        memcpy(target_row + (uint32_t)x * bpp,
               source_row + (uint32_t)source_x * bpp, bpp);
      }
    }
  } else {
    image->view_data = image->source_data;
    stride = source_stride;
    required = image->source_size;
  }

  memset(&image->descriptor, 0, sizeof(image->descriptor));
  image->descriptor.header.magic = LV_IMAGE_HEADER_MAGIC;
  image->descriptor.header.cf = color_format;
  image->descriptor.header.w = (uint32_t)image->sw;
  image->descriptor.header.h = (uint32_t)image->sh;
  image->descriptor.header.stride = stride;
  image->descriptor.data_size = required;
  image->descriptor.data = image->view_data;
  clean_dcache_range(image->view_data, required);
  lv_image_set_src(image->handle.object, &image->descriptor);
  lv_obj_invalidate(image->handle.object);
  return true;
}

static bool read_style_integer(lua_State* L,
                               int style_idx,
                               const char* key,
                               int32_t* value,
                               bool* present,
                               char* error,
                               size_t error_size) {
  lua_getfield(L, style_idx, key);
  if (lua_isnil(L, -1)) {
    lua_pop(L, 1);
    *present = false;
    return true;
  }
  if (!lua_isinteger(L, -1)) {
    lua_pop(L, 1);
    (void)snprintf(error, error_size, "property 'style.%s' expects integer", key);
    return false;
  }
  lua_Integer raw = lua_tointeger(L, -1);
  lua_pop(L, 1);
  if (raw < INT32_MIN || raw > UINT32_MAX) {
    (void)snprintf(error, error_size, "property 'style.%s' is out of range", key);
    return false;
  }
  *value = (int32_t)raw;
  *present = true;
  return true;
}

static bool apply_style(lua_State* L,
                        int properties_idx,
                        lua_ui_image_t* image,
                        bool* rebuild,
                        char* error,
                        size_t error_size) {
  properties_idx = lua_absindex(L, properties_idx);
  lua_getfield(L, properties_idx, "style");
  if (lua_isnil(L, -1)) {
    lua_pop(L, 1);
    return true;
  }
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    (void)snprintf(error, error_size, "property 'style' expects a table");
    return false;
  }
  int style_idx = lua_gettop(L);
  if (!lua_ui_validate_properties(L, style_idx, k_style_properties,
                                  sizeof(k_style_properties) /
                                      sizeof(k_style_properties[0]),
                                  &image->handle, error, error_size)) {
    lua_pop(L, 1);
    return false;
  }

  int32_t integer;
  bool present;
  if (!read_style_integer(L, style_idx, "alpha", &integer, &present,
                          error, error_size)) {
    lua_pop(L, 1);
    return false;
  }
  if (present) {
    if (integer < 0 || integer > 255) {
      lua_pop(L, 1);
      (void)snprintf(error, error_size, "property 'style.alpha' expects 0..255");
      return false;
    }
    lv_obj_set_style_image_opa(image->handle.object, (lv_opa_t)integer, 0);
  }
  if (!read_style_integer(L, style_idx, "tint", &integer, &present,
                          error, error_size)) {
    lua_pop(L, 1);
    return false;
  }
  if (present) {
    lv_obj_set_style_image_recolor(image->handle.object,
                                   lv_color_hex((uint32_t)integer), 0);
    lv_obj_set_style_image_recolor_opa(image->handle.object, LV_OPA_COVER, 0);
  }

  const char* flip_keys[] = {"flip_x", "flip_y"};
  bool* flip_values[] = {&image->flip_x, &image->flip_y};
  for (size_t i = 0; i < 2u; ++i) {
    lua_getfield(L, style_idx, flip_keys[i]);
    if (!lua_isnil(L, -1)) {
      if (!lua_isboolean(L, -1)) {
        lua_pop(L, 2);
        (void)snprintf(error, error_size,
                       "property 'style.%s' expects boolean", flip_keys[i]);
        return false;
      }
      bool value = lua_toboolean(L, -1);
      if (*flip_values[i] != value) {
        *flip_values[i] = value;
        *rebuild = true;
      }
    }
    lua_pop(L, 1);
  }
  lua_pop(L, 1);
  return true;
}

static void image_cleanup(lua_ui_handle_t* handle) {
  lua_ui_image_t* image = (lua_ui_image_t*)handle;
  if (image->has_resource) {
    res_release(image->resource);
    image->has_resource = false;
  }
  image->source_data = NULL;
  image->view_data = NULL;
  image->scratch_data = NULL;
  image->scratch_capacity = 0u;
}

static bool image_apply(lua_State* L,
                        lua_ui_image_t* image,
                        int properties_idx,
                        bool creating,
                        char* error,
                        size_t error_size) {
  lua_ui_image_t previous = *image;
  const char* const* allowed = creating ? k_create_properties : k_patch_properties;
  size_t allowed_count = creating
      ? sizeof(k_create_properties) / sizeof(k_create_properties[0])
      : sizeof(k_patch_properties) / sizeof(k_patch_properties[0]);
  if (!lua_ui_validate_properties(L, properties_idx, allowed, allowed_count,
                                  &image->handle, error, error_size)) {
    return false;
  }

  const char* id = NULL;
  bool id_present = false;
  if (!lua_ui_read_optional_string(L, properties_idx, "id", &id, &id_present,
                                   error, error_size)) {
    return false;
  }
  if (id_present) {
    if (id[0] == '\0' || strlen(id) >= sizeof(image->handle.debug_id)) {
      (void)snprintf(error, error_size, "property 'id' is empty or too long");
      return false;
    }
    (void)snprintf(image->handle.debug_id,
                   sizeof(image->handle.debug_id), "%s", id);
  }

  const char* src = NULL;
  bool src_present = false;
  properties_idx = lua_absindex(L, properties_idx);
  lua_getfield(L, properties_idx, "src");
  int src_index = lua_gettop(L);
  if (!lua_isnil(L, src_index)) {
    src_present = true;
    if (lua_type(L, src_index) == LUA_TSTRING) src = lua_tostring(L, src_index);
    else if (!luaL_testudata(L, src_index, LUA_ASSET_HANDLE_MT)) {
      lua_pop(L, 1);
      (void)snprintf(error, error_size,
                     "property 'src' expects a path or image asset handle");
      return false;
    }
  }
  if (creating && !src_present) {
    lua_pop(L, 1);
    (void)snprintf(error, error_size, "property 'src' is required");
    return false;
  }

  loaded_image_t loaded;
  bool source_changed = false;
  if (src_present) {
    bool loaded_ok = src ? load_image(src, &loaded, error, error_size)
                         : load_image_handle(L, src_index, &loaded,
                                             error, error_size);
    if (!loaded_ok) { lua_pop(L, 1); return false; }
    source_changed = true;
    image->source_data = loaded.pixels;
    image->source_size = loaded.size;
    image->source_w = loaded.width;
    image->source_h = loaded.height;
    image->format = loaded.format;
    image->bpp = loaded.bpp;
  }
  lua_pop(L, 1);

  bool region_present = false;
  if (!parse_region(L, properties_idx, image, source_changed, &region_present,
                    error, error_size)) {
    if (source_changed) {
      res_release(loaded.resource);
      image->source_data = previous.source_data;
      image->source_size = previous.source_size;
      image->source_w = previous.source_w;
      image->source_h = previous.source_h;
      image->format = previous.format;
      image->bpp = previous.bpp;
      image->sx = previous.sx;
      image->sy = previous.sy;
      image->sw = previous.sw;
      image->sh = previous.sh;
    }
    return false;
  }
  bool rebuild = source_changed || region_present;
  if (!apply_style(L, properties_idx, image, &rebuild, error, error_size) ||
      (rebuild && !rebuild_view(image, error, error_size)) ||
      !lua_ui_apply_rect(L, properties_idx, image->handle.object,
                         0, 0,
                         creating ? image->sw : 0,
                         creating ? image->sh : 0,
                         error, error_size) ||
      !lua_ui_apply_hidden(L, properties_idx, image->handle.object,
                           error, error_size) ||
      !lua_ui_apply_common_state(L, properties_idx, image->handle.object,
                                 error, error_size)) {
    if (source_changed) {
      res_release(loaded.resource);
      image->source_data = previous.source_data;
      image->source_size = previous.source_size;
      image->source_w = previous.source_w;
      image->source_h = previous.source_h;
      image->format = previous.format;
      image->bpp = previous.bpp;
      image->sx = previous.sx;
      image->sy = previous.sy;
      image->sw = previous.sw;
      image->sh = previous.sh;
      image->flip_x = previous.flip_x;
      image->flip_y = previous.flip_y;
      image->resource = previous.resource;
      image->has_resource = previous.has_resource;
      if (previous.has_resource) {
        char ignored_error[LUA_UI_ERROR_MAX];
        (void)rebuild_view(image, ignored_error, sizeof(ignored_error));
      }
    }
    return false;
  }

  if (source_changed) {
    if (image->has_resource) res_release(image->resource);
    image->resource = loaded.resource;
    image->has_resource = true;
  }
  int32_t width = lv_obj_get_width(image->handle.object);
  int32_t height = lv_obj_get_height(image->handle.object);
  lv_image_set_inner_align(image->handle.object,
                           width == image->sw && height == image->sh
                               ? LV_IMAGE_ALIGN_DEFAULT
                               : LV_IMAGE_ALIGN_STRETCH);
  return true;
}

static int image_create(lua_State* L) {
  char error[LUA_UI_ERROR_MAX];
  if (!lua_istable(L, 2)) {
    return lua_ui_push_error(L, "ui.image expects a properties table");
  }
  lv_obj_t* root = lua_ui_resolve_parent(L, 2, error, sizeof(error));
  if (!root) return lua_ui_push_error(L, error);

  lua_ui_image_t* image = (lua_ui_image_t*)lua_ui_handle_new(
      L, sizeof(*image), LUA_UI_OBJECT_IMAGE);
  if (!image) return lua_ui_push_error(L, "failed to allocate image handle");
  int handle_idx = lua_gettop(L);
  lv_obj_t* object = lv_image_create(root);
  if (!object) return lua_ui_push_error(L, "failed to create image");
  image->handle.object = object;

  if (!image_apply(L, image, 2, true, error, sizeof(error))) {
    lv_obj_delete(object);
    image->handle.object = NULL;
    image_cleanup(&image->handle);
    return lua_ui_push_error(L, error);
  }
  if (!lua_ui_handle_register(L, handle_idx, &image->handle, object,
                              image_cleanup)) {
    lv_obj_delete(object);
    image->handle.object = NULL;
    image_cleanup(&image->handle);
    return lua_ui_push_error(L, "failed to register image owner");
  }
  return 1;
}

bool lua_ui_image_patch(lua_State* L,
                        lua_ui_handle_t* handle,
                        int properties_idx,
                        char* error,
                        size_t error_size) {
  if (!handle || handle->object_type != LUA_UI_OBJECT_IMAGE) return false;
  return image_apply(L, (lua_ui_image_t*)handle, properties_idx, false,
                     error, error_size);
}

int luaopen_ui_image(lua_State* L) {
  lua_newtable(L);
  lua_newtable(L);
  lua_pushcfunction(L, image_create);
  lua_setfield(L, -2, "__call");
  lua_setmetatable(L, -2);
  return 1;
}
