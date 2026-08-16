#pragma once
#include "starnet_service.h"
#include <unordered_map>
#include "starnet_socketworker.h"
#include "starnet_conn.h"

class StarnetStart;

class Starnet {
public:
    //单例
    static Starnet* inst;
public:
    //构造函数
    Starnet();
    //初始化并开始
    void Start();
    //等待运行
    void Wait();
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
    //对外Event接口
    void ModifyEvent(int fd, bool epollOut);
private:
    //线程池管理（对齐 skynet_start.c）
    StarnetStart* start;
    //Socket线程对象（事件接口使用，创建归start）
    SocketWorker* socketWorker;
    //服务列表
    unordered_map<uint32_t, shared_ptr<Service>> services;
    uint32_t maxId = 0;              //最大ID
    pthread_rwlock_t servicesLock;   //读写锁
    //Conn列表
    unordered_map<uint32_t, shared_ptr<Conn>> conns;
    pthread_rwlock_t connsLock;   //读写锁

private:
    //获取服务
    shared_ptr<Service> GetService(uint32_t id);
};