# service/

框架**官方服务**目录——存放框架自带、但以 **Lua 服务**形式提供的系统能力。

当前为空。业务演示服务放在 `../examples/`（单文件 `*.lua`）。

## 为什么这里没有 bootstrap / launcher / gate

starnet **刻意不学 skynet** 把这三个文件做成服务，因为对应能力已内建在 C++ 核心：

| skynet 服务 | 职责 | starnet 的内建替代 |
|---|---|---|
| `bootstrap.lua` | 启动编排（先起 logger/辅助服务、再起业务） | `starnet_main.cpp` + `Starnet::Start` 顺序完成初始化，C++ 直接 `NewService(config.start)` 拉起业务服务 |
| `launcher.lua` | 服务创建 + init 完成确认（LAUNCHOK/ERROR） | `NewService` **同步**初始化（`Service::OnInit` 当场 loadfile + 跑完 `starnet.start` 到首次挂起），返回值即结果，无需异步确认协议 |
| `gate.lua` | 接入层（watchdog/agent 拆层、fd 转发） | SocketServer 内建，accept/data/close/error 事件**直投**监听服务（`starnet.socket.listen` + `dispatch("socket")`，见 `chat.lua`/`clusterd.lua`） |

所以本目录**不会**出现这三个文件；把服务加载、创建确认、网络派发这三件事内建进核心，换来的是 Lua 侧更薄、启动链路更短。

## 什么时候才往这里放服务

只有**需要以 Lua 服务形式存在、可替换可配置**的框架能力才放这里（做成 C++ 内建就无法在不改内核的情况下替换）。典型候选：

- **调试/控制台服务**（对齐 skynet `debug_console`）：在线查服务列表、状态（cpu/message/内存）、单服务 kill/重启——当前 starnet 缺"单服务生命周期管理 + 可观测性"，最值得补的是这层。
- **可替换的日志服务**：当前 logger 为 C++ 内建（`starnet_logger.cpp`），日志如需按服务分级/过滤/转发，可抽成 Lua 服务。

## 服务加载顺序

由 config 的 `luaservice` 模板决定（默认 `../service/?/init.lua;../examples/?.lua`）：

1. 先查 `../service/<type>/init.lua`（官方目录，**目录式**：一服务一目录）；
2. 再查 `../examples/<type>.lua`（示例/业务单文件）。

模板用 `;` 分隔多路径、`?` 占位服务名，见 `starnet_config.cpp`。
