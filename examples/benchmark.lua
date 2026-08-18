--引擎核心性能基准（对齐 skynet test/testecho.lua）：
--进程内 RPC ping-pong，测 消息队列 + worker 分发 + 协程 RPC 的吞吐
--用法：cd build && ./starnet ../examples/config_bench.lua
local starnet = require "starnet"

starnet.start(function()
    local slave = starnet.newservice("benchmark_echo")
    starnet.log("benchmark slave = " .. tostring(slave))

    local N = 100000

    --串行：单个协程依次 call
    local start = starnet.now()
    for i = 1, N do
        starnet.call(slave, "lua")
    end
    local seq_cs = starnet.now() - start
    starnet.log(string.format("sequential: %d calls, %d cs, qps = %.0f", N, seq_cs, N / seq_cs * 100))

    --并行：workers 个协程同时压（对齐 testecho 的 worker 并行）
    local workers = 10
    local task = N / workers
    local remaining = workers
    start = starnet.now()
    for i = 1, workers do
        starnet.fork(function()
            for j = 1, task do
                starnet.call(slave, "lua")
            end
            remaining = remaining - 1
            if remaining == 0 then
                local cs = starnet.now() - start
                starnet.log(string.format("parallel (workers=%d): %d calls, %d cs, qps = %.0f",
                    workers, N, cs, N / cs * 100))
            end
        end)
    end
    --踢一脚：fork 队列在 dispatch_message 末尾排空，sleep(0) 触发一次消息处理把并行协程跑起来
    starnet.sleep(0)

    --统计交叉验证：本服务处理的消息数 ≈ 2*N（每次 call 一请求一响应），cpu 反映调度开销
    local stat = starnet.command("STAT")
    starnet.log(string.format("self STAT: message=%d cpu=%.4fs mem=%.1fKB",
        stat.message, stat.cpu, starnet.mem()))
end)