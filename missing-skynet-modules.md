# Starnet 缺失的 Skynet 重要代码分析

> 本文档基于对 `starnet`（`src/`、`include/`、`lualib-src/`、`examples/`、`service/` 全部源码）与 `D:\服务器引擎制作资料\skynet` 核心模块的逐文件对比，整理出 starnet 相对 skynet 缺失的重要代码模块，并按补全优先级给出路线图。

## 现状概述

starnet 已具备的骨架（对应 skynet 的简化版）：

| starnet 模块 | 对应 skynet 模块 | 备注 |
|---|---|---|
| `starnet_service.cpp` | `skynet_server.c` / `skynet_context` | 服务创建、消息分发（按类型） |
| `starnet_mq.cpp/h`（`StarnetMQ` 二级队列 + `starnet_globalmq_*` 全局队列） | `skynet_mq.c` | 二级 + 全局消息队列 + `MQ_OVERLOAD` 告警（指数退避阈值）+ `Clear(dropFunc)` 丢弃通知 |
| `StarnetStart::StartWorker` 的 weight 表 + `Worker::weight` | `skynet_start.c` weight[] + `skynet_context_message_dispatch` | weight 加权调度：`<0` 每轮 1 条，`>=0` 每轮 `队列长度>>weight` 条 |
| `starnet_start.cpp/h`（`StarnetStart` 线程池+休眠唤醒）+ `starnet_worker.cpp` | `skynet_start.c` | 线程池管理、worker 线程 |
| `starnet_socket_server.cpp`（IO引擎） + `starnet_socket.cpp`（桥接） | `socket_server.c` + `skynet_socket.c` | 网络层（极简版；读已统一到 socket 线程：TCP 循环 read / UDP 循环 recvfrom，引擎读、服务收现成数据） |
| `starnet_socket_server.cpp` 内写缓冲（`ConnWriteBuffer`） | `socket_server.c` 写缓冲 | 写缓冲/优雅关闭（极简版） |
| `lualib-src/lua-starnet.cpp` | `lua-skynet.c` | Lua C API 绑定（极简版） |
| `lualib/starnet.lua` | `lualib/skynet.lua` | Lua 宿主库：协程 dispatch / session RPC / sleep/fork/timeout（核心子集） |
| `lualib-src/lua-netpack.cpp` | `lualib-src/lua-netpack.c` | 网络封包：2 字节大端长度头 + 粘包/半包解析（`netpack.filter/pop/pack`） |
| `starnet_msg.h` 的 `BaseMsg::TYPE` + `SocketMsg` | `skynet.h` 的 `PTYPE_*` + `socket_server.h` 的 `SKYNET_SOCKET_TYPE_*` | 协议类型体系：PTYPE 编号对齐 + socket 子类型（`starnet.PTYPE_*` 常量） |
| `starnet_handle.cpp/h` + `Starnet` 异步退休 | `skynet_handle.c` | 服务句柄/名字服务：`name/localname`、handle 从 1 开始（0 保留）、KillService 跨线程安全（worker 线程执行退出） |
| `starnet_env.cpp/h` + `StarnetConfig.env` | `skynet_env.c` | 环境配置：config 全部顶层键 + 运行时 `getenv/setenv`（用 C++ `unordered_map`+rwlock 实现，语义对齐 Lua 表方案） |
| `starnet_monitor.cpp/h` + `StarnetStart` monitor 线程 | `skynet_monitor.c` | 监视器：每 worker 一个 monitor（trigger/check）+ 5 秒检查线程，服务卡死打告警 |
| `starnet_mem.cpp/h` + `Starnet::MemoryUsed` + Lua `starnet.mem()` | `malloc_hook.c` / `mem_info.c` | 内存统计：进程 RSS（KB，读 `/proc/self/status`，对齐 `mem_info.c`；泄漏排查用外部 valgrind） |
| `examples/main、chat、ping、db、udp`（单文件 `*.lua`） + `starnet_config.cpp`（`luaservice` 模板，对齐 `skynet_main.c`/`service_snlua.c`） | `examples/` + `service/` | 示例服务 |
| `starnet_timer.cpp/h`（时间轮 + timer 线程，每 2.5ms 驱动） | `skynet_timer.c` | 定时器系统（极简版） |

