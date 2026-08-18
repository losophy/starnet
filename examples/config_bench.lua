--性能基准配置（对齐 skynet 标准 thread=8）
--用法：cd build && ./starnet ../examples/config_bench.lua
--注：profile=true 开启每消息 CPU 统计（用于 STAT 交叉验证），但每条消息多 ~2 次 clock 调用；
--    纯吞吐对比可临时改 profile=false 再跑一次。
return {
    luaservice = "../service/?/init.lua;../examples/?.lua",
    start = "benchmark",
    thread = 8,
    luaPath = "../lualib/?.lua",
    logger = "",
    harbor = 0,
    daemon = false,
    profile = true,
}