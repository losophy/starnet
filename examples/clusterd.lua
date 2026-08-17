--clusterd 集群服务（对齐 skynet clusterd/clustersender/clusteragent 的简化合并版）
--每节点一个，统一管理：
--  出站：node -> 连接（connect 后走写缓冲排队），RPC 用 session 匹配 + 协程等待响应
--  入站：socket.listen 监听，accept 后收包 -> 本地调用 -> 写回响应
--协议（外层 netpack 2 字节大端长度头）：
--  请求：pack( >s2i4s4, addr, session, payload )   addr="" 为名字查询(payload=name)
--  响应：pack( >i4i1s4, session, ok, data )
--  session=0 表示 push（无需响应），>0 表示 call（需响应）

local starnet = require "starnet"
local socket = starnet.socket
local pack = starnet.pack
local coroutine_yield = coroutine.yield

--节点配置：node -> "host:port"（env cluster 扁平键，逗号分隔）
local node_address = {}
--出站连接：node -> fd
local node_fd = {}
--入站连接：fd -> true
local in_fd = {}
--本节点公开服务名：name -> handle / handle -> name（双向）
local register_name = {}
local register_addr = {}
--出站 RPC 响应等待：session -> 协程
local session_cb = {}
local out_session = 0

--协议编解码（网络序大端）
local function pack_request(addr, session, payload)
    return pack(string.pack(">s2i4s4", addr, session, payload))
end
local function unpack_request(buff)
    return string.unpack(">s2i4s4", buff)
end
local function pack_response(session, ok, data)
    return pack(string.pack(">i4i1s4", session, ok and 1 or 0, data or ""))
end
local function unpack_response(buff)
    local session, ok, data = string.unpack(">i4i1s4", buff)
    return session, ok == 1, data
end

--解析 cluster 配置字符串："nodeB=127.0.0.1:8002,nodeC=..." -> 表
local function parse_cluster_config(cfg)
    local t = {}
    if not cfg then
        return t
    end
    for item in cfg:gmatch("[^,]+") do
        local node, addr = item:match("^%s*(.-)%s*=%s*(.-)%s*$")
        if node and addr then
            t[node] = addr
        end
    end
    return t
end

--取某节点的出站 fd（无则 connect；连接建立前 write 走写缓冲排队，对齐 starnet 写缓冲）
local function get_fd(node)
    local fd = node_fd[node]
    if fd then
        return fd
    end
    local addr = node_address[node]
    if not addr then
        return nil, "cluster node " .. node .. " absent"
    end
    local host, port = addr:match("^(.+):(%d+)$")
    if not host then
        return nil, "invalid cluster node address: " .. addr
    end
    fd = socket.connect(host, tonumber(port))
    if fd < 0 then
        return nil, "connect to cluster node " .. node .. " failed"
    end
    node_fd[node] = fd
    return fd
end

--出站请求（need_response=true 时挂起协程等待响应，session 匹配唤醒）
local function request(node, addr, payload, need_response)
    local fd, err = get_fd(node)
    if not fd then
        error(err)
    end
    local session = 0
    if need_response then
        out_session = out_session + 1
        session = out_session
    end
    socket.write(fd, pack_request(addr, session, payload))
    if session == 0 then
        return true, ""
    end
    session_cb[session] = coroutine.running()
    local ok, resp_ok, resp = coroutine_yield("SUSPEND")
    if not ok then
        error("cluster call failed")
    end
    return resp_ok, resp
end

--入站请求处理（本地调用并回包）
local function handle_request(fd, addr, session, payload)
    local handle
    if addr == "" then
        --名字查询：payload=name
        local h = register_name[payload]
        socket.write(fd, pack_response(session, h ~= nil, h and tostring(h) or "name not found"))
        return
    elseif addr:sub(1, 1) == "@" then
        handle = register_name[addr:sub(2)]
    else
        handle = tonumber(addr)
    end
    if not handle then
        socket.write(fd, pack_response(session, false, "invalid address: " .. addr))
        return
    end
    if session == 0 then
        --push：仅发送，不回包
        starnet.send(handle, "lua", payload)
    else
        --call：本地调用（协程等待业务 ret）并回包
        local ok, data = pcall(starnet.call, handle, "lua", payload)
        socket.write(fd, pack_response(session, ok, ok and data or tostring(data)))
    end
