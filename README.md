# Starnet

一个 skynet 风格的**游戏服务器框架**：C++20 内核（消息驱动 + 多线程） + Lua 服务（协程 + dispatch），整体对齐 skynet 的架构与 API，命名统一使用 `starnet_` 前缀便于对照移植。

## 功能

- **消息驱动架构**：二级消息队列 + 全局队列 + worker 线程池，weight 加权调度（`MQ_OVERLOAD` 积压告警）
- **Lua 协程 + Session RPC**：`starnet.call/ret/response/send`，每条消息在独立协程处理，`dispatch("lua"/"socket"/"accept"/"close")` 注册回调
- **高性能网络层**：epoll ET 模式、动态读缓冲（8192B ~ 1MB）、2 字节长度头粘包/半包解析（netpack）
- **TCP / UDP / 主动连接 / 绑定已有 fd**：`starnet.socket.listen/connect/bind/udp`，支持 IPv4/IPv6
- **写缓冲优先级**：high/low 双队列（high 先刷、low 不丢包），积压告警（1MB 起、阈值翻倍渐进）
- **连接控制**：`nodelay/pause/start/shutdown`，服务端 accept 的连接默认开启 `TCP_NODELAY`
- **定时器系统**：4 级时间轮 + 独立 timer 线程（2.5ms 驱动），`starnet.sleep/fork/timeout`
- **基础设施**：日志（文件/stderr）、配置（`getenv/setenv`）、内存统计（进程 RSS）、监视器（5 秒卡死告警）、性能统计（`starnet.cpu/message/msgtime`）
- **进程级能力**：优雅全局退出（`SIGINT` → 逐服务退出 + 残留消息回 `PTYPE_ERROR`）、守护进程化（daemon）
- **sharedata 共享只读数据**：引擎内精简版，跨服务零拷贝只读、版本号热更
- **cluster 跨节点 RPC**：简化版 skynet.cluster，全 Lua、C++ 零改动
- **示例服务**：`main / chat / ping / db / udp / cluster / sharedata`

## 项目亮点

- **消息驱动 + 协程，服务即 Lua 脚本**
    - C++ 内核负责队列/线程/网络/定时，业务全在 Lua：`starnet.start` + `starnet.dispatch` 即可写服务，新增服务零重编译（`luaservice` 模板按名加载 `<type>.lua`）。
- **对齐 skynet 架构，逐模块对照移植**
    - `starnet_service` ↔ `skynet_server`、`starnet_mq` ↔ `skynet_mq`、`starnet_timer` ↔ `skynet_timer`、`starnet_socket_server` ↔ `socket_server`… 每个模块都有对应的 skynet 参照物，便于移植与排查。
- **新连接默认读，业务零样板**
    - skynet 要求 accept/connect 后显式 `socket.start(fd)` 才收数据；starnet 的 accept/数据走同一条服务队列，**先 ACCEPT 后 DATA 顺序天然保证**，所以新连接默认读，`start` 只作 `pause` 后的恢复。
- **读统一到 socket 线程，worker 只消费现成数据**
    - TCP 循环 `read`、UDP 循环 `recvfrom` 都在 socket 线程完成，投递的是完整缓冲；worker 侧不再碰 `read/recvfrom`，EOF/错误检测随读一并迁移。
- **UDP 无写缓冲，不混入 TCP 发送路径**
    - skynet 的 UDP 写缓冲是「TCP/UDP 共用一套发送流程」的副产品；starnet 的 UDP 一发就走、直接 `sendto`，无队列负担。
- **写缓冲分优先级 + 渐进式积压告警**
    - high 刷完才刷 low，low 不丢包仅排后；`wbSize ≥ 1MB` 时投 `dispatch("warning", fd, kb)`，阈值翻倍渐进，连接是否断开由业务决定。
- **内存统计用进程 RSS，不做全局 operator new 重载**
    - 单进程框架下进程内存即框架全部内存；C++ 的 `malloc_hook` 等价物是全局 `operator new` 重载，侵入整个进程所有分配且无泄漏定位能力，收益与风险不成比例。
- **不实施 `skynet_module`（C 模块加载）**
    - skynet 用 `dlopen` 是因为 C 语言没有运行时按名分派机制；starnet 是 C++ 单体，服务已是运行时加载，若未来需要 C++ 原生服务，用静态注册表即可，无 `extern "C"` 与跨平台 dlopen 差异。

