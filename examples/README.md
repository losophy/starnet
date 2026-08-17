# examples/ 测试指南

本目录的每个 `*.lua` 都是**可运行的小例子**，用来验证引擎功能。照下面的步骤跑，看到"预期输出"里的日志就说明引擎正常。

## 0. 跑之前

1. **构建**（只需一次，见根目录 `README.md`）：需要 Linux + Lua 5.3.5
   ```sh
   cd build && cmake .. && make
   ```
2. **必须在 `build/` 目录里运行**可执行文件（服务路径是相对运行目录的）：
   ```sh
   cd build
   ./starnet ../examples/config.lua
   ```
3. 停止：`Ctrl+C`（优雅退出，打印各服务 `OnExit`）。

> 端口冲突（`bind fail errno=98`）：引擎已开 `SO_REUSEADDR`，杀掉进程后**可立即重启**；若仍报错，先 `pgrep -af starnet` 确认没有残留进程。

## 1. 测试一览

| 想测什么 | 跑哪个 | 怎么验证 |
|---|---|---|
| 服务间 RPC / 协程 / 名字服务 | `./starnet`（默认 config） | 看日志出现 `main call ping result n1:2 n2:4`、`pong:hello` |
| TCP 聊天室（多端互聊） | `./starnet` + 另开终端 `nc 127.0.0.1 8002` | 打字回车，其他终端能看到 |
| UDP 收发 | `./starnet` + `nc -u 127.0.0.1 8003` | 发 `hello` 原样回 `hello` |
| 跨节点 RPC（cluster） | 开 2 个终端各跑一个节点 | 双向 `call ... -> pong from ...` |
| 共享只读数据热更 | `./starnet ../examples/config_sharedata.lua` | 日志 `max_level` 从 50 → 99 → 100 |

下面逐个给出**完整命令**和**预期输出**。

## 2. 默认示例（RPC + 聊天 + UDP 全家桶）

启动 `main` 服务，它会拉起 `chat`（8002 TCP）、`ping`、`db`、`udp`（8003 UDP），并演示 `starnet.call` RPC、`sleep` 协程挂起。

```sh
cd build
./starnet ../examples/config.lua
```

**预期输出**（启动后 1~2 秒内）：

```
[lua] main start id:1
[info] config test_config = hello from config
[lua] chat start id:2
[lua] ping start id:3
[lua] db start id:4
[lua] udp start id:5
[lua] udp bind :8003 fd:6
[lua] main call ping result n1:2 n2:4
[lua] main call db result:pong:hello
[lua] main wake up after sleep
```

### chat：多终端互聊

另开终端，连 8002，**直接打字回车**：

```sh
nc 127.0.0.1 8002
```

- 你发的内容会广播给**所有**连着的 nc 终端（包括自己）
- 服务器日志对应出现：`chat accept 7 from 5`、`chat socket data 7 len:N`、断开后 `chat close 7`

> chat 用的是**裸数据逐行协议**（`starnet.socket.rawdata()`），所以 nc 打原始文本即可；不需要也不会解析 netpack 长度头。需要帧协议的服务（cluster 等）保持默认。

### udp：echo

```sh
echo hello | nc -u 127.0.0.1 8003
```

回显 `hello` 即成功。服务器日志：`udp recv fd:6 from 127.0.0.1:xxxxx len:6`。

## 3. cluster：跨节点 RPC

模拟两个"服务器"互相调用（全 Lua 实现，C++ 零改动）。**先起 nodeB（cluster2），再起 nodeA（cluster1）**，各用一个终端：

```sh
# 终端 1（先启动）
cd build && ./starnet ../examples/config_cluster2.lua
# 终端 2
cd build && ./starnet ../examples/config_cluster1.lua
```

**预期输出**：

- 终端 2（cluster1）日志：
  ```
  cluster listening on 8001
  cluster register [hello] :1
  cluster1 call nodeB.@hello2 -> pong from cluster2
  cluster1 query nodeB.hello2 -> 1
  ```
- 终端 1（cluster2）日志：
  ```
  cluster listening on 8002
  cluster register [hello2] :1
  cluster2 call nodeA.@hello -> pong from cluster1
  ```

