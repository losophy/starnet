--cluster2 节点配置（集群演示）
--用法：从 build/ 目录运行  ./starnet ../examples/config_cluster2.lua
return {
    --服务搜索模板：;分隔多路径，?为服务名占位（对齐 skynet luaservice/LUA_SERVICE）
    luaservice = "../service/?/init.lua;../examples/?.lua",
    --启动服务名（对齐 skynet start）
    start = "cluster2",
    --worker线程数（对齐 skynet thread）
    thread = 8,
    --Lua模块搜索路径（lualib 宿主库，对齐 skynet lua_path）
    luaPath = "../lualib/?.lua",
    --日志输出文件（对齐 skynet logger；不配置或空串则写 stderr）
    logger = "",
    --对齐 skynet config 常见键
    harbor = 0,
    --集群节点配置（clusterd 读 env：node=host:port 逗号分隔）
    cluster = "nodeA=127.0.0.1:8001",
}