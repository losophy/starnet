# examples/

演示/示例服务目录（对齐 skynet 的 `examples/`）。与 `../service/`（框架官方服务）区分：
- `service/`：框架自带的系统服务，当前为空
- `examples/`：演示如何写服务，当前有 `main.lua`、`chat.lua`、`ping.lua`、`db.lua`、`udp.lua`（**单文件**，每服务一个 `*.lua`）

## 服务搜索顺序

框架按配置中的 `luaservice` 模板顺序查找服务脚本（对齐 skynet 的 `LUA_SERVICE`，见 `src/starnet_config.cpp`）：

1. `../service/<类型>/init.lua`（官方目录，目录式，优先）
2. `../examples/<类型>.lua`（示例兜底，单文件）

模板语法：`;` 分隔多个路径，`?` 是服务名占位。可用 `config.lua` 的 `luaservice` 字段覆盖。

## 构建与运行

需要 Linux 环境（依赖 epoll / pthread）。在项目根目录：

```sh
mkdir build && cd build
cmake ..
make
```

> 注意：服务路径是相对运行目录的（默认模板 `../service/...;../examples/...`），所以**必须从 `build/` 目录运行**可执行文件：

```sh
cd build
./starnet ../examples/config.lua
```

不带参数直接 `./starnet` 也可运行（使用默认配置）。启动的服务由配置的 `start` 字段决定（默认 `main`），`main` 的 `starnet.start` 再拉起 `chat`、`ping`、`db`。

### config.lua 字段

| 字段 | 含义 | 默认值 |
|---|---|---|
| `luaservice` | 服务搜索模板（`;` 分隔，`?` 为服务名占位） | `../service/?/init.lua;../examples/?.lua` |
| `start` | 启动服务名 | `main` |
| `thread` | worker 线程数 | `8`（对齐 skynet 标准；覆盖 weight 表，避免全部 -1） |
| `luaPath` | Lua 模块搜索路径（`lualib/` 宿主库，对齐 skynet `lua_path`） | `../lualib/?.lua` |
| `logger` | 日志输出文件（空串写 stderr） | `""` |
| `daemon` | 守护进程化 pidfile（配字符串则后台运行；**必须同时配 `logger` 文件**，否则 daemon 后 stdio 进 `/dev/null` 日志丢失，对齐 skynet） | `""`（前台） |
| `profile` | 性能统计开关（`starnet.cpu()/time()/message()` 查询；关闭时埋点零开销） | `true` |

参考 `examples/config.lua`。

## 服务写法（协程 + dispatch 风格）

服务脚本统一用 `lualib/starnet.lua` 宿主库（对齐 `skynet.lua`），核心两点：

1. `starnet.start(function() ... end)`：主协程执行启动逻辑
2. `starnet.dispatch("lua"/"socket"/"accept"/"close", func)`：注册消息处理函数，**每条消息在独立协程中处理**

```lua
local starnet = require "starnet"

starnet.start(function()
    --启动逻辑
end)

starnet.dispatch("lua", function(session, source, buff)
    --收到服务间消息：session>0 可用 starnet.ret 回包（RPC应答）
end)
```

### 服务间 API

