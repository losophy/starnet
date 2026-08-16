# service/

框架**官方服务**目录（对齐 skynet 的 `service/`，如 `bootstrap.lua`、`launcher.lua`、`gate.lua` 等）。

当前为空——starnet 尚无框架自带服务。启动入口由 C++ 侧（`starnet_main.cpp`）直接 `NewService("main")` 拉起，`main` 等演示服务放在 `../examples/`。

以后框架自身的系统服务（logger、console、gate 等）应放在此目录。

服务加载顺序由 config 的 `luaservice` 模板决定：先查 `../service/<type>/init.lua`（官方目录，目录式），再查 `../examples/<type>.lua`（示例单文件，见 `starnet_config.cpp`）。