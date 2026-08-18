--TCP echo 压测配置：start=echo，监听 8004（可 env echo_port 覆盖）
return {
    luaservice = "../service/?/init.lua;../examples/?.lua",
    start = "echo",
    thread = 8,
    luaPath = "../lualib/?.lua",
    logger = "",
    harbor = 0,
    daemon = false,
    profile = false,
}