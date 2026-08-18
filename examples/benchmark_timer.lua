--定时器精度基准：并发 sleep(1)（10ms）统计实际延迟分布
--用法：cd build && ./starnet ../examples/config_bench.lua（临时改 start=benchmark_timer）
local starnet = require "starnet"

starnet.start(function()
    local COUNT = 1000
    local TARGET = 1  -- centisecond（10ms）
    local delays = {}
    local remaining = COUNT
    local t0 = starnet.now()
    for i = 1, COUNT do
        starnet.fork(function()
            local s = starnet.now()
            starnet.sleep(TARGET)
            delays[#delays + 1] = starnet.now() - s
            remaining = remaining - 1
            if remaining == 0 then
                local sum, maxd = 0, 0
                for _, d in ipairs(delays) do
                    sum = sum + d
                    if d > maxd then maxd = d end
                end
                local avg = sum / #delays
                starnet.log(string.format("timer: %d sleep(%d), avg=%.2fcs max=%dcs wall=%dcs",
                    COUNT, TARGET, avg, maxd, starnet.now() - t0))
            end
        end)
    end
    starnet.sleep(0)  -- 踢 fork 队列
end)