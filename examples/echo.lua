--TCP echo 服务（性能压测用）：收到什么回什么，rawdata 裸字节流
--端口取 env echo_port，默认 8004
local starnet = require "starnet"

starnet.start(function()
    local port = tonumber(starnet.getenv("echo_port")) or 8004
    starnet.socket.rawdata()
    starnet.socket.listen(port, starnet.self())
    starnet.log("echo listening on " .. port)
end)

starnet.dispatch("socket", function(fd, msg)
    starnet.socket.write(fd, msg)
end)