#ifndef LAUNCHER_INPUT_H
#define LAUNCHER_INPUT_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LAUNCHER_INPUT_MAX_SLOTS 12u
#define LAUNCHER_INPUT_MAX_BUTTONS 5u

typedef struct {
    int16_t x;
    int16_t y;
    bool pressed;
    uint8_t touch_count;
    uint32_t timestamp_ms;
} launcher_pointer_sample_t;

typedef struct {
    int16_t x1;
    int16_t y1;
    int16_t x2;
    int16_t y2;
} launcher_input_rect_t;

typedef struct {
    int16_t center_x;
    int16_t center_y;
    int16_t radius;
} launcher_input_circle_t;

typedef struct {
    launcher_input_rect_t viewport;
    launcher_input_rect_t slots[LAUNCHER_INPUT_MAX_SLOTS];
    launcher_input_circle_t buttons[LAUNCHER_INPUT_MAX_BUTTONS];
    uint8_t slot_count;
    uint8_t button_count;
} launcher_hit_geometry_t;

typedef enum {
    LAUNCHER_INPUT_TARGET_NONE = 0,
    LAUNCHER_INPUT_TARGET_APP_SLOT,
    LAUNCHER_INPUT_TARGET_FIXED_BUTTON,
} launcher_input_target_kind_t;

typedef struct {
    launcher_input_target_kind_t kind;
    int8_t id;
} launcher_input_target_t;

typedef enum {
    LAUNCHER_INPUT_IDLE = 0,
    LAUNCHER_INPUT_PRESSED,
    LAUNCHER_INPUT_DRAGGING,
    LAUNCHER_INPUT_CANCELLED,
} launcher_input_state_t;

typedef struct {
    int32_t (*get_scroll_x)(void *user);
    void (*scroll_tick)(bool pressed, int32_t screen_x, uint32_t now_ms, void *user);
    void (*scroll_cancel)(void *user);
    void (*activate_slot)(uint8_t slot, void *user);
    void (*activate_button)(uint8_t button, void *user);
    void *user;
} launcher_input_ops_t;

typedef struct {
    launcher_hit_geometry_t geometry;
    launcher_input_ops_t ops;
    launcher_input_state_t state;
    launcher_input_target_t press_target;
    int16_t press_x;
    int16_t press_y;
    bool scroll_candidate;
    bool drag_latched;
    bool active_last_tick;
} launcher_input_t;

void launcher_input_init(launcher_input_t *input,
                         const launcher_hit_geometry_t *geometry,
                         const launcher_input_ops_t *ops);
void launcher_input_reset(launcher_input_t *input);
launcher_input_target_t launcher_input_hit_test(const launcher_input_t *input,
                                                int16_t screen_x,
                                                int16_t screen_y);
void launcher_input_process(launcher_input_t *input,
                            bool hw_pan_active,
                            const launcher_pointer_sample_t *sample);

#ifdef __cplusplus
}
#endif

#endif /* LAUNCHER_INPUT_H */