## 系统架构

项目围绕一条完整链路展开：**事件采集 → 消息分发 → 服务处理 → 业务交付**。

| 阶段 | 做什么 | 涉及模块 |
| --- | --- | --- |
| 事件采集 | socket 线程用 epoll 统一读 TCP/UDP、accept 循环、连接事件；timer 线程按时间轮投超时 | `starnet_socket_server` / `starnet_timer` |
| 消息分发 | 二级队列 + 全局队列，worker 按 weight 加权取消息，`MQ_OVERLOAD` 积压告警 | `starnet_mq` / `starnet_worker` / `starnet_start` |
| 服务处理 | worker 按消息类型分发到服务，Lua 协程执行 `dispatch` 回调；session RPC 挂起/恢复协程 | `starnet_service` / `lua-starnet` / `starnet.lua` |
| 业务交付 | 服务间 RPC、socket 收发（netpack 粘包半包）、sharedata 只读共享、cluster 跨节点 | `lua-netpack` / `lua-seri` / `lua-sharedata` / `starnet/cluster.lua` |

> 另设 monitor 线程（每 worker 一个监视器，5 秒检查卡死）、logger 统一日志、`starnet_handle` 名字服务（handle 从 1 开始、0 保留）。

## 依赖与构建

项目依赖 Lua 5.3.5（源码不随仓库分发），需要手动下载并编译：

```sh
# 下载并解压到 3rd 目录，确保目录结构为 3rd/lua-5.3.5/
wget https://www.lua.org/ftp/lua-5.3.5.tar.gz
tar -xzf lua-5.3.5.tar.gz -C 3rd/

# 编译生成 src/liblua.a
cd 3rd/lua-5.3.5
make linux
cd ../..
```

> 需要 Linux 环境（依赖 epoll / pthread）。

```sh
mkdir build && cd build
cmake ..
make
```

## 快速开始

服务路径是相对运行目录的（默认模板 `../service/...;../examples/...`），所以**必须从 `build/` 目录运行**可执行文件：

```sh
cd build
./starnet ../examples/config.lua
```

不带参数直接 `./starnet` 也可运行（使用默认配置）。`config.lua` 的 `start` 字段决定启动哪个服务（默认 `main`），`main` 的 `starnet.start` 再拉起 `chat`、`ping`、`db`、`udp`。

联调 `chat` 聊天室示例（监听 8002 端口，多开几个终端即可互相收发）：

```sh
nc 127.0.0.1 8002
```

UDP echo 示例（8003 端口）：

```sh
echo hello | nc -u 127.0.0.1 8003
```

更多示例（`ping` RPC、`db`、`cluster` 跨节点、`sharedata` 热更）见 [`examples/README.md`](examples/README.md)。

## 服务写法

服务脚本统一用 `lualib/starnet.lua` 宿主库（对齐 `skynet.lua`），核心两点：

```lua
local starnet = require "starnet"

starnet.start(function()
    -- 启动逻辑
end)

starnet.dispatch("lua", function(session, source, buff)
    -- 收到服务间消息：session > 0 可用 starnet.ret 回包（RPC 应答）
end)
```

常用 API：

| API | 说明 | 对齐 skynet |
| --- | --- | --- |
| `starnet.self()` | 当前服务 id | `skynet.self()` |
| `starnet.newservice(type)` | 新建服务 | `skynet.newservice` |
| `starnet.send(addr, "lua", msg)` | 发送消息（无需响应）；addr 支持 `.名字` | `skynet.send` |
| `starnet.call(addr, "lua", msg)` | RPC 同步调用（挂起协程等响应） | `skynet.call` |
| `starnet.ret(msg)` / `starnet.response()` | 会话回包 / 一次性异步回包函数 | `skynet.ret` / `skynet.response` |
| `starnet.sleep(ti)` / `starnet.fork(func)` / `starnet.timeout(ti, func)` | 协程挂起 / 新协程 / 定时回调 | `skynet.sleep/fork/timeout` |
| `starnet.socket.listen(port, id)` | 监听端口 | `skynet.socket.listen` |
| `starnet.socket.write(fd, msg)` / `write_low(fd, msg)` | 发送（high 高优先级 / low 低优先级队列） | `skynet.socket.send` / `send_low` |
| `starnet.socket.connect(host, port)` | 主动连接（非阻塞） | `skynet.socket.connect` |
| `starnet.socket.udp(...)` / `send_udp(fd, msg, addr, port)` | UDP 监听 / 发送 | `skynet.udp` / `send_udp` |
| `starnet.socket.nodelay/pause/start/shutdown/bind` | 连接控制 / 绑定已有 fd | `skynet.socket.*` |
| `starnet.name(name, handle)` / `localname(name)` | 名字服务 | `skynet.name` / `localname` |
| `starnet.getenv/setenv/mem/log/cpu/message/msgtime` | 配置 / 内存 / 日志 / 性能统计 | `skynet.*` |

