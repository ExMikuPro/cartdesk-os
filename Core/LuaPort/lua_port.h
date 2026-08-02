#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 保留空配置结构，便于宿主初始化入口稳定；Lua Foundation API 不暴露 HAL。 */
typedef struct { uint8_t reserved; } lua_port_config_t;

struct lua_State;

/**
 * 注册只读的 CartDesk Lua Foundation 模块：ui、assets、storage、timer、
 * system、random、log 和 crc。
 */
void lua_port_bind(struct lua_State* L, const lua_port_config_t* cfg);

#ifdef __cplusplus
}
#endif
