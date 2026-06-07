#ifndef LUAVM_PLATFORM_H
#define LUAVM_PLATFORM_H

#include <stdio.h>

#include "lua.h"

FILE *luavm_platform_open_binary_write(const char *path);

#endif /* LUAVM_PLATFORM_H */