> 命名约定：所有模块统一使用 `starnet_` 前缀命名文件（对齐 skynet 目录结构，便于对照移植），如 `starnet_server.cpp`、`starnet_timer.cpp`。当前旧文件已全部重命名，`starnet.h` / `starnet.cpp` 对应主类 `Starnet`。

---

## 一、核心机制缺失（不补无法成为可用的服务器框架）

### 1. 定时器系统

- **skynet 对应**：`skynet_timer.c` / `skynet_timer.h`
- **功能**：时间轮定时器（`TIME_NEAR` 近层 + 4 级远层）、独立 timer 线程驱动、`skynet_timeout()` 向目标服务投递 `PTYPE_RESPONSE` 超时消息。
- **starnet 现状**：✅ 已补 `starnet_timer.cpp/h`——4 级时间轮照搬 skynet、独立 timer 线程（`StarnetStart::StartTimer`，每 2.5ms 驱动 `starnet_updatetime`）、`starnet_timeout(handle, time, session)` 到期投递 `RESPONSE` 消息（`source=0`）到目标服务（对齐 skynet_timer 投 `PTYPE_RESPONSE`）。Lua 协程模型下的 `starnet.sleep / starnet.fork / starnet.timeout(回调)` 已随第 2 节补上。

### 2. Lua 协程 + Session RPC 模型

- **skynet 对应**：`lualib/skynet.lua`（1189 行）、`lualib-src/lua-skynet.c`
- **功能**：
  - 每条消息在一个独立 Lua 协程中处理（`skynet.dispatch`）。
  - `skynet.call / rawsend` 通过自增 `session` 实现同步 RPC 等待。
  - `skynet.response / ret / retpack / wakeup` 支持异步回包与唤醒。
  - `skynet.fork / skynet.timeout / skynet.sleep` 协程调度。
- **starnet 现状**：✅ 已补核心子集——
  - `lualib/starnet.lua`：协程池（`co_create` 复用）+ `dispatch_message` 入口（RESPONSE 按 `session_id_coroutine` 恢复等待协程，普通消息新建协程执行 `proto[type].dispatch`）+ fork 队列。
  - `starnet.call / ret / response / send`：`genid` 自增 session（`Service::sessionGen`，C 侧 `starnet.genid()`），`send_session` 携带 `(type, session)`（source 取当前服务），`ServiceMsg` 增加 `session` 字段，`RESPONSE=4` 消息类型对齐 `PTYPE_RESPONSE`。
  - `starnet.sleep / fork / timeout`：定时器到期投 RESPONSE 恢复协程（`auxtimeout`）。
  - 服务写法改为 skynet 风格：`starnet.start` + `starnet.dispatch("lua"/"socket"/"accept"/"close", func)`；socket 消息也协程化（`dispatch_socket`）。
- **未补**：`wakeup`（唤醒表）、`skynet.queue/mqueue`、错误上报 `watching_session`（协议类型体系已随第 4 节补上）。

### 3. 网络封包 / 粘包半包处理

- **skynet 对应**：`lualib-src/lua-netpack.c`、`socket_server.c` 的读缓冲
- **功能**：
  - netpack filter 处理 TCP 粘包/半包，按 2 字节长度头解析完整数据包。
  - `socket_server.c` 每个连接有独立读缓冲，边读边解析。
- **starnet 现状**：✅ 已补 `lua-netpack.cpp`——移植 `lua-netpack.c` 核心逻辑：
  - 发送 `starnet.pack(msg)` 加 2 字节大端长度头（最大 0xFFFF）。
  - 接收 `netpack.filter(queue, fd, buff, size)` 按 fd 维护 `uncomplete` 半包链表，解析完整包（`"data"`）或多个包（`"more"` + `netpack.pop` 循环取）；连接关闭 `netpack.close` 清半包。
  - `starnet.lua` 的 `dispatch_socket` 数据分支接入 filter，`dispatch("socket")` 收到的是**完整包**（无粘包半包）；`chat` 广播用 `starnet.socket.write(fd, starnet.pack(msg))`。
  - **读取已统一到 socket 线程**（对齐 skynet `socket_server.c` 读缓冲 + `read` 即投递）：`SocketServer::ReadData` 循环 `read(fd, 8192)` 到 EAGAIN，每块投一条 `SocketMsg{DATA, fd, buff}`，worker 侧直接用现成数据（`Service::OnSocketMsg` 不再 read）。