| API | 说明 | 对齐 skynet |
|---|---|---|
| `starnet.self()` | 当前服务 id | `skynet.self()` |
| `starnet.log(...)` | 写日志（统一日志器：时间戳 + 级别 + `config.logger` 文件/stderr） | `skynet.log` |
| `starnet.getenv(name)` / `starnet.setenv(name, value)` | 环境配置查询/设置（config 全部键 + 运行时修改；getenv 不存在返回 nil） | `skynet.getenv` / `skynet.setenv` |
| `starnet.mem()` | 内存统计：进程常驻内存 RSS（KB，读 `/proc/self/status`） | `skynet.mem` |
| `starnet.name(name, handle)` / `starnet.localname(name)` | 注册本地名（`.` 前缀）/ 按名查 id | `skynet.name` / `skynet.localname` |
| `starnet.newservice(type)` | 新建服务 | `skynet.newservice` |
| `starnet.killservice(id)` / `starnet.exit()` | 退出服务 | `skynet.kill` |
| `starnet.send(addr, "lua", msg)` | 发送消息（无需响应）；addr 支持 `.名字` | `skynet.send` |
| `starnet.call(addr, "lua", msg)` | RPC 同步调用（挂起协程等响应）；addr 支持 `.名字` | `skynet.call` |
| `starnet.ret(msg)` | 当前请求回包（会话式） | `skynet.ret` |
| `starnet.response()` | 返回一次性异步回包函数 | `skynet.response` |
| `starnet.sleep(ti)` | 协程挂起 ti centisecond | `skynet.sleep` |
| `starnet.fork(func, ...)` | 新建协程延后执行 | `skynet.fork` |
| `starnet.timeout(ti, func)` | ti centisecond 后执行 func | `skynet.timeout` |
| `starnet.socket.listen(port, id)` | 监听端口（回退到 C 绑定） | `skynet.socket.listen` |
| `starnet.socket.write(fd, msg)` | 发送（走 high 高优先级队列） | `skynet.socket.send` |
| `starnet.socket.write_low(fd, msg)` | 发送（走 low 低优先级队列：high 刷完才刷 low，不丢包仅排后） | `skynet.socket.send_low` |
| `starnet.socket.close(fd)` | 关闭连接 | `skynet.socket.close` |
| `starnet.socket.connect(host, port)` | 主动连接（非阻塞）：返回 fd；成功 `dispatch("connect", fd, ip)`，失败 `dispatch("error", fd, err)` | `skynet.socket.connect` |
| `starnet.socket.bind(fd)` | 绑定已有 fd（接管外部创建的 socket，类型自动识别，引擎不负责 close） | `skynet.socket.bind` |
| `starnet.socket.nodelay(fd)` | TCP_NODELAY 关 Nagle（游戏交互协议必须；服务端 accept 的连接已默认开启，此接口主要给 connect 的连接用） | `skynet.socket.nodelay` |
| `starnet.socket.pause(fd)` / `starnet.socket.start(fd)` | 暂停读 / 恢复读（流控） | `skynet.socket.pause` / `skynet.socket.start` |
| `starnet.socket.shutdown(fd)` | 优雅关闭：写缓冲发完再关 | `skynet.socket.shutdown` |
| `starnet.socket.udp(addr, port)` / `starnet.socket.udp_connect(addr, port)` / `starnet.socket.send_udp(fd, msg, addr, port)` | UDP：监听 / 连接（默认对端）/ 发送（addr 空用默认对端） | `skynet.udp` / `skynet.udp_connect` / `skynet.send_udp` |
| `starnet.PTYPE_*` | 协议类型常量（TEXT/RESPONSE/SOCKET/LUA…，对齐 `skynet.h`） | `skynet.PTYPE_*` |

socket 消息类型（`dispatch` 名）：`accept`(clientfd, listenfd)、`socket`(fd, msg)、`close`(fd)、`udp`(fd, msg, addr, port)、`connect`(fd, ip)、`error`(fd, err)。其中 `socket` 回调收到的是**完整数据包**——TCP 粘包/半包由 `netpack` 自动解析（2 字节大端长度头，对齐 skynet）；发送方需用 `starnet.pack(msg)` 加长度头；`udp` 报式无粘包，直接收完整报文：

```lua
starnet.dispatch("socket", function(fd, msg)
    -- msg 是完整包（已去掉长度头）
    starnet.socket.write(fd, starnet.pack(msg))  -- 转发时再加头
end)
```

> **为什么 starnet 新连接默认就读、不用显式 `start(fd)`？**
> skynet 中 accept/connect 刚建好的连接默认暂停，服务必须调 `socket.start(fd)` 才授权引擎读数据（给业务"先准备再收"的同步点）。starnet 的 accept/数据走同一条服务消息队列，**先 ACCEPT 后 DATA 顺序天然保证**（业务处理 accept 时数据必在其后），无需显式授权——所以 `start` 只用于 `pause` 暂停读后的恢复，日常业务零样板。

## 示例说明

### main
启动入口示例。`starnet.start` 里拉起 `chat`/`ping`/`db`/`udp`，演示：
- `starnet.call` RPC 请求-应答（call `ping`、`db`）
- `starnet.sleep` 协程挂起
- `starnet.timeout` 每秒心跳（回调续订）

### chat
Socket 聊天室示例（协程化 socket 消息 + 裸数据逐行协议）：
- `starnet.start` 里 `starnet.socket.rawdata()` 启用裸数据模式（`dispatch("socket")` 直接收原始字节流，跳过 netpack 帧解析），`starnet.socket.listen(8002, serviceId)` 监听 8002 端口
- `dispatch("accept", ...)` 记录新连接
- `dispatch("socket", ...)` 收到**原始数据**后广播给所有连接（`starnet.socket.write(cfd, msg)` 原样转发，nc 直接可读）
- `dispatch("close", ...)` 移除断开连接

> chat 为什么用裸数据？聊天室是逐行协议，nc 等终端发的是纯文本、不带 netpack 的 2 字节长度头；若走帧协议，`netpack.filter` 会把文本头两个字节当包长，半包永远等不齐，`dispatch("socket")` 不触发。需要帧协议的 TCP 服务（如 cluster、自定义协议）保持默认即可，不调 `rawdata()`。

