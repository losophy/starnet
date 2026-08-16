--main 服务：启动子服务，演示 RPC call / sleep / 定时器心跳
local starnet = require "starnet"

starnet.start(function()
    print("[lua] main start id:"..starnet.self())
    --演示配置系统：starnet.getenv 查询 config 键（对齐 skynet.getenv）
    starnet.log("config test_config = "..tostring(starnet.getenv("test_config")))
    starnet.log("config harbor = "..tostring(starnet.getenv("harbor")))
    --启动子服务
    starnet.NewService("chat")
    local ping = starnet.NewService("ping")
    local db = starnet.NewService("db")
    --名字服务：注册本地名（对齐 skynet.name），并用名字解析（starnet.localname）
    starnet.name(".ping", ping)
    starnet.name(".db", db)
    print("[lua] main localname .ping="..tostring(starnet.localname(".ping")).." .db="..tostring(starnet.localname(".db")))
    --演示 RPC call：ping（请求-应答，参数用 string.pack 编码，对齐原示例）
    local n1, n2 = string.unpack("i4 i4", starnet.call(".ping", "lua", string.pack("i4 i4", 1, 2)))
    print("[lua] main call ping result n1:"..n1.." n2:"..n2)
    --演示 RPC call：db（用名字地址）
    local r = starnet.call(".db", "lua", "hello")
    print("[lua] main call db result:"..r)
    --演示 sleep（协程挂起 200 centisecond）
    starnet.sleep(200)
    print("[lua] main wake up after sleep")
    --心跳：每 1 秒（回调式，对齐 skynet.timeout）
    local function heartbeat()
        print("[lua] main heartbeat")
        starnet.timeout(100, heartbeat)
    end
    starnet.timeout(100, heartbeat)
end)

starnet.dispatch("lua", function(session, source, buff)
    print("[lua] main recv session:"..session.." from:"..source.." buff:"..buff)
end)