- **未补**：`socket_server.c` 的动态读缓冲大小（`MIN_READ_BUFFER` 增缩，starnet 用固定 8192 块）。

### 4. 协议类型分发体系

- **skynet 对应**：`skynet.h` 的 `PTYPE_*`（TEXT/RESPONSE/SOCKET/LUA/ERROR…）+ `skynet.register_protocol` + `skynet.dispatch`
- **功能**：消息按类型路由到不同回调；响应自动匹配请求 session。
- **starnet 现状**：✅ 已补——`BaseMsg::TYPE` 对齐 `skynet.h` 的 `PTYPE_*` 编号（TEXT=0/RESPONSE=1/…/SOCKET=6/ERROR=7/LUA=10…）：
  - `SocketAcceptMsg`+`SocketRWMsg` 合并为 `SocketMsg`（`type=SOCKET` + `SUBTYPE{DATA=1,CLOSE=3,ACCEPT=4,UDP=6}`，对齐 `SKYNET_SOCKET_TYPE_*`），socket 投递统一走 `OnSocketMsg`；UDP 报文带对端地址（`udpAddr` 二进制打包，对齐 skynet `gen_udp_address`）。
  - `starnet.lua` 暴露 `starnet.PTYPE_*` 常量表（对齐 skynet.lua）；内置协议 `"lua"` id=`PTYPE_LUA(10)`、`"socket"` id=`PTYPE_SOCKET(6)`。
  - `starnet.register_protocol` 加冲突/范围校验；`dispatch_message` 对未注册协议类型打印告警（含 source/session）。
  - `dispatch_socket` 首参改传 socket 子类型（ACCEPT/DATA/CLOSE），`dispatch("accept"/"socket"/"close")` 名字接口不变。
- **未补**：`PTYPE_TAG_DONTCOPY / ALLOCSESSION`（无 tag 消息需求）、`PTYPE_ERROR` 错误回包链路（协程出错向请求方回 ERROR，属 `watching_session` 范畴）。

### 5. 服务句柄 / 名字服务

- **skynet 对应**：`skynet_handle.c` / `skynet_handle.h`
- **功能**：handle 分配与回收、引用计数 `grab / release`、名字服务（`skynet_handle_namehandle` / `findname`）、`HANDLE_MASK` 高位预留远程 id。
- **starnet 现状**：✅ 已补——
  - 新增 `starnet_handle.cpp/h`：名字服务（`namehandle` 注册 / `findname` 查询 / `removename` 退休清名，`unordered_map` + rwlock）、`HANDLE_MASK 0xffffff` / `HANDLE_REMOTE_SHIFT 24`（高 8 位预留 harbor，单节点 harbor=0）。
  - `NewService` 分配 handle 从 **1** 开始（**0 保留**，对齐 skynet）。
  - **异步退休**（跨线程安全，对齐 `skynet_handle_retire` + `grab/release`）：`KillService` 改为标记 `isExiting` + 从 services map/名字表摘除 + 兜底入全局队列；`OnExit`/`lua_close` 由 worker 线程在安全点执行（`CheckAndPutGlobal` 用 `exited` 原子标志保证只执行一次），残留消息用 `mq.Clear()` 丢弃。解决原「调用线程同步 `lua_close` 与 worker 并发」的崩溃风险。
  - Lua 侧：`starnet.name(name, handle)` / `starnet.localname(name)`；`send/call` 地址支持 `.名字`（内部 `queryname` 解析）。
