print("run lua init.lua")

local serviceId

function OnInit(id)
    serviceId = id
    starnet.NewService("chat")
    --每1秒（100 centisecond）心跳，演示定时器
    starnet.timeout(serviceId, 100, 1)
end

function OnTimeout(session)
    print("[lua] main OnTimeout session:"..session)
    --续订下一跳
    starnet.timeout(serviceId, 100, session + 1)
end

function OnExit()
    print("[lua] main OnExit")
end