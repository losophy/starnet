# examples/

演示/示例服务目录（对齐 skynet 的 `examples/`）。与 `../service/`（框架官方服务）区分：
- `service/`：框架自带的系统服务，当前为空
- `examples/`：演示如何写服务，当前有 `main`、`chat`、`ping`、`db`

## 服务搜索顺序

框架按配置中的 `luaservice` 模板顺序查找服务脚本（对齐 skynet 的 `LUA_SERVICE`，见 `src/starnet_config.cpp`）：

1. `../service/<类型>/init.lua`（官方优先）
2. `../examples/<类型>/init.lua`（示例兜底）

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
| `luaservice` | 服务搜索模板（`;` 分隔，`?` 为服务名占位） | `../service/?/init.lua;../examples/?/init.lua` |
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
| `starnet.NewService(type)` | 新建服务 | `skynet.newservice` |
| `starnet.KillService(id)` / `starnet.exit()` | 退出服务 | `skynet.kill` |
| `starnet.send(addr, "lua", msg)` | 发送消息（无需响应）；addr 支持 `.名字` | `skynet.send` |
| `starnet.call(addr, "lua", msg)` | RPC 同步调用（挂起协程等响应）；addr 支持 `.名字` | `skynet.call` |
| `starnet.ret(msg)` | 当前请求回包（会话式） | `skynet.ret` |
| `starnet.response()` | 返回一次性异步回包函数 | `skynet.response` |
| `starnet.sleep(ti)` | 协程挂起 ti centisecond | `skynet.sleep` |
| `starnet.fork(func, ...)` | 新建协程延后执行 | `skynet.fork` |
| `starnet.timeout(ti, func)` | ti centisecond 后执行 func | `skynet.timeout` |
| `starnet.Listen(port, id)` / `starnet.Write(fd, buff)` / `starnet.CloseConn(fd)` | socket 操作（回退到 C 绑定） | `skynet.socket` |
| `starnet.PTYPE_*` | 协议类型常量（TEXT/RESPONSE/SOCKET/LUA…，对齐 `skynet.h`） | `skynet.PTYPE_*` |

socket 消息类型（`dispatch` 名）：`accept`(clientfd, listenfd)、`socket`(fd, msg)、`close`(fd)。其中 `socket` 回调收到的是**完整数据包**——TCP 粘包/半包由 `netpack` 自动解析（2 字节大端长度头，对齐 skynet）；发送方需用 `starnet.pack(msg)` 加长度头：

```lua
starnet.dispatch("socket", function(fd, msg)
    -- msg 是完整包（已去掉长度头）
    starnet.Write(fd, starnet.pack(msg))  -- 转发时再加头
end)
```

## 示例说明

### main
启动入口示例。`starnet.start` 里拉起 `chat`/`ping`/`db`，演示：
- `starnet.call` RPC 请求-应答（call `ping`、`db`）
- `starnet.sleep` 协程挂起
- `starnet.timeout` 每秒心跳（回调续订）

### chat
Socket 聊天室示例（协程化 socket 消息 + 封包）：
- `starnet.start` 里 `starnet.Listen(8002, serviceId)` 监听 8002 端口
- `dispatch("accept", ...)` 记录新连接
- `dispatch("socket", ...)` 收到**完整包**后广播给所有连接（`starnet.Write(cfd, starnet.pack(msg))` 加长度头）
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

## 如何新建示例

1. 新建目录 `examples/<名字>/init.lua`
2. 用宿主库编写：`starnet.start` + `starnet.dispatch`（见上方 API 表）
3. 在启动链上拉起它：`main/init.lua` 里 `starnet.NewService("<名字>")`，或把 config 的 `start` 字段改成你的服务名（`starnet_main.cpp` 按 `cfg.start` 启动）