- **未补**：跨节点编址（harbor 位实际位移，需集群支持）、`skynet_handle_grab/release` 的显式引用计数（starnet 用 `shared_ptr` 持有服务对象，语义等价）。

---

## 二、重要基础设施缺失

| 缺失模块 | skynet 对应文件 | starnet 现状 | 影响 |
|---|---|---|---|
| **C 模块加载** | `skynet_module.c` / `skynet_module.h` | 服务 = Lua 脚本路径（`luaservice` 模板 `?`→type 找 `<type>.lua`，`service/` 官方为 `<type>/init.lua`），C++ 宿主唯一，无 C 原生服务 | 不实施（见下表后说明） |
| **日志系统** | `skynet_error.c` / `skynet_log.c` | ✅ 已补：`starnet_logger.cpp/h`（时间戳 + 级别 + 文件/stderr、线程安全、`config.logger` 指定文件、Lua 侧 `starnet.log`）；框架 `cout` 已替换 | 无 skynet 的 logger 独立服务（日志作为服务可按需替换） |
| **配置系统** | `skynet_env.c` | ✅ 已补：`starnet_env.cpp/h`（`getenv/setenv`，config 全部顶层键导入 env） | env 用 C++ `unordered_map`+rwlock 实现（**不照搬 Lua 表**）：starnet 为 C++ 单体、env 读多写少、避免额外 `lua_State` 与 `skynet_getenv` 返回指针跨调用失效的坑；语义等价；未补 skynet 内置 env 项（`mem_limit` 等） |
| **监视器** | `skynet_monitor.c` | ✅ 已补：`starnet_monitor.cpp/h`（每 worker 一个 monitor，处理消息前 trigger / 批后 check；monitor 线程每 5 秒检查，卡死打 `starnet_error` 告警） | 无 Lua `endless` 调试接口（告警为 C 侧输出，对齐 skynet 默认行为） |
| **内存管理** | `malloc_hook.c` / `mem_info.c` | ✅ 已补：`starnet_mem.cpp/h`（进程 RSS 报告，对齐 `mem_info.c`） | 决策：内存统计用「进程 RSS」，不做全局 `operator new` 重载（详见下方说明） |
| **队列 overload / 权重调度** | `skynet_mq.c` | ✅ 已补：`MQ_OVERLOAD`（1024）告警 + weight 加权调度 | 决策：weight 用硬编码表（对齐 `skynet_start.c`），不做 config 化（skynet 本身即硬编码；config 系统只导入顶层标量）；示例 `thread=8`（对齐 skynet 标准），避免 thread≤4 时全部 weight=-1（每轮 1 条）的低效 |
| **消息丢弃 / 释放** | `skynet_mq.c` 的 `message_drop` / `skynet_mq_release` | ✅ 已补：服务退出丢弃残留消息时给发送方回 `PTYPE_ERROR`（对齐 `drop_message`）；Lua 侧 `call` 收到 ERROR 报错退出 | 决策：SocketMsg 不加 source 字段（详见下方说明）；队列内存随 Service 生命周期（`shared_ptr` 自管） |
| **UDP** | `socket_server.c` 的 `socket_server_udp*` / `skynet_socket.c` 的 `SKYNET_SOCKET_TYPE_UDP` | ✅ 已补：`AddUdp/SetUdpAddress/SendUdp`（`starnet.udp/udp_connect/send_udp`），`getaddrinfo` 支持 IPv4/IPv6，socket 线程循环 `recvfrom` 读（读归引擎），报文带二进制对端地址（对齐 `gen_udp_address`） | 决策：UDP 无写缓冲（直接 `sendto`；skynet 的 UDP 写缓冲是「与 TCP 共用一套发送流程」的副产品，详见下方说明）；报式无粘包，`dispatch("udp", fd, msg, addr, port)` 不走 netpack |
| **主动连接** | `socket_server.c` 的 `socket_server_connect` / `skynet_socket.c` 的 `SKYNET_SOCKET_TYPE_CONNECT` | ✅ 已补：`Connect`（`starnet.socket.connect(host, port)`），`getaddrinfo` 支持 IPv4/IPv6 + 非阻塞 `connect`（立即成功或 EINPROGRESS 等 EPOLLOUT）；完成 `getsockopt(SO_ERROR)` 检查，成功投 `CONNECT`（带对端 ip）、失败投 `ERROR`（带错误串）；`Conn.connecting` 标志连接中 | 决策：connect 失败走 `SKYNET_SOCKET_TYPE_ERROR`（对齐 skynet，`dispatch("error", fd, err)`），业务可区分「连不上」与「连上后断开」；无 connect 超时（靠 TCP 内核超时，对齐 skynet） |
| **绑定已有 fd** | `socket_server.c` 的 `socket_server_bind` / `skynet_socket.c` 的 `skynet_socket_bind` | ✅ 已补：`Bind`（`starnet.socket.bind(fd)`）接管外部创建的 socket——校验 fd 合法且未托管、强制 `O_NONBLOCK`、`getsockopt(SO_TYPE)` 自动识别 TCP/UDP、`AddConn`+`AddEvent`；`Conn.isBind` 标记 | 决策：绑定 fd 所有权在外部，引擎**不负责 close**（对齐 skynet `force_close` 对 `SOCKET_TYPE_BIND` 跳过 close）；无 start 步骤（starnet 同步注册即启用读） |
| **写缓冲优先级** | `socket_server.c` 的 `send_socket`/`send_buffer_` / `socket_server_send_lowpriority` | ✅ 已补：`starnet.socket.write_low(fd, msg)`——`ConnWriteBuffer` high/low 双队列；空缓冲直写失败的部分**一律进 high**（对齐 `send_socket` "even priority == PRIORITY_LOW"）；非空按优先级分流；刷写对齐 `send_buffer_` 四步：1.刷 high 到空 2.刷 low 3.low 头半包挪到 high 尾（`raise_uncomplete` 防 TCP 乱序）4.都空关 EPOLLOUT；**积压告警**：✅ 已补（对齐 skynet `SOCKET_WARNING`）——`ConnWriteBuffer.wbSize` 入队累加/刷出扣减，`wbSize >= 1MB` 且 ≥ `warnSize` 时投 `dispatch("warning", fd, kb)` 给服务，阈值翻倍渐进（`warnSize` 0→2MB→4MB…） | 决策：low **不丢包**（仅排最后）；告警仅通知不自动断开（踢连接由服务业务决定）；`LingerClose` 需刷完 high+low 才关 |
| **连接控制** | `socket_server.c` 的 `socket_server_nodelay/pause/start/shutdown` | ✅ 已补：`starnet.socket.nodelay(fd)`（`setsockopt(TCP_NODELAY)` 关 Nagle）、`pause(fd)`（`Conn.paused` 去 EPOLLIN，写缓冲照常刷）、`start(fd)`（恢复读，对已读连接幂等）、`shutdown(fd)`（优雅关闭：写缓冲发完再断，复用 `LingerClose`）；`AddEvent`/`ModifyEvent` 按 `paused` 拼 events；**服务端 `accept` 的连接默认设 `TCP_NODELAY`**（偏离 skynet 由业务显式调：游戏小包协议低延迟刚需、UE 客户端实现不统一不可控，服务端兜底） | 决策：**新连接默认读，不学 skynet「必须显式 start 才收数据」**（详见下方说明）；`start` 只作 pause 后的恢复，日常业务零样板 |

