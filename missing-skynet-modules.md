# Starnet 缺失的 Skynet 重要代码分析

> 本文档基于对 `starnet`（`src/`、`include/`、`lualib-src/`、`examples/`、`service/` 全部源码）与 `D:\服务器引擎制作资料\skynet` 核心模块的逐文件对比，整理出 starnet 相对 skynet 缺失的重要代码模块，并按补全优先级给出路线图。

## 现状概述

starnet 已具备的骨架（对应 skynet 的简化版）：

| starnet 模块 | 对应 skynet 模块 | 备注 |
|---|---|---|
| `starnet_service.cpp` | `skynet_server.c` / `skynet_context` | 服务创建、消息分发（按类型） |
| `starnet_mq.cpp/h`（`StarnetMQ` 二级队列 + `starnet_globalmq_*` 全局队列） | `skynet_mq.c` | 二级 + 全局消息队列（极简版） |
| `starnet_start.cpp/h`（`StarnetStart` 线程池+休眠唤醒）+ `starnet_worker.cpp` | `skynet_start.c` | 线程池管理、worker 线程 |
| `starnet_socket_server.cpp`（IO引擎） + `starnet_socket.cpp`（桥接） | `socket_server.c` + `skynet_socket.c` | 网络层（极简版） |
| `starnet_socket_server.cpp` 内写缓冲（`ConnWriteBuffer`） | `socket_server.c` 写缓冲 | 写缓冲/优雅关闭（极简版） |
| `lualib-src/lua-starnet.cpp` | `lua-skynet.c` | Lua C API 绑定（极简版） |
| `lualib/starnet.lua` | `lualib/skynet.lua` | Lua 宿主库：协程 dispatch / session RPC / sleep/fork/timeout（核心子集） |
| `lualib-src/lua-netpack.cpp` | `lualib-src/lua-netpack.c` | 网络封包：2 字节大端长度头 + 粘包/半包解析（`netpack.filter/pop/pack`） |
| `starnet_msg.h` 的 `BaseMsg::TYPE` + `SocketMsg` | `skynet.h` 的 `PTYPE_*` + `socket_server.h` 的 `SKYNET_SOCKET_TYPE_*` | 协议类型体系：PTYPE 编号对齐 + socket 子类型（`starnet.PTYPE_*` 常量） |
| `starnet_handle.cpp/h` + `Starnet` 异步退休 | `skynet_handle.c` | 服务句柄/名字服务：`name/localname`、handle 从 1 开始（0 保留）、KillService 跨线程安全（worker 线程执行退出） |
| `starnet_env.cpp/h` + `StarnetConfig.env` | `skynet_env.c` | 环境配置：config 全部顶层键 + 运行时 `getenv/setenv`（用 C++ `unordered_map`+rwlock 实现，语义对齐 Lua 表方案） |
| `examples/main、chat、ping、db` + `starnet_config.cpp`（`luaservice` 模板，对齐 `skynet_main.c`/`service_snlua.c`） | `examples/` + `service/` | 示例服务 |
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
  - `starnet.lua` 的 `dispatch_socket` 数据分支接入 filter，`dispatch("socket")` 收到的是**完整包**（无粘包半包）；`chat` 广播用 `starnet.Write(fd, starnet.pack(msg))`。
  - 读取仍在 worker 线程（`Service::OnRWMsg` read 512 字节裸数据，对齐 skynet 每次 read 即投递、Lua 侧解析）。
- **未补**：`socket_server.c` 的动态读缓冲大小（`MIN_READ_BUFFER` 增缩）、`netpack.tostring`（已字符串化）、读缓冲移到 socket 线程。

### 4. 协议类型分发体系

