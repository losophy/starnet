--starnet Lua宿主库（对齐 skynet.lua 核心子集）
--每条消息在独立协程中处理（starnet.dispatch）；session 匹配 RPC 请求/响应

local c = _G.starnet  -- C 绑定表（lua-starnet.cpp 注册）
local netpack = c.netpack  -- 网络封包（粘包/半包解析，对齐 skynet netpack）

local coroutine = coroutine
local table = table
local assert = assert
local error = error
local pcall = pcall
local tremove = table.remove
local tpack = table.pack
local tunpack = table.unpack

local cresume = coroutine.resume
local running_thread = nil

local function coroutine_resume(co, ...)
    running_thread = co
    return cresume(co, ...)
end
local coroutine_yield = coroutine.yield
local coroutine_create = coroutine.create

--消息类型（对齐 skynet.h 的 PTYPE_*，见 starnet.PTYPE_*）
local PTYPE_TEXT = 0
local PTYPE_RESPONSE = 1
local PTYPE_MULTICAST = 2
local PTYPE_CLIENT = 3
local PTYPE_SYSTEM = 4
local PTYPE_HARBOR = 5
local PTYPE_SOCKET = 6
local PTYPE_ERROR = 7
local PTYPE_QUEUE = 8
local PTYPE_DEBUG = 9
local PTYPE_LUA = 10
local PTYPE_SNAX = 11

--socket 子类型（对齐 socket_server.h 的 SKYNET_SOCKET_TYPE_*）
local SKYNET_SOCKET_TYPE_DATA = 1
local SKYNET_SOCKET_TYPE_CONNECT = 2
local SKYNET_SOCKET_TYPE_CLOSE = 3
local SKYNET_SOCKET_TYPE_ACCEPT = 4
local SKYNET_SOCKET_TYPE_ERROR = 5
local SKYNET_SOCKET_TYPE_UDP = 6
local SKYNET_SOCKET_TYPE_WARNING = 7

local starnet = {}

--暴露协议类型常量（对齐 skynet.lua 的 starnet.PTYPE_*）
starnet.PTYPE_TEXT = PTYPE_TEXT
starnet.PTYPE_RESPONSE = PTYPE_RESPONSE
starnet.PTYPE_MULTICAST = PTYPE_MULTICAST
starnet.PTYPE_CLIENT = PTYPE_CLIENT
starnet.PTYPE_SYSTEM = PTYPE_SYSTEM
starnet.PTYPE_HARBOR = PTYPE_HARBOR
starnet.PTYPE_SOCKET = PTYPE_SOCKET
starnet.PTYPE_ERROR = PTYPE_ERROR
starnet.PTYPE_QUEUE = PTYPE_QUEUE
starnet.PTYPE_DEBUG = PTYPE_DEBUG
starnet.PTYPE_LUA = PTYPE_LUA
starnet.PTYPE_SNAX = PTYPE_SNAX

--协议表（对齐 skynet register_protocol + proto：name 与 id 双索引）
local proto = {}

local function pack_string(...) return ... end
local function unpack_string(...) return ... end

function starnet.register_protocol(class)
    local name = class.name
    local id = class.id
    assert(type(name) == "string", "register_protocol name must be string")
    if proto[name] ~= nil then
        error("register_protocol conflict name: " .. tostring(name))
    end
    if id then
        assert(type(id) == "number" and id >= 0 and id <= 255, "register_protocol invalid id: " .. tostring(id))
        if proto[id] ~= nil then
            error("register_protocol conflict id: " .. tostring(id))
        end
        proto[id] = class
    end
    proto[name] = class
end

--内置协议（lua/socket 对齐 skynet 的 PTYPE_LUA / PTYPE_SOCKET）
starnet.register_protocol({ name = "lua", id = PTYPE_LUA, pack = pack_string, unpack = unpack_string })
starnet.register_protocol({ name = "socket", id = PTYPE_SOCKET, pack = pack_string, unpack = unpack_string })
starnet.register_protocol({ name = "accept", pack = pack_string, unpack = unpack_string })
starnet.register_protocol({ name = "close", pack = pack_string, unpack = unpack_string })
starnet.register_protocol({ name = "udp", pack = pack_string, unpack = unpack_string })
starnet.register_protocol({ name = "connect", pack = pack_string, unpack = unpack_string })
starnet.register_protocol({ name = "error", pack = pack_string, unpack = unpack_string })
starnet.register_protocol({ name = "warning", pack = pack_string, unpack = unpack_string })

--协程池
local coroutine_pool = {}

--session 映射（对齐 skynet.lua）
local session_id_coroutine = {}       -- session -> co（等待恢复的协程）
local session_coroutine_id = {}       -- co -> session（当前协程上下文）
local session_coroutine_address = {}  -- co -> source（当前协程上下文）

local fork_queue = { h = 1, t = 0 }