> **内存统计为何用「进程 RSS」（不照搬 `malloc_hook`）**：
> 1. **对齐 skynet 实际做法**——skynet 的 `mem_info.c` 报告的就是进程 RSS（读 `/proc/self/status` 的 `VmRSS`），`skynet.mem()` 返回进程级内存，并非单服务统计；
> 2. **RSS 即监控全景**——单进程框架下，进程内存 = 框架全部内存（starnet 服务为 Lua 脚本，Lua 内存也在进程内）；
> 3. **不做全局 `operator new` 重载**——`malloc_hook` 的宏替换 `malloc/free` 在 C++ 不可行（统计不到 `new/delete` 与 std 容器）；C++ 等价物为全局 `operator new` 重载，侵入整个进程所有分配（含 std 库内部），须正确实现分配失败的 `bad_alloc` 异常语义与对齐处理，且**同样没有泄漏定位能力**，收益与风险不成比例；
> 4. **泄漏排查用外部工具**（valgrind/heaptrack），非框架内模块职责。

> **连接控制为何「新连接默认读」，不学 skynet「必须显式 `start` 才收数据」**：
> 1. **skynet 的设计**：accept/connect 刚建好的连接处于 paused（`SOCKET_TYPE_PACCEPT`），服务必须调 `socket.start(fd)` 才授权引擎读——这是给业务一个「先处理 accept 消息、准备好会话/初始化、再开始收数据」的显式同步点，也是 pause/start 流控的基础；
> 2. **starnet 不需要这个同步点**：starnet 的 accept/数据走**同一条服务消息队列**，socket 线程先投 ACCEPT、后续 DATA 排在后面——顺序天然保证「业务先于数据」（服务处理 accept 时数据必在其后），不存在 skynet 担心的「业务未就绪数据先到」问题；
> 3. **强制 start 的代价**：每个 accept/connect 处理多一行样板代码，忘了写连接就「僵尸」了（收不到数据），排查困难；
> 4. **starnet 的折中**：新连接默认读（零样板），但保留 `pause/start` 作为**可选流控**（`start` 对已读连接幂等、无副作用）——学 skynet 的「能力」，不学它的「默认强制」。

