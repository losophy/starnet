--ping 服务：演示 RPC 请求-应答（数值累加，对齐原示例逻辑）
local starnet = require "starnet"

starnet.start(function()
    print("[lua] ping start id:"..starnet.self())
end)

starnet.dispatch("lua", function(session, source, buff)
    print("[lua] ping recv session:"..session.." from:"..source.." buff:"..buff)
    if buff == "start" then
        return
    end
    local n1, n2 = string.unpack("i4 i4", buff)
    n1 = n1 + 1
    n2 = n2 + 2
    starnet.ret(string.pack("i4 i4", n1, n2))
end)