完整 API 表与 socket 消息类型（`accept` / `socket` / `close` / `udp` / `connect` / `error`）见 [`examples/README.md`](examples/README.md)。

## 配置

配置写在启动时传入的 Lua 脚本中（`examples/config.lua`），全部顶层键导入环境，运行时可用 `starnet.getenv` 查询：

| 字段 | 含义 | 默认值 |
| --- | --- | --- |
| `luaservice` | 服务搜索模板（`;` 分隔，`?` 为服务名占位） | `../service/?/init.lua;../examples/?.lua` |
| `start` | 启动服务名 | `main` |
| `thread` | worker 线程数 | `8`（对齐 skynet 标准；覆盖 weight 表避免全部 -1） |
| `luaPath` | Lua 模块搜索路径（`lualib/` 宿主库） | `../lualib/?.lua` |
| `logger` | 日志输出文件（空串写 stderr） | `""` |
| `daemon` | 守护进程化 pidfile（配字符串则后台运行；**必须同时配 `logger` 文件**） | `""`（前台） |
| `profile` | 性能统计开关（关闭时埋点零开销） | `true` |

## 目录结构

```
starnet/
├── CMakeLists.txt           # C++20 构建（-no-pie 兼容 Lua 静态库）
├── src/                     # C++ 内核（starnet_ 前缀，对齐 skynet）
│   ├── starnet.cpp          # 主类 Starnet：服务/配置/退出编排
│   ├── starnet_service.cpp  # 服务创建、消息分发（对齐 skynet_server）
│   ├── starnet_mq.cpp       # 二级 + 全局消息队列、MQ_OVERLOAD
│   ├── starnet_worker.cpp   # worker 线程、weight 加权调度
│   ├── starnet_start.cpp    # 线程池 / socket / timer / monitor 启动
│   ├── starnet_socket_server.cpp  # IO 引擎（epoll、读、写缓冲）
│   ├── starnet_timer.cpp    # 4 级时间轮定时器
│   ├── starnet_handle.cpp   # 服务句柄 / 名字服务
│   ├── starnet_sharedata.cpp # sharedata 共享只读数据（引擎内）
│   └── ...                  # env / logger / mem / monitor / daemon / config
├── include/                 # 内核头文件
├── lualib-src/              # Lua C 绑定（lua-starnet / lua-netpack / lua-seri / lua-sharedata）
├── lualib/                  # Lua 宿主库
│   ├── starnet.lua          # 协程 dispatch / session RPC / sleep/fork/timeout
│   └── starnet/             # cluster.lua（跨节点 RPC）、sharedata.lua（只读数据）
├── service/                 # 框架官方服务目录（当前为空）
├── examples/                # 演示服务（main/chat/ping/db/udp/cluster/sharedata，单文件 *.lua）
│   ├── config.lua           # 启动配置模板
│   └── README.md            # 服务写法 + 各示例联调说明
├── 3rd/lua-5.3.5/           # 第三方依赖（手动下载编译，不入库）
└── missing-skynet-modules.md # 相对 skynet 的缺失分析 + 补全路线图
```

## 说明

- starnet 是对 skynet 的简化重实现：核心机制（定时器 / 协程 RPC / 网络封包 / 协议分发 / 名字服务 / 监控 / sharedata / cluster）已补齐，部分高级特性（harbor 集群、sharetable、datasheet、C 模块加载）明确不做或有替代方案，详见 `missing-skynet-modules.md`。
- 新连接默认读、UDP 无写缓冲等与 skynet 的行为差异，均属于**有意设计**（理由见上文「项目亮点」与 `missing-skynet-modules.md`）。