--udp 服务：UDP echo（收什么回什么，演示 starnet.udp 监听 + dispatch("udp") + starnet.send_udp 回包）
local starnet = require "starnet"

starnet.start(function()
    print("[lua] udp start id:"..starnet.self())
    --绑定 8003 端口收 UDP 包
    local fd = starnet.udp("0.0.0.0", 8003)
    if fd >= 0 then
        print("[lua] udp bind :8003 fd:"..fd)
    else
        print("[lua] udp bind fail")
    end
end)

starnet.dispatch("udp", function(fd, msg, addr, port)
    print("[lua] udp recv fd:"..fd.." from "..addr..":"..port.." len:"..#msg)
    --echo：回给发件人
    starnet.send_udp(fd, msg, addr, port)
end)