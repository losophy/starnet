--chat 服务：广播聊天（对齐原示例，socket 消息改为协程化 dispatch）
local starnet = require "starnet"

local serviceId
local conns = {}

starnet.start(function()
    serviceId = starnet.self()
    print("[lua] chat start id:"..serviceId)
    starnet.Listen(8002, serviceId)
end)

starnet.dispatch("accept", function(clientfd, listenfd)
    print("[lua] chat accept "..clientfd.." from "..listenfd)
    conns[clientfd] = true
end)

starnet.dispatch("socket", function(fd, buff, len)
    print("[lua] chat socket data "..fd.." len:"..len)
    for cfd, _ in pairs(conns) do
        starnet.Write(cfd, buff)
    end
end)

starnet.dispatch("close", function(fd)
    print("[lua] chat close "..fd)
    conns[fd] = nil
end)