联调方式（另开终端）：

```sh
nc 127.0.0.1 8002
```

（多开几个终端即可互相收发消息。）

### ping
RPC 请求-应答示例：
- `dispatch("lua", ...)` 收到请求，`string.unpack` 解码
- `starnet.ret` 回包累加结果（`string.pack` 编码）

`main` 启动后自动 call `ping` 一次，打印 n1/n2 递增。

### db
最简 RPC 服务示例：收到 `"lua"` 请求后 `starnet.ret("pong:"..buff)` 回包。

### udp
UDP echo 示例（绑定 8003 端口）：
- `starnet.start` 里 `starnet.socket.udp("0.0.0.0", 8003)` 绑定 UDP 端口
- `dispatch("udp", ...)` 收到报文（含对端 `addr`/`port`），`starnet.socket.send_udp(fd, msg, addr, port)` 原样回包

联调方式（另开终端）：

```sh
echo hello | nc -u 127.0.0.1 8003
```

（返回 `hello` 即 echo 成功。）

### cluster（cluster1 + cluster2 + clusterd）
跨节点 RPC 示例（简化版 skynet.cluster，全 Lua、C++ 零改动）：
- `clusterd.lua`：每节点一个的集群服务（`cluster.lua` 首次调用时懒启动），统一管理出站/入站连接、`@名字`解析、请求/响应 session 匹配
- `cluster1.lua`：监听 8001，注册服务 `hello`，跨服调 `cluster2` 的 `hello2`
- `cluster2.lua`：监听 8002，注册服务 `hello2`，跨服调 `cluster1` 的 `hello`

配置：`config.lua` 的扁平键 `cluster = "nodeB=127.0.0.1:8002"`（`node=host:port` 逗号分隔，`clusterd` 读 `env`）。

API（`require "starnet.cluster"`）：`cluster.open(port)` 开放入站、`cluster.call(node, addr, payload)` 跨服 RPC、`cluster.send(node, addr, payload)` 跨服发送、`cluster.query(node, name)` 查对端注册名、`cluster.register(name[, addr])` 本节点公开服务、`cluster.unregister(name)`、`cluster.reload(cfg)`。`addr` 支持 `@名字`（对端 clusterd 解析）或数字 handle；`payload` 为字符串，业务自行打包（如 `string.pack`）。

> 协议：外层 `starnet.pack`（2 字节大端长度头，单包 ≤64KB）；内部 `>s2i4s4`（请求）与 `>i4i1s4`（响应）。启动顺序：**先 `cluster2` 后 `cluster1`**（或并行启动，cluster1 的调用稍候再发）。

```sh
# 终端 1（先启动 cluster2）
cd build && ./starnet ../examples/config_cluster2.lua
# 终端 2
cd build && ./starnet ../examples/config_cluster1.lua
```

`cluster1` 日志应打印 `call nodeB.@hello2 -> pong from cluster2`、`query nodeB.hello2 -> <cluster2 服务 handle>`。

### sharedata
共享只读数据示例（引擎内精简版 sharedata，对齐 `skynet.sharedata`）：
- `config_data.lua`：数据文件（`loadfile` 后取 `return` 的表，对齐 skynet `CMD.new` 的 `"@文件"` 加载）
- `sharedata.lua`：演示服务——`sharedata.new` 加载配置 → `query` 快照读取（box 只读视图零拷贝）→ `deepcopy` 导出普通表 → `update` 热更（老 box 读旧版、新 query 拿新版）→ `subscribe` 订阅（轮询简化版）

API（`require "starnet.sharedata"`）：`query(name)` 返回 box（`box[key]` 读取、`pairs(box)` 迭代、`box:version()`/`box:isdirty()`）；`new(name, v)` / `update(name, v)` 写入（`v` 为表、`"@文件路径"` 或代码串）；`delete(name)`；`deepcopy(name)` 导出普通表；`subscribe(name, fn)` 订阅更新（每 100ms 轮询版本）。底层 `starnet.sharedata` C 子表提供 `query/new/update/delete/exist/version/copy`。

```sh
cd build && ./starnet ../examples/config_sharedata.lua
```

日志应打印加载的配置值、热更前后 max_level（50 → 99）、订阅回调（max_level → 100）。

## 如何新建示例

1. 新建单文件 `examples/<名字>.lua`
2. 用宿主库编写：`starnet.start` + `starnet.dispatch`（见上方 API 表）
3. 在启动链上拉起它：`main.lua` 里 `starnet.newservice("<名字>")`，或把 config 的 `start` 字段改成你的服务名（`starnet_main.cpp` 按 `cfg.start` 启动）