> **SocketMsg 为何不加 source 字段（丢弃通知只对 `ServiceMsg`）**：
> 1. **skynet 的 socket 消息 `source` 恒为 0**——skynet 所有消息共用 `struct skynet_message`（C 语言结构统一，无继承），socket 消息也装进该结构，网络消息没有真实发送方，`skynet_socket.c` 的 `forward_message` 设 `message.source = 0`；
> 2. 因此 skynet `drop_message` 对 socket 消息回 ERROR 是发给 handle 0 → `skynet_send` 查无此句柄**静默无效**——即网络消息被丢弃时本就无人收到通知；
> 3. starnet 是 C++ 多态（`BaseMsg` 派生 `ServiceMsg`/`SocketMsg`），消息模型已由继承统一，**不需要给 SocketMsg 加恒 0 的 source 字段**（加两个死字段并不改变任何行为）；
> 4. 行为等价：`ServiceMsg` 丢弃回 `PTYPE_ERROR` 通知发送方（对齐 skynet 有效路径），`SocketMsg` 无发送方语义直接跳过（对齐 skynet 实际效果）；消息内存全为智能指针自管，无泄漏。

> **C 模块加载（`skynet_module`）为何不实施**：skynet 用 `dlopen` 按名加载 `.so`，是因为其「C 内核 + 可插拔 C 服务」架构——C 语言没有运行时按名分派机制，只能交给操作系统加载器的符号表。starnet 是 C++ 单体：
> 1. **服务已是运行时加载**——`Service::OnInit` 按 `luaservice` 模板把 `?` 替换为类型名找 `<type>.lua`（examples 单文件；`service/` 官方目录为 `<type>/init.lua`，等价于「模块查询 + snlua」），加新服务零重编译；
> 2. **若未来需要 C++ 原生服务**，用静态注册表（`unordered_map<string, StarnetModule>`，含 `create/init/signal/release` 函数指针，对齐 skynet 函数表语义）即可，类型安全、无 `extern "C"` 符号修饰、无 Windows/Linux dlopen 差异；
> 3. **dlopen 的收益（第三方独立 `.so` 分发）在自研单体场景不存在**，而跨平台加载、ABI 脆弱等成本真实存在。

---

## 三、高级功能缺失

### 网络能力

