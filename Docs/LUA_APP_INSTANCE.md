# CartDesk Lua 应用实例

正式设计、API、所有权和错误规则统一见 [LUA_FOUNDATION_API.md](LUA_FOUNDATION_API.md)。

宿主在 `init(self)` 前创建 `state`、`ui`、`assets`、`timers`、`services` 五个独立
table。真实 UI、资源和 timer 所有权由 C registry 管理，应用清空这些 table 不会
绕过退出清理。生命周期保持 `init`、`fixed_update`、`update`、`late_update`、
`on_input`、`on_message`、`on_reload`、`final`。

验证命令：

```bash
build/host_tools/bin/luavm --self-test
build/host_tools/bin/lua_ui_owner_test
build/host_tools/bin/lua_crc_test
build/host_tools/bin/lua_runtime_task_test
sh tests/lua/style_contract_lint.sh
```
