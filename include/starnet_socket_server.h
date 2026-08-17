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

//连接写缓冲（对齐 socket_server.c 中 socket 的 wb_list：high/low 双队列）
//high=objs（高优先级，优先刷完）、low（低优先级，high 空才刷；不丢包仅排后，对齐 skynet send_lowpriority）
struct ConnWriteBuffer {
    list<shared_ptr<WriteObject>> objs;
    list<shared_ptr<WriteObject>> low;
    bool isClosing = false;
    size_t wbSize = 0;   //当前积压字节数（send 入队累加、刷出扣减，对齐 skynet socket 的 wb_size）
    size_t warnSize = 0; //下次告警阈值（触发后翻倍，对齐 skynet socket 的 warn_size）
    pthread_spinlock_t lock;
    ConnWriteBuffer() {
        pthread_spin_init(&lock, 0);
    }
};

//事件出口（桥接层实现，对齐 skynet_socket.c）
class SocketServerListener {
public:
    virtual void OnSocketMsg(shared_ptr<SocketMsg> msg, uint32_t serviceId) = 0;
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
    //写缓冲（跨线程：worker线程发送，socket线程刷写；low=true 走低优先级队列，对齐 skynet send_lowpriority）
    int SendBuffer(int fd, shared_ptr<char> buff, size_t len, bool low = false);
    void OnWriteable(int fd);
    void LingerClose(int fd);
    //UDP：创建 socket（addr/port 非空则 bind，bind_=true 对齐 skynet udp_listen；addr 空则任意地址）
    int AddUdp(uint32_t serviceId, const char* addr, int port, bool bind_);
    //UDP：设置默认对端地址（对齐 skynet socket_server_udp_connect）
    int SetUdpAddress(int fd, const char* addr, int port);
    //UDP：发送（addr/port 为空用默认对端；直接 sendto，无写缓冲）
    int SendUdp(int fd, const char* addr, int port, shared_ptr<char> buff, size_t len);
    //主动连接（对齐 skynet socket_server_connect：非阻塞 connect，完成投 CONNECT，失败投 ERROR）
    int Connect(uint32_t serviceId, const char* host, int port);
    //绑定已有 fd（对齐 skynet socket_server_bind：接管外部创建的 socket，引擎不负责 close）
    int Bind(uint32_t serviceId, int fd);
    //连接控制（对齐 skynet socket_server_nodelay / pause / start / shutdown）
    int SetNoDelay(int fd);          //TCP_NODELAY（关 Nagle，游戏交互协议必须）
    int PauseRead(int fd);           //暂停读（去 EPOLLIN，写缓冲照常刷）
    int ResumeRead(int fd);          //恢复读（start；对已读连接幂等）
private:
    void OnEvent(epoll_event ev);
    void OnAccept(shared_ptr<Conn> conn);
    void OnRW(shared_ptr<Conn> conn, bool r, bool w);
    //socket 线程读数据（对齐 skynet：读是引擎操作，服务收现成数据）
    void ReadData(shared_ptr<Conn> conn);   //TCP：循环 read 到 EAGAIN
    void ReadUdp(shared_ptr<Conn> conn);    //UDP：循环 recvfrom 到 EAGAIN
    void NotifyClose(shared_ptr<Conn> conn); //读 EOF/错误：通知 close + 清理
    //主动连接完成（EPOLLOUT 触发，getsockopt 检查结果，对齐 skynet report_connect）
    void OnConnectFinish(shared_ptr<Conn> conn);
    //写缓冲内部
    void EntireWriteWhenEmpty(int fd, ConnWriteBuffer& wb, shared_ptr<char> buff, size_t len);
    void EntireWriteWhenNotEmpty(ConnWriteBuffer& wb, shared_ptr<char> buff, size_t len, bool low);
    //刷写指定队列（written 累加本次实际写入字节数，供 wbSize 扣减；返回：1=完整写完一条，0=部分写或EAGAIN，-1=错误）
    int WriteFrontFromList(int fd, list<shared_ptr<WriteObject>>& lst, size_t& written);
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