- **UDP**：✅ 已补（`starnet.socket.udp/udp_connect/send_udp`，IPv4/IPv6，socket 线程读，无写缓冲，见现状表 UDP 行）。
- **主动连接**：✅ 已补（`starnet.socket.connect(host, port)`，非阻塞 connect + `getsockopt` 检查；成功 `dispatch("connect", fd, ip)`、失败 `dispatch("error", fd, err)`，见现状表「主动连接」行）。
- **绑定已有 fd**：✅ 已补（`starnet.socket.bind(fd)` 接管外部 socket，引擎不负责 close，见现状表「绑定已有 fd」行）。
- **写缓冲优先级**：✅ 已补（`starnet.socket.write_low(fd, msg)`，high/low 双队列：high 刷完才刷 low、low 不丢包仅排后、low 半包 raise 到 high 尾防乱序，见现状表「写缓冲优先级」行）；✅ 积压告警已补（`SOCKET_WARNING`，1MB 起、阈值翻倍渐进，`dispatch("warning", fd, kb)`）。
- **连接控制**：✅ 已补（`starnet.socket.nodelay/pause/start/shutdown`，见现状表「连接控制」行；服务端 accept 的连接默认已设 `TCP_NODELAY`）。
- **accept 细节**：✅ 已补——starnet 用 **ET 模式**（skynet 用 LT 不会漏，ET 只通知一次），`SocketServer::OnAccept` 原只 accept 一次会漏连接；现已**循环 accept 到 EAGAIN** 清空队列，accept 失败（EAGAIN 正常结束；EMFILE/ENFILE 记日志跳出）不再把 -1 注册进管理表。未加 skynet 的 reserve_fd 技巧（fd 耗尽不常见；ET 下释放 fd 后下个新连接即可恢复）。
- **读缓冲**：✅ 已补——`Conn` 加动态读缓冲（对齐 skynet `forward_message_tcp` 的 `s->p.size` 增缩）：`readSize` 初始 8192，读满（`len==readSize`）翻倍继续读、读不满且 `len*2 < readSize` 减半回落，区间 `[8192, 1MB]`；缓冲复用（`readBuffCap` 只增不减，缩小只改逻辑大小）。**区间贴合游戏服务器消息尺寸**（非 skynet 的 `MIN_READ_BUFFER=64` 起步，游戏小消息为主，8192 日常足够、大包/突发自动涨）。投递仍 `buff.assign` 拷贝一次（零拷贝未做）。

### 集群 / 分布式

- **harbor（不做）**：`skynet_harbor.c`（跨节点消息、`REMOTE_MAX`）——skynet 官方**不推荐 harbor**（全局名字依赖 master 单点、全连通 O(n²) 连接、消息可能乱序、任意 handle 互通难管控），且需劫持进程内消息发送路径（starnet C++ 内核改动大）。**不做，理由：游戏服务器跨服/跨区用 cluster 白名单 RPC 更贴合**。
- **cluster：✅ 已补（简化版，全 Lua，C++ 零改动）**——`lualib/starnet/cluster.lua`（客户端 API：`open/call/send/query/register/unregister/reload`）+ `examples/clusterd.lua`（每节点一个，统一管理出站/入站连接，简化合并 skynet 的 `clusterd`/`clustersender`/`clusteragent`/`cluster.core`/`gate`）。
- **datacenter / datacenterd（不做）**：全局共享数据服务，游戏服务器可用 sharedata/共享内存替代，暂缓。

### 标准服务集（`service/`）

| 服务 | 用途 |
|---|---|
| `gate.lua` + `watchdog.lua` | 网关 + 看门狗（连接管理） |
| `launcher.lua` / `bootstrap.lua` | 启动流程、配置加载 |
| `debug_console.lua` / `console.lua` / `dbg.lua` | 调试控制台 |
| `snaxd.lua` / `service_mgr` / `service_cell` | SNAX 框架 |
| `sharedatad.lua` | 共享数据分发 |
| `log.lua` / `cmemory.lua` | 日志 / 内存统计服务 |

### lualib 库（`lualib/`）

- `socket.lua` / `socketchannel.lua`：socket 协程封装
- `queue.lua` / `mqueue.lua`：消息队列协程
- `snax`：集群化 RPC 框架
- `sproto` / `seri`：协议序列化
- `sharedata` / `sharetable` / `datasheet`：共享只读数据
- **`cluster`（✅ 已补，见上「集群 / 分布式」）/ `harbor`（不做）/ `datacenter` / `multicast` / `stm`**
- `require.lua` / `codecache`：模块加载与代码缓存
- `debug.lua` / `profile.lua`：调试与性能分析

