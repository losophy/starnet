--starnet 集群客户端库（对齐 skynet.cluster：跨节点 RPC）
--clusterd 服务懒启动（本节点唯一，注册本地名 .clusterd）
--跨节点消息走显式 API（cluster.call/send），不劫持 starnet.send
--节点配置：config.lua 的扁平键 cluster = "nodeB=127.0.0.1:8002,nodeC=..."（clusterd 读 env）
--命令消息自打包（"lua" 协议 pack_string 只传单参数，多个命令参数用 s4 长度前缀串联）

local starnet = require "starnet"

local cluster = {}
local clusterd

--打包命令参数为一个字符串（>i1 数量 + 每参数 >s4 长度前缀）
local function pack_message(...)
    local n = select("#", ...)
    local parts = { string.pack(">i1", n) }
    for i = 1, n do
        parts[i + 1] = string.pack(">s4", select(i, ...))
    end
    return table.concat(parts)
end

local function get_clusterd()
    if clusterd then
        return clusterd
    end
    clusterd = starnet.localname(".clusterd")
    if clusterd then
        return clusterd
    end
    clusterd = starnet.newservice("clusterd")
    starnet.name(".clusterd", clusterd)
    return clusterd
end

--开启入站监听（本节点对外开放 cluster 端口，对齐 skynet.cluster.open）
function cluster.open(port)
    return starnet.call(get_clusterd(), "lua", pack_message("listen", port))
end

--跨节点请求-应答（payload 为字符串，业务自行打包；返回远端响应字符串，失败返回 nil）
function cluster.call(node, addr, payload)
    return starnet.call(get_clusterd(), "lua", pack_message("call", node, addr, payload))
end

--跨节点发送（无需响应，对齐 skynet.cluster.send）
function cluster.send(node, addr, payload)
    return starnet.send(get_clusterd(), "lua", pack_message("send", node, addr, payload))
end

--查询远端节点注册的服务名（返回 handle，未注册返回 nil）
function cluster.query(node, name)
    return starnet.call(get_clusterd(), "lua", pack_message("query", node, name))
end

--本节点注册公开服务名（addr 缺省为当前服务 handle，对齐 skynet.cluster.register）
function cluster.register(name, addr)
    if addr then
        return starnet.call(get_clusterd(), "lua", pack_message("register", name, addr))
    end
    return starnet.call(get_clusterd(), "lua", pack_message("register", name))
end

function cluster.unregister(name)
    return starnet.call(get_clusterd(), "lua", pack_message("unregister", name))
end

--热更节点配置（格式同 env cluster：node=host:port 逗号分隔）
function cluster.reload(cfg)
    return starnet.call(get_clusterd(), "lua", pack_message("reload", cfg))
end

return cluster