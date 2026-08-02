#include "lua_app_instance.h"

#include <stddef.h>

bool LuaAppInstance_CreateDefaultTables(lua_State* L, int self_index) {
  static const char* const fields[] = {
      "state", "ui", "assets", "timers", "services",
  };
  if (!L || !lua_istable(L, self_index)) return false;
  self_index = lua_absindex(L, self_index);
  for (size_t i = 0; i < sizeof(fields) / sizeof(fields[0]); ++i) {
    lua_newtable(L);
    lua_setfield(L, self_index, fields[i]);
  }
  return true;
}
