#ifndef SDRAM_COLD_POOL_H
#define SDRAM_COLD_POOL_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the SDRAM cold pool allocator.
 *
 * Must be called after FMC/SDRAM initialization and before any cold allocation.
 */
void cold_pool_init(void);

/**
 * @brief Reset all cold pool allocations.
 *
 * Existing pointers into the cold pool become invalid after this call.
 */
void cold_pool_reset(void);

/**
 * @brief Allocate from the SDRAM cold pool.
 *
 * The allocator is linear, does not call malloc/free, and raises alignments
 * smaller than 32 bytes to 32 bytes. Returns NULL on invalid alignment,
 * zero-size requests, or pool exhaustion.
 */
void *cold_alloc(size_t size, size_t align);

/**
 * @brief Allocate and zero memory from the SDRAM cold pool.
 *
 * Returns NULL if count * size overflows or if the allocation cannot fit.
 */
void *cold_calloc(size_t count, size_t size, size_t align);

/**
 * @brief Return bytes currently consumed by the cold pool.
 */
size_t cold_pool_used(void);

/**
 * @brief Return bytes still available in the cold pool.
 */
size_t cold_pool_free(void);

#ifdef __cplusplus
}
#endif

#endif /* SDRAM_COLD_POOL_H */