- **skynet 对应**：`skynet.h` 的 `PTYPE_*`（TEXT/RESPONSE/SOCKET/LUA/ERROR…）+ `skynet.register_protocol` + `skynet.dispatch`
- **功能**：消息按类型路由到不同回调；响应自动匹配请求 session。
- **starnet 现状**：✅ 已补——`BaseMsg::TYPE` 对齐 `skynet.h` 的 `PTYPE_*` 编号（TEXT=0/RESPONSE=1/…/SOCKET=6/ERROR=7/LUA=10…）：
  - `SocketAcceptMsg`+`SocketRWMsg` 合并为 `SocketMsg`（`type=SOCKET` + `SUBTYPE{DATA=1,CLOSE=3,ACCEPT=4}`，对齐 `SKYNET_SOCKET_TYPE_*`），socket 投递统一走 `OnSocketMsg`。
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
| **C 模块加载** | `skynet_module.c` / `skynet_module.h` | 服务 = Lua 脚本路径（`luaservice` 模板 `?`→type 找 `init.lua`），C++ 宿主唯一，无 C 原生服务 | 不实施（见下表后说明） |
| **日志系统** | `skynet_error.c` / `skynet_log.c` | ✅ 已补：`starnet_logger.cpp/h`（时间戳 + 级别 + 文件/stderr、线程安全、`config.logger` 指定文件、Lua 侧 `starnet.log`）；框架 `cout` 已替换 | 无 skynet 的 logger 独立服务（日志作为服务可按需替换） |
| **配置系统** | `skynet_env.c` | ✅ 已补：`starnet_env.cpp/h`（`getenv/setenv`，config 全部顶层键导入 env） | env 用 C++ `unordered_map`+rwlock 实现（**不照搬 Lua 表**）：starnet 为 C++ 单体、env 读多写少、避免额外 `lua_State` 与 `skynet_getenv` 返回指针跨调用失效的坑；语义等价；未补 skynet 内置 env 项（`mem_limit` 等） |
| **监视器** | `skynet_monitor.c` | 无死循环/卡死检测 | 服务死循环无告警（`skynet.endless`） |
| **内存管理** | `malloc_hook.c` / `mem_info.c` | 无内存统计（曾用 `char load[999999]` 占位，已移除） | 无内存统计、无泄漏排查工具 |
| **队列 overload / 权重调度** | `skynet_mq.c` | globalQueue 为普通 `queue` + spinlock | 无 `MQ_OVERLOAD` 告警、无 weight 加权调度 |
| **消息丢弃 / 释放** | `skynet_mq.c` 的 `message_drop` / `skynet_mq_release` | 服务退出时队列消息直接丢 | 服务退出清理不安全 |

> **C 模块加载（`skynet_module`）为何不实施**：skynet 用 `dlopen` 按名加载 `.so`，是因为其「C 内核 + 可插拔 C 服务」架构——C 语言没有运行时按名分派机制，只能交给操作系统加载器的符号表。starnet 是 C++ 单体：
> 1. **服务已是运行时加载**——`Service::OnInit` 按 `luaservice` 模板把 `?` 替换为类型名找 `<type>/init.lua`（等价于「模块查询 + snlua」），加新服务零重编译；
> 2. **若未来需要 C++ 原生服务**，用静态注册表（`unordered_map<string, StarnetModule>`，含 `create/init/signal/release` 函数指针，对齐 skynet 函数表语义）即可，类型安全、无 `extern "C"` 符号修饰、无 Windows/Linux dlopen 差异；
> 3. **dlopen 的收益（第三方独立 `.so` 分发）在自研单体场景不存在**，而跨平台加载、ABI 脆弱等成本真实存在。

---

## 三、高级功能缺失

### 网络能力

- **UDP**：`skynet_socket_udp_*` 系列（starnet 完全无 UDP）。
- **主动连接**：`skynet_socket_connect`（starnet 只能 listen）。
- **绑定已有 fd**：`skynet_socket_bind`。
- **写缓冲优先级**：`sendbuffer_lowpriority`（starnet 单队列）。
- **连接控制**：`nodelay / pause / start / shutdown`。
- **accept 细节**：starnet `SocketServer::OnAccept` 只 accept 一次，ET 模式应循环到 EAGAIN。
- **读缓冲**：starnet 无 per-conn 读缓冲累积。

### 集群 / 分布式

- `skynet_harbor.c`（跨节点消息、`REMOTE_MAX`）
- `cluster.lua` / `clustersender` / `clusteragent` / `clusterd`
- `datacenter` / `datacenterd`

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
- `cluster` / `harbor` / `datacenter` / `multicast` / `stm`
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
| **P2（网络）** | 3. 网络封包层（长度头粘包处理 + per-conn 读缓冲）；accept 循环 | P1 |
| **P3（寻址）** | 4. handle/名字服务 + 协议类型分发（`PTYPE_*`） | P1（✅ 已完成） |
| **P4（工程化）** | 5. 日志（✅）/ 配置（✅：`getenv/setenv` + config 全量 env，`skynet_env`）/ 内存统计；队列 overload 与 weight 调度 | 无 |
| **P5（扩展）** | 6. C 模块加载（`skynet_module`） | ——（不实施，见「C 模块加载为何不实施」） |
| **P6（高级）** | 7. 监视器、集群（harbor/cluster）、UDP/connect、标准服务集、lualib | P4 |

> 补充：starnet 现有实现还需对齐的简化点——`SocketServer::OnAccept` 循环 accept、服务退出时清空未处理消息、`KillService` 与 worker 的并发安全。