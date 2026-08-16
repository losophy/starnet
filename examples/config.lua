--示例启动配置（对齐 skynet config.lua）
--用法：从 build/ 目录运行  ./starnet ../examples/config.lua
return {
    --服务搜索模板：;分隔多路径，?为服务名占位（对齐 skynet luaservice/LUA_SERVICE）
    luaservice = "../service/?/init.lua;../examples/?/init.lua",
    --启动服务名（对齐 skynet start）
    start = "main",
    --worker线程数（对齐 skynet thread）
    thread = 3,
}