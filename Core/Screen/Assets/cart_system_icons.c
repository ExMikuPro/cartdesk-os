#include "cart_system_icons.h"

#include <stddef.h>

#include "cart_system_icons_data.h"

typedef struct
{
    const lv_image_dsc_t *source;
    const char *name;
} cart_system_icon_entry_t;

static const cart_system_icon_entry_t s_system_icons[CART_SYSTEM_ICON_COUNT] = {
    [CART_SYSTEM_ICON_GALLERY] = {&cart_system_icon_gallery_dsc, "library-photo"},
    [CART_SYSTEM_ICON_GAMEPAD] = {&cart_system_icon_gamepad_dsc, "device-gamepad-2"},
    [CART_SYSTEM_ICON_EXTENSIONS] = {&cart_system_icon_extensions_dsc, "puzzle"},
    [CART_SYSTEM_ICON_SETTINGS] = {&cart_system_icon_settings_dsc, "settings"},
    [CART_SYSTEM_ICON_SLEEP] = {&cart_system_icon_sleep_dsc, "moon"},
};

_Static_assert((sizeof(s_system_icons) / sizeof(s_system_icons[0])) == CART_SYSTEM_ICON_COUNT,
               "system icon table must match cart_system_icon_id_t");

const lv_image_dsc_t *CartSystemIcon_GetSource(cart_system_icon_id_t icon_id)
{
    if ((unsigned int)icon_id >= (unsigned int)CART_SYSTEM_ICON_COUNT) {
        return NULL;
    }

    return s_system_icons[icon_id].source;
}

const char *CartSystemIcon_GetDebugName(cart_system_icon_id_t icon_id)
{
    if ((unsigned int)icon_id >= (unsigned int)CART_SYSTEM_ICON_COUNT) {
        return NULL;
    }

    return s_system_icons[icon_id].name;
}
