--chat 服务：广播聊天（对齐原示例，socket 消息改为协程化 dispatch）
local starnet = require "starnet"

local serviceId
local conns = {}

starnet.start(function()
    serviceId = starnet.self()
    print("[lua] chat start id:"..serviceId)
    --裸数据模式：nc 等终端直接发文本即可，无需 netpack 2 字节长度头
    starnet.socket.rawdata()
    starnet.socket.listen(8002, serviceId)
end)

starnet.dispatch("accept", function(clientfd, listenfd)
    print("[lua] chat accept "..clientfd.." from "..listenfd)
    conns[clientfd] = true
end)

starnet.dispatch("socket", function(fd, msg)
    print("[lua] chat socket data "..fd.." len:"..#msg)
    --广播：裸文本原样转发（对齐聊天室逐字节协议，nc 直接可读）
    for cfd, _ in pairs(conns) do
        starnet.socket.write(cfd, msg)
    end
end)

starnet.dispatch("close", function(fd)
    print("[lua] chat close "..fd)
    conns[fd] = nil
end)