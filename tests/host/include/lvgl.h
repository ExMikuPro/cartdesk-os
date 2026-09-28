#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct lv_obj_t lv_obj_t;
typedef struct lv_event_t lv_event_t;
typedef struct lv_event_dsc_t lv_event_dsc_t;
typedef void (*lv_event_cb_t)(lv_event_t* event);

typedef int32_t lv_event_code_t;
typedef uint8_t lv_opa_t;
typedef uint32_t lv_color_t;
typedef uint32_t lv_color_format_t;

typedef struct {
  uint32_t magic;
  lv_color_format_t cf;
  uint32_t w;
  uint32_t h;
  uint32_t stride;
} lv_image_header_t;

typedef struct {
  lv_image_header_t header;
  uint32_t data_size;
  const uint8_t* data;
} lv_image_dsc_t;

#define LV_EVENT_DELETE 1
#define LV_EVENT_CLICKED 2
#define LV_EVENT_PRESSED 3
#define LV_EVENT_RELEASED 4
#define LV_EVENT_ALL 255
#define LV_OBJ_FLAG_CLICKABLE (1u << 0)
#define LV_OBJ_FLAG_SCROLLABLE (1u << 1)
#define LV_OBJ_FLAG_HIDDEN (1u << 2)
#define LV_PCT(value) (value)
#define LV_PART_MAIN 0
#define LV_STATE_DEFAULT 0
#define LV_STATE_DISABLED (1u << 0)
#define LV_STATE_CHECKED (1u << 1)
#define LV_COLOR_FORMAT_ARGB8888 1u
#define LV_IMAGE_HEADER_MAGIC 0x19u
#define LV_IMAGE_ALIGN_DEFAULT 0u
#define LV_IMAGE_ALIGN_STRETCH 1u
#define LV_OPA_COVER 255u

struct lv_event_dsc_t {
  lv_event_cb_t callback;
  lv_event_code_t filter;
  void* user_data;
  lv_event_dsc_t* next;
};

struct lv_obj_t {
  lv_obj_t* parent;
  lv_obj_t* first_child;
  lv_obj_t* next_sibling;
  lv_event_dsc_t* events;
  bool deleted;
  int32_t x;
  int32_t y;
  int32_t width;
  int32_t height;
  char text[128];
  const void* image_src;
  bool invalidated;
};

struct lv_event_t {
  lv_event_code_t code;
  lv_obj_t* current_target;
  void* user_data;
};

lv_obj_t* lv_screen_active(void);
lv_obj_t* lv_obj_create(lv_obj_t* parent);
lv_obj_t* lv_label_create(lv_obj_t* parent);
lv_obj_t* lv_button_create(lv_obj_t* parent);
lv_obj_t* lv_image_create(lv_obj_t* parent);
void lv_obj_delete(lv_obj_t* object);
void lv_obj_remove_style_all(lv_obj_t* object);
void lv_obj_set_pos(lv_obj_t* object, int32_t x, int32_t y);
void lv_obj_set_size(lv_obj_t* object, int32_t width, int32_t height);
void lv_obj_set_hidden(lv_obj_t* object, bool enabled);
void lv_obj_set_clickable(lv_obj_t* object, bool enabled);
void lv_obj_set_scrollable(lv_obj_t* object, bool enabled);
void lv_obj_center(lv_obj_t* object);
void lv_label_set_text(lv_obj_t* object, const char* text);
void lv_obj_set_style_bg_color(lv_obj_t* object, lv_color_t color, int32_t selector);
void lv_obj_set_style_bg_opa(lv_obj_t* object, lv_opa_t opacity, int32_t selector);
void lv_obj_set_style_text_color(lv_obj_t* object, lv_color_t color, int32_t selector);
void lv_obj_set_style_radius(lv_obj_t* object, int32_t radius, int32_t selector);
void lv_obj_set_style_border_color(lv_obj_t* object, lv_color_t color, int32_t selector);
void lv_obj_set_style_border_width(lv_obj_t* object, int32_t width, int32_t selector);
void lv_obj_set_style_opa(lv_obj_t* object, lv_opa_t opacity, int32_t selector);
void lv_obj_add_state(lv_obj_t* object, uint32_t state);
void lv_obj_remove_state(lv_obj_t* object, uint32_t state);
lv_color_t lv_color_hex(uint32_t color);
uint32_t lv_draw_buf_width_to_stride(uint32_t width, lv_color_format_t format);
void lv_image_set_src(lv_obj_t* object, const void* source);
void lv_image_set_inner_align(lv_obj_t* object, uint32_t align);
int32_t lv_obj_get_width(const lv_obj_t* object);
int32_t lv_obj_get_height(const lv_obj_t* object);
void lv_obj_invalidate(lv_obj_t* object);
void lv_obj_set_style_image_opa(lv_obj_t* object, lv_opa_t opacity,
                                int32_t selector);
void lv_obj_set_style_image_recolor(lv_obj_t* object, lv_color_t color,
                                    int32_t selector);
void lv_obj_set_style_image_recolor_opa(lv_obj_t* object, lv_opa_t opacity,
                                        int32_t selector);
lv_event_dsc_t* lv_obj_add_event_cb(lv_obj_t* object,
                                    lv_event_cb_t callback,
                                    lv_event_code_t filter,
                                    void* user_data);
void* lv_event_get_user_data(lv_event_t* event);
lv_event_code_t lv_event_get_code(lv_event_t* event);
