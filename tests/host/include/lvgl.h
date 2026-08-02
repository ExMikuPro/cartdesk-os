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
void lv_obj_delete(lv_obj_t* object);
void lv_obj_remove_style_all(lv_obj_t* object);
void lv_obj_set_pos(lv_obj_t* object, int32_t x, int32_t y);
void lv_obj_set_size(lv_obj_t* object, int32_t width, int32_t height);
void lv_obj_remove_flag(lv_obj_t* object, uint32_t flags);
void lv_obj_add_flag(lv_obj_t* object, uint32_t flags);
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
lv_event_dsc_t* lv_obj_add_event_cb(lv_obj_t* object,
                                    lv_event_cb_t callback,
                                    lv_event_code_t filter,
                                    void* user_data);
void* lv_event_get_user_data(lv_event_t* event);
lv_event_code_t lv_event_get_code(lv_event_t* event);