即两边互相 RPC 成功（`@名字` 由对端解析，`query` 拿到对端服务 handle）。

> **若 nodeB 的调用方向 nodeA 时报 `cluster call ... error: cluster connection to ... closed`**：
> 说明发起调用时对端还没监听（本例两者都会在 `sleep(200)` 后互调，约 2 秒内对端必须已启动）。
> 这是**预期行为**——连接被拒时本次 `cluster.call` 立即失败返回 `nil` 并打印告警，**不会永久挂起**；
> 稍后对端起来后，它发过来的调用仍正常处理。想两者都调通，把两个节点在 2 秒内都启动即可。

## 4. sharedata：共享只读数据 + 热更

```sh
cd build && ./starnet ../examples/config_sharedata.lua
```

**预期输出**（约 0.3 秒内陆续打印）：

```
[info] config loaded, version = 0
[info] max_level = 50, server_name = starnet-demo
[info] rates[2] = 2.0, items[1].name = sword
[info] item_price[1003] = 999
[info] item_price sum = 1179
[info] deepcopy.max_level = 999 (改副本), box.max_level = 50 (共享表不变)
[info] old box max_level = 50 (快照，应为旧值 50)
[info] new box max_level = 99, server_name = hotfix
[info] sharedata demo running, update at 300ms
[info] subscribe: config updated, max_level = 100
```

验证点：老 box 快照不变（50）、新 query 拿到新版（99）、`update` 后订阅回调触发（100）。

## 5. 服务写法速览

想自己写个服务测试？三行核心（完整 API 表见根目录 `README.md`）：

```lua
local starnet = require "starnet"

starnet.start(function()
    -- 启动逻辑（如 starnet.socket.listen(端口, starnet.self())）
end)

starnet.dispatch("lua", function(session, source, buff)
    -- 收到服务间消息；session > 0 时用 starnet.ret(msg) 回包
end)
```

跑法：改 `config.lua` 的 `start` 字段为你的服务名，然后 `./starnet ../examples/config.lua`；或把 `starnet.newservice("<你的服务>")` 加进 `main.lua`。

socket 消息类型：`accept`(clientfd, listenfd)、`socket`(fd, msg)、`close`(fd)、`udp`(fd, msg, addr, port)、`connect`(fd, ip)、`error`(fd, err)。默认 TCP 数据走 netpack 帧协议（2 字节大端长度头，发送用 `starnet.pack`）；要裸字节流就 `starnet.socket.rawdata()`。

## 6. 常见问题

| 现象 | 原因 / 解决 |
|---|---|
| `listen error, bind fail errno=98` | 端口被占。杀干净残留进程再启；引擎已开 `SO_REUSEADDR`，正常 kill 后可立即重启 |
| `nc 连上但打字没反应` | 不是 chat 的话：该服务走 netpack 帧协议，nc 发的裸文本被当包长缓冲了；chat 已开 `rawdata()`，用它测 |
| 日志里 `OnEvent error, conn == NULL` | 旧版本 socket 线程拷贝 bug，重新构建（`make`） |
| cluster 只看到 `listening` 没有互相调用 | 检查启动顺序：**先 cluster2 后 cluster1**；以及两端口（8001/8002）未被占。若日志有 `cluster call ... error: cluster connection to ... closed`，说明发调用时对端未起，call 已失败返回（不挂起），重新启动即可 |

## 7. 目录结构

```
examples/
├── config.lua          # 默认配置：start=main，拉起 chat/ping/db/udp
├── config_cluster1.lua # cluster 节点 A（监听 8001）
├── config_cluster2.lua # cluster 节点 B（监听 8002）
├── config_sharedata.lua# sharedata 专用配置
├── main.lua            # 默认入口：RPC + 协程 + 拉起子服务
├── chat.lua            # TCP 聊天室（8002，裸数据）
├── ping.lua / db.lua   # 最简 RPC 服务
├── udp.lua             # UDP echo（8003）
├── clusterd.lua        # cluster 集群服务（每节点一个，懒启动）
├── cluster1.lua / cluster2.lua  # cluster 演示业务
├── config_data.lua     # sharedata 的数据文件
└── sharedata.lua       # sharedata 演示
```
