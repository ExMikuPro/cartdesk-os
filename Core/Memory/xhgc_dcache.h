#ifndef XHGC_DCACHE_H
#define XHGC_DCACHE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void xhgc_dcache_clean_range(const void *ptr, size_t size);
void xhgc_dcache_invalidate_range(void *ptr, size_t size);
void xhgc_dcache_clean_invalidate_range(void *ptr, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* XHGC_DCACHE_H */