--网络封包解析队列（每服务一个，跨消息累积半包）
local netpack_queue = netpack.create()

--从协程池创建/复用协程（对齐 skynet.lua co_create）
local function co_create(f)
    local co = tremove(coroutine_pool)
    if co == nil then
        co = coroutine_create(function(...)
            f(...)
            while true do
                local address = session_coroutine_address[co]
                if address then
                    session_coroutine_id[co] = nil
                    session_coroutine_address[co] = nil
                end
                --回收协程，等待新主函数
                f = nil
                coroutine_pool[#coroutine_pool+1] = co
                f = coroutine_yield("SUSPEND")
                f(coroutine_yield())
            end
        end)
    else
        --把新主函数传给池协程，并恢复 running_thread
        local running = running_thread
        coroutine_resume(co, f)
        running_thread = running
    end
    return co
end

--协程调度状态机（对齐 skynet.lua suspend）
local function suspend(co, result, command)
    if not result then
        error("coroutine error: " .. tostring(command))
    end
    if command == "SUSPEND" then
        --协程主动挂起，等新消息恢复
    elseif command == "QUIT" then
        coroutine.close(co)
    end
end

--定时器等待：分配 session 并注册（对齐 skynet auxtimeout）
local function auxtimeout(ti)
    local session = c.genid()
    c.timeout(c.self(), ti, session)
    return session
end

--消息分发入口（由 C++ Service::OnMsg 调用，对齐 skynet.dispatch_message）
local function raw_dispatch_message(type, session, source, buff, sz)
    --RPC响应：恢复等待的协程
    if type == PTYPE_RESPONSE then
        local co = session_id_coroutine[session]
        if co then
            session_id_coroutine[session] = nil
            suspend(co, coroutine_resume(co, true, buff, sz))
        end
        return
    end
    --错误通知：恢复等待的协程并使其失败（对齐 skynet.lua，call 收到 ERROR 报 "call failed"）
    if type == PTYPE_ERROR then
        local co = session_id_coroutine[session]
        if co then
            session_id_coroutine[session] = nil
            suspend(co, coroutine_resume(co, false))
        end
        return
    end
    --未知协议类型（对齐 skynet：未注册类型无回调，告警后丢弃）
    local p = proto[type]
    if p == nil then
        print("unknown protocol type " .. tostring(type) ..
            " from " .. tostring(source) .. " session " .. tostring(session))
        return
    end
    --普通消息：新协程执行对应 dispatch 回调
    if p.dispatch then
        local f = p.dispatch
        local co = co_create(f)
        session_coroutine_id[co] = session
        session_coroutine_address[co] = source
        suspend(co, coroutine_resume(co, session, source, p.unpack(buff, sz)))
    end
end

function starnet.dispatch_message(...)
    local ok, err = pcall(raw_dispatch_message, ...)
    --处理 fork 队列（对齐 skynet dispatch_message）
    while true do
        if fork_queue.h > fork_queue.t then
            fork_queue.h = 1
            fork_queue.t = 0
            break
        end
        local h = fork_queue.h
        local co = fork_queue[h]
        fork_queue[h] = nil
        fork_queue.h = h + 1
        local fork_ok, fork_err = coroutine_resume(co)
        if not fork_ok then
            if ok then
                ok = false
                err = tostring(fork_err)
            else
                err = tostring(err) .. "\n" .. tostring(fork_err)
            end
        end
    end
    if not ok then
        error(tostring(err))
    end
end

--socket 消息分发（由 C++ Service::OnMsg 调用，协程化）
local function dispatch_in_coroutine(f, ...)
    local co = co_create(f)
    session_coroutine_id[co] = 0
    session_coroutine_address[co] = 0
    suspend(co, coroutine_resume(co, ...))
end

function starnet.dispatch_socket(subtype, a, b, c, d)
    if subtype == SKYNET_SOCKET_TYPE_ACCEPT then
        local p = proto["accept"]
        if p and p.dispatch then
            dispatch_in_coroutine(p.dispatch, a, b)  -- func(clientfd, listenfd)
        end
    elseif subtype == SKYNET_SOCKET_TYPE_CONNECT then
        --主动连接成功（对齐 skynet dispatch("connect", fd, ip)）
        local p = proto["connect"]
        if p and p.dispatch then
            dispatch_in_coroutine(p.dispatch, a, b)  -- func(fd, ip)
        end
    elseif subtype == SKYNET_SOCKET_TYPE_ERROR then
        --连接失败等错误（对齐 skynet dispatch("error", fd, err)）
        local p = proto["error"]
        if p and p.dispatch then
            dispatch_in_coroutine(p.dispatch, a, b)  -- func(fd, err)
        end
    elseif subtype == SKYNET_SOCKET_TYPE_CLOSE then
        --连接关闭：清除该fd半包
        netpack.close(netpack_queue, a)
        local p = proto["close"]
        if p and p.dispatch then
            dispatch_in_coroutine(p.dispatch, a)  -- func(fd)
        end
    elseif subtype == SKYNET_SOCKET_TYPE_DATA then
        --数据：netpack 解析粘包/半包，投递完整包（对齐 skynet netpack.filter + pop）
        local p = proto["socket"]
        if p and p.dispatch then
            local t, fd, msg = netpack.filter(netpack_queue, a, b, c)
            while t == "data" or t == "more" do
                dispatch_in_coroutine(p.dispatch, fd, msg)  -- func(fd, msg)
                if t == "data" then
                    break
                end
                --more：队列中还有完整包
                fd, msg = netpack.pop(netpack_queue)
                if not fd then
                    break
                end
            end
        end
    elseif subtype == SKYNET_SOCKET_TYPE_WARNING then
        --写缓冲积压告警（对齐 skynet dispatch("warning", fd, kb)）
        local p = proto["warning"]
        if p and p.dispatch then
            dispatch_in_coroutine(p.dispatch, a, b)  -- func(fd, kb)
        end
    elseif subtype == SKYNET_SOCKET_TYPE_UDP then
        --UDP 数据报：报式无粘包，直接分发（对齐 skynet dispatch("udp", fd, msg, addr, port)）
        local p = proto["udp"]
        if p and p.dispatch then
            dispatch_in_coroutine(p.dispatch, a, b, c, d)  -- func(fd, msg, addr, port)
        end
    end
end

--服务元函数
function starnet.self()
    return c.self()
end

function starnet.exit()
    c.killservice(c.self())
    --终止当前协程（对齐 skynet exit 投 QUIT 后挂起，避免 exit 后代码继续执行）
    coroutine_yield("QUIT")
end

--名字服务（对齐 skynet.name / skynet.localname）
function starnet.name(name, handle)
    assert(c.name(handle, name), "duplicate name: " .. tostring(name))
end

function starnet.localname(name)
    local handle = c.localname(name)
    if handle == 0 then
        return nil
    end
    return handle
end

--地址解析：支持整数 id 或 '.名字'（对齐 skynet queryname）；未注册名返回 nil
local function resolve_addr_or_nil(addr)
    if type(addr) == "string" then
        return starnet.localname(addr)
    end
    return addr
end

--地址解析：失败抛错（用于 call）
local function resolve_addr(addr)
    local h = resolve_addr_or_nil(addr)
    if not h then
        error("invalid address: " .. tostring(addr))
    end
    return h
end

--启动函数（对齐 skynet.start：主协程执行）
function starnet.start(func)
    local co = co_create(func)
    session_coroutine_id[co] = 0
    session_coroutine_address[co] = 0
    suspend(co, coroutine_resume(co))
end

--注册消息处理函数（对齐 skynet.dispatch）
function starnet.dispatch(typename, func)
    local p = proto[typename]
    if p == nil then
        error("dispatch unknown protocol: " .. typename)
    end
    p.dispatch = func
end

--发送消息（无需响应，session=0，对齐 skynet.send/rawsend）
--地址解析失败（无效 .名字）时静默丢弃（对齐 skynet.send 容错，不抛错）
function starnet.send(addr, typename, ...)
    local p = proto[typename]
    local msg = p.pack(...) or ""
    addr = resolve_addr_or_nil(addr)
    if not addr then
        return
    end
    c.send_session(addr, p.id, 0, msg)
end

--网络封包：加 2 字节大端长度头（对齐 skynet netpack.pack）
function starnet.pack(msg)
    return netpack.pack(msg)
end

--socket 接口（对齐 skynet.socket 子表：listen/connect/bind/write/write_low/close/shutdown/start/pause/nodelay/udp/udp_connect/send_udp）
starnet.socket = {}

--监听（对齐 skynet.socket.listen）
function starnet.socket.listen(port, id)
    return c.listen(port, id)
end

--主动连接（对齐 skynet.socket.connect：非阻塞，返回 fd；成功投 dispatch("connect", fd, ip)，失败投 dispatch("error", fd, err)）
function starnet.socket.connect(addr, port)
    return c.connect(addr, port)
end

--绑定已有 fd（对齐 skynet.socket.bind：接管外部创建的 socket，引擎不负责 close；类型自动识别，数据走 dispatch("socket"/"udp")）
function starnet.socket.bind(fd)
    return c.bind(fd)
end

--发送（对齐 skynet.socket.write：走 high 高优先级队列）
function starnet.socket.write(fd, msg)
    return c.write(fd, msg)
end

--发送低优先级（对齐 skynet.socket.send_low：high 刷完才刷 low，不丢包仅排后）
function starnet.socket.write_low(fd, msg)
    return c.write_low(fd, msg)
end

--关闭连接（对齐 skynet.socket.close）
function starnet.socket.close(fd)
    c.close_conn(fd)
end

--shutdown：写缓冲发完再关（对齐 skynet.socket.shutdown，优雅关闭）
function starnet.socket.shutdown(fd)
    c.shutdown(fd)
end

--恢复读（对齐 skynet.socket.start：对已读连接幂等；新连接默认读，仅用于 pause 后恢复）
function starnet.socket.start(fd)
    c.start(fd)
end

--暂停读（对齐 skynet.socket.pause：去 EPOLLIN，写缓冲照常刷，流控用）
function starnet.socket.pause(fd)
    c.pause(fd)
end

--TCP_NODELAY 关 Nagle（对齐 skynet.socket.nodelay：游戏交互协议必须）
function starnet.socket.nodelay(fd)
    c.nodelay(fd)
end

--UDP 监听：绑定 addr:port 收包（对齐 skynet.udp，即 udp_listen）
function starnet.socket.udp(addr, port)
    return c.udp(addr, port, true)
end

--UDP 连接：创建并设置默认对端（对齐 skynet.udp_connect，后续 send_udp 可不带地址）
function starnet.socket.udp_connect(addr, port)
    local fd = c.udp(addr, port, false)
    if fd >= 0 then
        c.udp_connect(fd, addr, port)
    end
    return fd
end

--UDP 发送：addr 为空用默认对端（对齐 skynet.send_udp）
function starnet.socket.send_udp(fd, msg, addr, port)
    if addr == nil then
        return c.send_udp(fd, nil, 0, msg)
    end
    return c.send_udp(fd, addr, port or 0, msg)
end

--写日志（对齐 skynet.log：走统一日志器，时间戳 + 落盘/stderr）
function starnet.log(...)
    c.log(...)
end

--环境配置：查询（对齐 skynet.getenv；键不存在返回 nil）
function starnet.getenv(name)
    return c.getenv(name)
end

--环境配置：设置（对齐 skynet.setenv）
function starnet.setenv(name, value)
    c.setenv(name, value)
end

--内存统计（对齐 skynet.mem：进程 RSS，KB）
function starnet.mem()
    return c.mem()
end

--RPC请求：分配 session 发送并挂起等待响应（对齐 skynet.call）
local function yield_call(service, session)
    session_id_coroutine[session] = running_thread
    local ok, msg, sz = coroutine_yield("SUSPEND")
    if not ok then
        error("call failed")
    end
    return msg, sz
end

function starnet.call(addr, typename, ...)
    local addr = resolve_addr(addr)
    local p = proto[typename]
    local msg = p.pack(...) or ""
    local session = c.genid()
    c.send_session(addr, p.id, session, msg)
    return p.unpack(yield_call(addr, session))
end

--回包（对齐 skynet.ret：把结果发回请求方）
function starnet.ret(msg, sz)
    msg = msg or ""
    local co = running_thread
    local co_session = session_coroutine_id[co]
    if co_session == nil then
        error("No session")
    end
    session_coroutine_id[co] = nil
    if co_session == 0 then
        return false  -- send 消息不需要回包
    end
    local co_address = session_coroutine_address[co]
    c.send_session(co_address, PTYPE_RESPONSE, co_session, msg)
    return true
end

--异步回包（对齐 skynet.response：返回一次性回包函数）
function starnet.response()
    local co = running_thread
    local co_session = assert(session_coroutine_id[co], "no session")
    session_coroutine_id[co] = nil
    local co_address = session_coroutine_address[co]
    if co_session == 0 then
        return function() return false end
    end
    local sent = false
    return function(msg, sz)
        if sent then
            return false
        end
        sent = true
        c.send_session(co_address, PTYPE_RESPONSE, co_session, msg or "")
        return true
    end
end

--协程挂起指定时间（centisecond，对齐 skynet.sleep）
function starnet.sleep(ti)
    local session = auxtimeout(ti)
    session_id_coroutine[session] = running_thread
    local ok, ret = coroutine_yield("SUSPEND")
    if not ok then
        error(tostring(ret))
    end
end

--新建协程延后执行（对齐 skynet.fork）
function starnet.fork(func, ...)
    local args = tpack(...)
    local co = co_create(function()
        func(tunpack(args, 1, args.n))
    end)
    fork_queue[fork_queue.t + 1] = co
    fork_queue.t = fork_queue.t + 1
    return co
end

--定时器回调（对齐 skynet.timeout：ti centisecond 后执行 func）
function starnet.timeout(ti, func)
    local session = auxtimeout(ti)
    local co = co_create(func)
    session_id_coroutine[session] = co
    return co
end

--用 Lua 表覆盖 C 表（starnet.xxx 优先走宿主库封装，其余字段回退 C 绑定）
setmetatable(starnet, { __index = c })
_G.starnet = starnet

return starnet