#include "starnet_socket_server.h"
#include <iostream>
#include <unistd.h>
#include <assert.h>
#include <string.h>
#include <fcntl.h>
#include <sys/socket.h>

using namespace std;

//初始化
void SocketServer::Init() {
    cout << "SocketServer Init" << endl;
    //创建epoll
    epollFd = epoll_create(1024); // 返回值：非负数:成功的描述符，-1失败
    assert(epollFd > 0);
    //锁
    pthread_rwlock_init(&connsLock, NULL);
    listener = NULL;
}

//注册事件监听（桥接层）
void SocketServer::SetListener(SocketServerListener* listener) {
    this->listener = listener;
}

//新连接
void SocketServer::OnAccept(shared_ptr<Conn> conn) {
    cout << "OnAccept fd:" << conn->fd << endl;
    //步骤1：accept
    int clientFd = accept(conn->fd, NULL, NULL);
    if (clientFd < 0) {
        cout << "accept error" << endl;
    }
    //步骤2：设置非阻塞
    fcntl(clientFd, F_SETFL, O_NONBLOCK);
    //步骤3：添加到管理结构
    AddConn(clientFd, conn->serviceId, Conn::TYPE::CLIENT);
    //步骤4：添加到epoll
    struct epoll_event ev;
	ev.events = EPOLLIN | EPOLLET;
	ev.data.fd = clientFd;
	if (epoll_ctl(epollFd, EPOLL_CTL_ADD, clientFd, &ev) == -1) {
		cout << "OnAccept epoll_ctl Fail:" << strerror(errno) << endl;
	}
    //步骤5：通知（事件出口，由桥接层投递）
    auto msg = make_shared<SocketAcceptMsg>();
    msg->type = BaseMsg::TYPE::SOCKET_ACCEPT;
    msg->listenFd = conn->fd;
    msg->clientFd = clientFd;
    if(listener) {
        listener->OnAcceptMsg(msg, conn->serviceId);
    }
}

//可读可写
void SocketServer::OnRW(shared_ptr<Conn> conn, bool r, bool w) {
    cout << "OnRW fd:" << conn->fd << endl;
    auto msg = make_shared<SocketRWMsg>();
    msg->type = BaseMsg::TYPE::SOCKET_RW;
    msg->fd = conn->fd;
    msg->isRead = r;
    msg->isWrite = w;
    if(listener) {
        listener->OnRWMsg(msg, conn->serviceId);
    }
}

//处理事件
void SocketServer::OnEvent(epoll_event ev){
    int fd = ev.data.fd;
    auto conn = GetConn(fd);
    if(conn == NULL){
        cout << "OnEvent error, conn == NULL" << endl;
        return;
    }
    //事件类型
    bool isRead = ev.events & EPOLLIN;
    bool isWrite = ev.events & EPOLLOUT;
    bool isError = ev.events & EPOLLERR;
    //监听Socket
    if(conn->type == Conn::TYPE::LISTEN){
        if(isRead) {
            OnAccept(conn);
        }
    }
    //普通Socket
    else {
        if(isRead || isWrite) {
            OnRW(conn, isRead, isWrite);
        }
        if(isError){
            cout << "OnError fd:" << conn->fd << endl;
        }
    }
}

//线程函数
void SocketServer::operator()() {
    while(true) {
        //阻塞等待
        const int EVENT_SIZE = 64;
        struct epoll_event events[EVENT_SIZE];
	    int eventCount = epoll_wait(epollFd , events, EVENT_SIZE, -1);
        //取得事件
        for (int i=0; i<eventCount; i++) {
            epoll_event ev = events[i]; //当前要处理的事件
            OnEvent(ev);
        }
    }
}

//添加连接
int SocketServer::AddConn(int fd, uint32_t id, Conn::TYPE type) {
    auto conn = make_shared<Conn>();
    conn->fd = fd;
    conn->serviceId = id;
    conn->type = type;
    pthread_rwlock_wrlock(&connsLock);
    {
        conns.emplace(fd, conn);
    }
    pthread_rwlock_unlock(&connsLock);
    return fd;
}

//由id查找连接
shared_ptr<Conn> SocketServer::GetConn(int fd) {
    shared_ptr<Conn> conn = NULL;
    pthread_rwlock_rdlock(&connsLock);
    {
        unordered_map<int, shared_ptr<Conn>>::iterator iter = conns.find (fd);
        if (iter != conns.end()){
            conn = iter->second;
        }
    }
    pthread_rwlock_unlock(&connsLock);
    return conn;
}

//删除连接
bool SocketServer::RemoveConn(int fd) {
    int result;
    pthread_rwlock_wrlock(&connsLock);
    {
        result = conns.erase(fd);
    }
    pthread_rwlock_unlock(&connsLock);
    return result == 1;
}

//跨线程调用
void SocketServer::AddEvent(int fd) {
    cout << "AddEvent fd " << fd << endl;
    //添加到epoll
    struct epoll_event ev;
	ev.events = EPOLLIN | EPOLLET;
	ev.data.fd = fd;
	if (epoll_ctl(epollFd, EPOLL_CTL_ADD, fd, &ev) == -1) {
		cout << "AddEvent epoll_ctl Fail:" << strerror(errno) << endl;
	}
}

//跨线程调用
void SocketServer::RemoveEvent(int fd) {
    cout << "RemoveEvent fd " << fd << endl;
    epoll_ctl(epollFd, EPOLL_CTL_DEL, fd, NULL);
}

//跨线程调用
void SocketServer::ModifyEvent(int fd, bool epollOut) {
    cout << "ModifyEvent fd " << fd << " " << epollOut << endl;
    struct epoll_event ev;
    ev.data.fd = fd;

    if(epollOut){
	    ev.events = EPOLLIN | EPOLLET | EPOLLOUT;
    }
    else
    {
        ev.events = EPOLLIN | EPOLLET ;
    }
    epoll_ctl(epollFd, EPOLL_CTL_MOD, fd, &ev);
}