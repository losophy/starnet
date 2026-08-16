#pragma once
#include <memory>
#include <unordered_map>
#include <list>
#include <pthread.h>
#include <sys/epoll.h>
#include "starnet_conn.h"
#include "starnet_msg.h"

using namespace std;

//写缓冲对象（对齐 socket_server.c 的 socket_sendbuffer）
struct WriteObject {
    int start;
    int len;
    shared_ptr<char> buff;
};

//连接写缓冲（对齐 socket_server.c 中 socket 的 wb_list）
struct ConnWriteBuffer {
    list<shared_ptr<WriteObject>> objs;
    bool isClosing = false;
    pthread_spinlock_t lock;
    ConnWriteBuffer() {
        pthread_spin_init(&lock, 0);
    }
};

//事件出口（桥接层实现，对齐 skynet_socket.c）
class SocketServerListener {
public:
    virtual void OnAcceptMsg(shared_ptr<SocketAcceptMsg> msg, uint32_t serviceId) = 0;
    virtual void OnRWMsg(shared_ptr<SocketRWMsg> msg, uint32_t serviceId) = 0;
};

//网络IO引擎（对齐 skynet socket_server.c，不依赖 Starnet）
class SocketServer {
public:
    void Init();        //初始化
    void operator()();  //线程函数
    //注册事件监听（桥接层）
    void SetListener(SocketServerListener* listener);
    //增删查Conn
    int AddConn(int fd, uint32_t id, Conn::TYPE type);
    shared_ptr<Conn> GetConn(int fd);
    bool RemoveConn(int fd);
    //Epoll事件（跨线程）
    void AddEvent(int fd);
    void RemoveEvent(int fd);
    void ModifyEvent(int fd, bool epollOut);
    //写缓冲（跨线程：worker线程发送，socket线程刷写）
    int SendBuffer(int fd, shared_ptr<char> buff, size_t len);
    void OnWriteable(int fd);
    void LingerClose(int fd);
private:
    void OnEvent(epoll_event ev);
    void OnAccept(shared_ptr<Conn> conn);
    void OnRW(shared_ptr<Conn> conn, bool r, bool w);
    //写缓冲内部
    void EntireWriteWhenEmpty(int fd, ConnWriteBuffer& wb, shared_ptr<char> buff, size_t len);
    void EntireWriteWhenNotEmpty(ConnWriteBuffer& wb, shared_ptr<char> buff, size_t len);
    bool WriteFrontObj(int fd, ConnWriteBuffer& wb);
private:
    //epoll描述符
    int epollFd;
    //事件监听（桥接层）
    SocketServerListener* listener;
    //Conn列表
    unordered_map<int, shared_ptr<Conn>> conns;
    pthread_rwlock_t connsLock;
    //写缓冲（fd -> 写缓冲）
    unordered_map<int, ConnWriteBuffer> writeBuffers;
    pthread_spinlock_t writeBuffersLock;
};