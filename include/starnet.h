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
    //增删服务
    uint32_t NewService(shared_ptr<string> type);
    void KillService(uint32_t id);     //仅限服务自己调用
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
    //发送缓冲（转发到SocketIO引擎写缓冲）
    int Write(int fd, shared_ptr<char> buff, size_t len);
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