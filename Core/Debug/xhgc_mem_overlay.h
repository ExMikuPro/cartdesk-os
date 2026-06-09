#ifndef XHGC_MEM_OVERLAY_H
#define XHGC_MEM_OVERLAY_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void xhgc_mem_overlay_init(void);
void xhgc_mem_overlay_set_visible(bool visible);
void xhgc_mem_overlay_toggle(void);
bool xhgc_mem_overlay_is_visible(void);
void xhgc_mem_overlay_update(void);

#ifdef __cplusplus
}
#endif

#endif /* XHGC_MEM_OVERLAY_H */
