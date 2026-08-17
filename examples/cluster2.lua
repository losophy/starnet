--cluster2 节点：监听 8002，注册服务 hello2，跨服调用 cluster1 的 hello
local starnet = require "starnet"
local cluster = require "starnet.cluster"

starnet.start(function()
    cluster.open(8002)
    cluster.register("hello2")
    starnet.log("cluster2 listening on 8002, registered [hello2]")
    --稍候让 cluster1 先启动（连接失败会导致 call 挂起，先启动的对端稍后再调）
    starnet.sleep(200)
    --跨服 RPC：调 cluster1 的 hello
    local r = cluster.call("nodeA", "@hello", "ping from cluster2")
    starnet.log("cluster2 call nodeA.@hello -> "..tostring(r))
end)

--本节点公开服务：被远端调用时回包
starnet.dispatch("lua", function(session, source, buff)
    starnet.log("cluster2 recv ["..buff.."] from "..tostring(source))
    starnet.ret("pong from cluster2")
end)