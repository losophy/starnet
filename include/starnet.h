#pragma once
#include "starnet_service.h"
#include "starnet_config.h"
#include <unordered_map>
#include "starnet_socket_server.h"
#include "starnet_conn.h"

class StarnetStart;

class Starnet {
public:
    //单例
    static Starnet* inst;
public:
    //构造函数
    Starnet();
    //初始化并开始（对齐 skynet_start(&config)）
    void Start(StarnetConfig& cfg);
    //等待运行
    void Wait();
    //获取服务搜索模板（对齐 skynet 的 LUA_SERVICE）
    string GetService();
    //获取Lua模块搜索路径（对齐 skynet 的 lua_path）
    string GetLuaPath();
    //增删服务
    uint32_t NewService(shared_ptr<string> type);
    void KillService(uint32_t id);     //异步退休（跨线程安全）
    //优雅全局退出（对齐 skynet_globalexit / skynet_context_dispatchall）
    void RequestExit();                //请求退出（信号 / starnet.globalexit 触发）
    bool IsExitRequested();            //是否已请求退出（Wait 轮询）
    void KillAllServices();            //全部服务退休（OnExit + mq 清理，dispatchall 排空语义）
    //名字服务（对齐 skynet_handle_namehandle/findname）
    bool NameService(uint32_t handle, const char* name);
    uint32_t FindServiceByName(const char* name);
    //环境配置（对齐 skynet_getenv/setenv）
    string GetEnv(const char* key, bool* found = NULL);
    void SetEnv(const char* key, const char* value);
    //内存统计（对齐 skynet.mem：进程 RSS，KB）
    size_t MemoryUsed();
    //发送消息
    void Send(uint32_t toId, shared_ptr<BaseMsg> msg);
    //仅测试
    shared_ptr<BaseMsg> MakeMsg(uint32_t source, char* buff, int len);
    //增删查Conn
    int AddConn(int fd, uint32_t id, Conn::TYPE type);
    shared_ptr<Conn> GetConn(int fd);
    bool RemoveConn(int fd);
    //网络连接操作接口（用原始read write）
    int Listen(uint32_t port, uint32_t serviceId);
    void CloseConn(uint32_t fd);
    //发送缓冲（转发到SocketIO引擎写缓冲；low=true 走低优先级队列，对齐 skynet send_lowpriority）
    int Write(int fd, shared_ptr<char> buff, size_t len, bool low = false);
    //UDP（对齐 skynet socket_server_udp_*：创建 / 设默认对端 / 发送）
    int Udp(uint32_t serviceId, const char* addr, int port, bool bind_);
    int SetUdpAddress(int fd, const char* addr, int port);
    int SendUdp(int fd, const char* addr, int port, shared_ptr<char> buff, size_t len);
    //主动连接（对齐 skynet socket_server_connect）
    int Connect(uint32_t serviceId, const char* host, int port);
    //绑定已有 fd（对齐 skynet socket_server_bind）
    int Bind(uint32_t serviceId, int fd);
    //连接控制（对齐 skynet socket_server_nodelay / pause / start / shutdown）
    int SetNoDelay(int fd);
    int PauseRead(int fd);
    int ResumeRead(int fd);
    void Shutdown(int fd);
    //对外Event接口
    void ModifyEvent(int fd, bool epollOut);
private:
    //线程池管理（对齐 skynet_start.c）
    StarnetStart* start;
    //启动配置
    StarnetConfig config;
    //SocketIO引擎（事件接口使用，创建归start）
    SocketServer* socketServer;
    //服务列表
    unordered_map<uint32_t, shared_ptr<Service>> services;
    uint32_t maxId = 0;              //最大ID
    pthread_rwlock_t servicesLock;   //读写锁

private:
    //获取服务
    shared_ptr<Service> GetService(uint32_t id);
};