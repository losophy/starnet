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
Socket 聊天室示例（协程化 socket 消息 + 封包）：
- `starnet.start` 里 `starnet.socket.listen(8002, serviceId)` 监听 8002 端口
- `dispatch("accept", ...)` 记录新连接
- `dispatch("socket", ...)` 收到**完整包**后广播给所有连接（`starnet.socket.write(cfd, starnet.pack(msg))` 加长度头）
- `dispatch("close", ...)` 移除断开连接

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

## 如何新建示例

1. 新建单文件 `examples/<名字>.lua`
2. 用宿主库编写：`starnet.start` + `starnet.dispatch`（见上方 API 表）
3. 在启动链上拉起它：`main.lua` 里 `starnet.newservice("<名字>")`，或把 config 的 `start` 字段改成你的服务名（`starnet_main.cpp` 按 `cfg.start` 启动）