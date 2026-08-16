--db 服务：演示 RPC 请求-应答（starnet.ret 回包）
local starnet = require "starnet"

starnet.start(function()
    print("[lua] db start id:"..starnet.self())
end)

starnet.dispatch("lua", function(session, source, buff)
    print("[lua] db recv session:"..session.." from:"..source.." buff:"..buff)
    --回包：prefix + 原内容
    starnet.ret("pong:"..buff)
end)