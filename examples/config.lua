--示例启动配置（对齐 skynet config.lua）
--用法：从 build/ 目录运行  ./starnet ../examples/config.lua
return {
    --服务搜索模板：;分隔多路径，?为服务名占位（对齐 skynet luaservice/LUA_SERVICE；examples 为单文件 *.lua）
    luaservice = "../service/?/init.lua;../examples/?.lua",
    --启动服务名（对齐 skynet start）
    start = "main",
    --worker线程数（对齐 skynet thread；取 8 以覆盖 weight 表前 4 个 -1 与 5~8 个 0）
    thread = 8,
    --Lua模块搜索路径（lualib 宿主库，对齐 skynet lua_path）
    luaPath = "../lualib/?.lua",
    --日志输出文件（对齐 skynet logger；不配置或空串则写 stderr）
    logger = "",
    --对齐 skynet config 常见键（运行时可 starnet.getenv 查询）
    harbor = 0,
    daemon = false,
    profile = false,
    --演示自定义配置
    test_config = "hello from config",
}