end

--socket 数据：入站=请求，出站=响应
starnet.dispatch("socket", function(fd, msg)
    if in_fd[fd] then
        local addr, session, payload = unpack_request(msg)
        handle_request(fd, addr, session, payload)
    else
        local session, ok, data = unpack_response(msg)
        local co = session_cb[session]
        if co then
            session_cb[session] = nil
            starnet.wakeup(co, true, ok, data)
        end
    end
end)

--入站新连接
starnet.dispatch("accept", function(fd, listenfd)
    in_fd[fd] = true
end)

--连接错误/关闭：清除出站或入站状态
local function drop_node(fd)
    for node, nfd in pairs(node_fd) do
        if nfd == fd then
            node_fd[node] = nil
            starnet.log("cluster connection to " .. node .. " closed")
            return
        end
    end
    in_fd[fd] = nil
end

starnet.dispatch("error", function(fd, err)
    drop_node(fd)
end)

starnet.dispatch("close", function(fd)
    drop_node(fd)
end)

--命令消息解包（对应 cluster.lua 的 pack_message：>i1 数量 + 每参数 >s4）
local function unpack_message(buff)
    local n, pos = string.unpack(">i1", buff)
    local args = {}
    for i = 1, n do
        local s
        s, pos = string.unpack(">s4", buff, pos)
        args[i] = s
    end
    return args
end

--命令（由 cluster.lua 通过 lua 协议调用）
local command = {}

function command.listen(_, port)
    local fd = socket.listen(tonumber(port), starnet.self())
    if fd < 0 then
        error("cluster listen fail")
    end
    starnet.log("cluster listening on " .. tostring(port))
    return starnet.ret(true)
end

function command.call(_, node, addr, payload)
    local ok, resp_ok, resp = pcall(request, node, addr, payload, true)
    if not ok then
        starnet.log("cluster call " .. node .. " fail: " .. tostring(resp_ok))
        return starnet.ret(nil)
    end
    if not resp_ok then
        starnet.log("cluster call " .. node .. " error: " .. tostring(resp))
        return starnet.ret(nil)
    end
    return starnet.ret(resp)
end

function command.send(_, node, addr, payload)
    local ok, err = pcall(request, node, addr, payload, false)
    if not ok then
        starnet.log("cluster send " .. node .. " fail: " .. tostring(err))
    end
    return starnet.ret(true)
end

function command.query(_, node, name)
    local ok, resp_ok, resp = pcall(request, node, "", name, true)
    if not ok or not resp_ok then
        return starnet.ret(nil)
    end
    return starnet.ret(tonumber(resp))
end

function command.register(source, name, addr)
    addr = addr or source
    if register_addr[addr] then
        register_name[register_addr[addr]] = nil
    end
    register_name[name] = addr
    register_addr[addr] = name
    starnet.log("cluster register [" .. name .. "] :" .. tostring(addr))
    return starnet.ret(true)
end

function command.unregister(_, name)
    local addr = register_name[name]
    if addr then
        register_name[name] = nil
        register_addr[addr] = nil
    end
    return starnet.ret(true)
end

function command.reload(_, cfg)
    node_address = parse_cluster_config(cfg)
    starnet.log("cluster config reloaded")
    return starnet.ret(true)
end

starnet.start(function()
    node_address = parse_cluster_config(starnet.getenv("cluster"))
    starnet.dispatch("lua", function(session, source, buff)
        local args = unpack_message(buff)
        local cmd = table.remove(args, 1)
        local f = assert(command[cmd], "clusterd invalid command " .. tostring(cmd))
        f(source, table.unpack(args))
    end)
end)