### 进程级能力

- `skynet_daemon.c`：守护进程化
- `skynet_globalexit` / `skynet_context_dispatchall`：优雅全局退出
- `skynet_profile_enable`：性能统计

---

## 四、补全路线图（按依赖顺序）

| 阶段 | 内容 | 依赖 |
|---|---|---|
| **P0（地基）** | 1. 定时器系统（时间轮 + timer 线程） | 无 |
| **P1（灵魂）** | 2. Lua 协程 + Session RPC 层（`skynet.call/response/wakeup`） | P0 |
| **P2（网络）** | 3. 网络封包层（长度头粘包处理 ✅ + 读统一到 socket 线程 ✅，固定 8192 块未补动态增缩）；accept 循环 | P1 |
| **P3（寻址）** | 4. handle/名字服务 + 协议类型分发（`PTYPE_*`） | P1（✅ 已完成） |
| **P4（工程化）** | 5. 日志（✅）/ 配置（✅：`getenv/setenv` + config 全量 env，`skynet_env`）/ 内存统计（✅：进程 RSS，`mem_info`）/ 队列 overload 与 weight 调度（✅：`MQ_OVERLOAD` 告警 + 硬编码 weight 表）/ 消息丢弃通知（✅：退出丢弃回 `PTYPE_ERROR`，对齐 `drop_message`） | 无 |
| **P5（扩展）** | 6. C 模块加载（`skynet_module`） | ——（不实施，见「C 模块加载为何不实施」） |
| **P6（高级）** | 7. 监视器（✅ 已完成）、集群（cluster ✅ 已补 / harbor 不做）、UDP（✅ 已完成）、connect（✅ 已完成）、bind 已有 fd（✅ 已完成）、写缓冲优先级（✅ 已完成）、连接控制（✅ 已完成）、标准服务集、lualib | P4 |

> 补充：starnet 现有实现还需对齐的简化点——`SocketServer::OnAccept` 循环 accept、`KillService` 与 worker 的并发安全。（服务退出时清空未处理消息✅ 已补：丢弃时回 `PTYPE_ERROR` 通知发送方）

> **读位置与 UDP 写缓冲（本次 UDP + 读迁移的决策）**：
> 1. **读统一到 socket 线程**——TCP `ReadData`（循环 `read` 8192 块）与 UDP `ReadUdp`（循环 `recvfrom` 65536 包）都由 socket 线程执行，投 `SocketMsg{DATA/UDP, fd, buff, [udpAddr]}`；worker 侧 `OnSocketMsg` 直接用现成数据。读是引擎操作，业务层不再碰 `read/recvfrom`（对齐 skynet `socket_server.c` 统一读）。EOF/错误检测也随读迁到 socket 线程（`read==0 → SOCKET_CLOSE`，对齐 skynet）。
> 2. **UDP 无写缓冲**——skynet 的 UDP 写缓冲（`write_buffer_udp` + 发送队列）是「TCP/UDP 共用同一个 `struct socket` 和同一套发送流程（写缓冲 + EPOLLOUT 刷写）」的**副产品**：UDP socket 挂进统一发送路径就必须配队列，哪怕 UDP 一发就走、堵了就丢包根本不需要。starnet 无此架构负担——UDP 发送直接 `sendto`，不混入 TCP 写缓冲。
> 3. **UDP 地址二进制打包**（对齐 skynet `gen_udp_address`）——消息传递用 1 字节 family + 2 字节端口 + 4/16 字节 IP 的紧凑二进制（省内存），`Service::OnSocketMsg` 解析成 ip 字符串 + 端口再交给 Lua（`dispatch("udp", fd, msg, addr, port)`，报式无粘包不走 netpack）。
> 4. **TCP 读缓冲固定 8192 块**——skynet 每连接动态读缓冲（`MIN_READ_BUFFER` 增缩），starnet 用固定栈缓冲，未补动态增缩。