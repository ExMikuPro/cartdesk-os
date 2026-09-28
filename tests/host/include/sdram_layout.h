#pragma once

#include <stdint.h>

extern uint8_t g_host_resource_arena[];

#define RESOURCE_ARENA_BASE ((uintptr_t)g_host_resource_arena)
#define RESOURCE_ARENA_SIZE (2u * 1024u * 1024u)
