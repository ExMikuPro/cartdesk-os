#pragma once

#include "lvgl.h"

/** Launcher system-entry icon identifiers. */
typedef enum
{
    CART_SYSTEM_ICON_GALLERY = 0,
    CART_SYSTEM_ICON_GAMEPAD,
    CART_SYSTEM_ICON_EXTENSIONS,
    CART_SYSTEM_ICON_SETTINGS,
    CART_SYSTEM_ICON_SLEEP,

    CART_SYSTEM_ICON_COUNT
} cart_system_icon_id_t;

/**
 * Return the immutable LVGL image source for an icon.
 *
 * Invalid identifiers, including CART_SYSTEM_ICON_COUNT, return NULL.
 */
const lv_image_dsc_t *CartSystemIcon_GetSource(cart_system_icon_id_t icon_id);

/** Return the Tabler icon name, or NULL for an invalid identifier. */
const char *CartSystemIcon_GetDebugName(cart_system_icon_id_t icon_id);
