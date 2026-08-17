--cluster1 节点：监听 8001，注册服务 hello，跨服调用 cluster2 的 hello2
local starnet = require "starnet"
local cluster = require "starnet.cluster"

starnet.start(function()
    --对外开放 cluster 端口（入站）
    cluster.open(8001)
    --注册本节点公开服务（默认注册当前服务）
    cluster.register("hello")
    starnet.log("cluster1 listening on 8001, registered [hello]")
    --稍候让 cluster2 先启动（连接建立前消息走写缓冲排队，连接失败会丢失并告警）
    starnet.sleep(200)
    --跨服 RPC：调 cluster2 的 hello2（@名字由对端 clusterd 解析）
    local r = cluster.call("nodeB", "@hello2", "ping from cluster1")
    starnet.log("cluster1 call nodeB.@hello2 -> "..tostring(r))
    --名字查询
    local h = cluster.query("nodeB", "hello2")
    starnet.log("cluster1 query nodeB.hello2 -> "..tostring(h))
end)

--本节点公开服务：被远端调用时回包
starnet.dispatch("lua", function(session, source, buff)
    starnet.log("cluster1 recv ["..buff.."] from "..tostring(source))
    starnet.ret("pong from cluster1")
end)