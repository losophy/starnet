# examples/

演示/示例服务目录（对齐 skynet 的 `examples/`）。与 `../service/`（框架官方服务）区分：
- `service/`：框架自带的系统服务，当前为空
- `examples/`：演示如何写服务，当前有 `main`、`chat`、`ping`

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

不带参数直接 `./starnet` 也可运行（使用默认配置）。启动的服务由配置的 `start` 字段决定（默认 `main`），`main` 的 `OnInit` 再拉起 `chat`。

### config.lua 字段

| 字段 | 含义 | 默认值 |
|---|---|---|
| `luaservice` | 服务搜索模板（`;` 分隔，`?` 为服务名占位） | `../service/?/init.lua;../examples/?/init.lua` |
| `start` | 启动服务名 | `main` |
| `thread` | worker 线程数 | `3` |

参考 `examples/config.lua`。

## 示例说明

### main
启动入口示例。`OnInit` 中调用 `starnet.NewService("chat")` 拉起聊天服务，并注册每秒心跳定时器演示 `starnet.timeout`（每 100 centisecond 触发 `OnTimeout`，session 自增续订）。

### chat
Socket 聊天室示例：
- `OnInit` 里 `starnet.Listen(8002, id)` 监听 8002 端口
- `OnAcceptMsg` 记录新连接
- `OnSocketData` 把收到的数据广播给所有连接（`starnet.Write` 走写缓冲）
- `OnSocketClose` 移除断开连接

联调方式（另开终端）：

```sh
nc 127.0.0.1 8002
```

（多开几个终端即可互相收发消息。）

### ping
服务间消息 ping-pong 示例：
- 演示 `starnet.Send(服务Id, 源Id, 数据)` 双向通信
- 演示 `string.pack` / `string.unpack` 二进制编解码（`i4` 整数对）

用法：在 `main` 的 `OnInit` 里加一行 `starnet.NewService("ping")`，并在 `chat`/`main` 里向 ping 发 `"start"` 即可看到 n1/n2 递增。

## 如何新建示例

1. 新建目录 `examples/<名字>/init.lua`
2. 实现回调函数（可选）：
   - `OnInit(id)`：服务创建后触发，注册/启动逻辑
   - `OnServiceMsg(source, buff)`：收到其他服务的消息
   - `OnAcceptMsg(listenfd, clientfd)`：监听端口有新连接
   - `OnSocketData(fd, buff)`：连接收到数据
   - `OnSocketClose(fd)`：连接关闭
   - `OnTimeout(session)`：定时器到期（`starnet.timeout(服务Id, 延时centisecond, session)` 注册，见 `main/init.lua`）
   - `OnExit()`：服务退出前
3. 在启动链上拉起它：`main/init.lua` 里 `starnet.NewService("<名字>")`，或把 config 的 `start` 字段改成你的服务名（`starnet_main.cpp` 按 `cfg.start` 启动）