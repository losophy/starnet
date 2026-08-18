--benchmark 回显从服务（对齐 skynet test/testecho.lua 的 slave）：收到即回
local starnet = require "starnet"

starnet.start(function()
    starnet.dispatch("lua", function(session, source, msg)
        starnet.ret(msg)
    end)
end)