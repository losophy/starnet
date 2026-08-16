#include "starnet_socket_server.h"
#include <iostream>
#include <unistd.h>
#include <assert.h>
#include <errno.h>
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
    pthread_spin_init(&writeBuffersLock, 0);
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
    auto msg = make_shared<SocketMsg>();
    msg->type = BaseMsg::TYPE::SOCKET;
    msg->subtype = SocketMsg::SUBTYPE::ACCEPT;
    msg->listenFd = conn->fd;
    msg->fd = clientFd;
    if(listener) {
        listener->OnSocketMsg(msg, conn->serviceId);
    }
}

//可读可写
void SocketServer::OnRW(shared_ptr<Conn> conn, bool r, bool w) {
    cout << "OnRW fd:" << conn->fd << endl;
    //可写：由引擎内部刷写缓冲（对齐 socket_server.c，不通知服务）
    if(w) {
        OnWriteable(conn->fd);
    }
    //可读：通知服务（读逻辑暂留业务层）
    if(r) {
        auto msg = make_shared<SocketMsg>();
        msg->type = BaseMsg::TYPE::SOCKET;
        msg->subtype = SocketMsg::SUBTYPE::DATA;
        msg->fd = conn->fd;
        if(listener) {
            listener->OnSocketMsg(msg, conn->serviceId);
        }
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
    //清理写缓冲
    pthread_spin_lock(&writeBuffersLock);
    {
        writeBuffers.erase(fd);
    }
    pthread_spin_unlock(&writeBuffersLock);
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

//（写缓冲）无待写数据，先尝试直写
void SocketServer::EntireWriteWhenEmpty(int fd, ConnWriteBuffer& wb, shared_ptr<char> buff, size_t len) {
    char* s = buff.get();
    //谨记：>=0, -1&&EAGAIN, -1&&EINTR, -1&&其他
    int n = write(fd, s, len);
    if(n < 0 && errno == EINTR) { }; //仅提醒你要注意
    //情况1-1：全部写完
    if(n >= 0 && n == (int)len) {
        return;
    }
    //情况1-2：写一部分（或没写入）
    if( (n > 0 && n < (int)len) || (n < 0 && errno == EAGAIN) ) {
        auto obj = make_shared<WriteObject>();
        obj->start = n;
        obj->buff = buff;
        obj->len = len;
        wb.objs.push_back(obj);
        //请求EPOLLOUT（引擎内部）
        ModifyEvent(fd, true);
        return;
    }
    //情况1-3：真的发生错误
    cout << "EntireWriteWhenEmpty write error " << endl;
}

//（写缓冲）有待写数据，添加到末尾
void SocketServer::EntireWriteWhenNotEmpty(ConnWriteBuffer& wb, shared_ptr<char> buff, size_t len) {
    auto obj = make_shared<WriteObject>();
    obj->start = 0;
    obj->buff = buff;
    obj->len = len;
    wb.objs.push_back(obj);
}

//返回值:是否完整的写入了一条
bool SocketServer::WriteFrontObj(int fd, ConnWriteBuffer& wb) {
    //没待写数据
    if(wb.objs.empty()) {
        return false;
    }
    //获取第一条
    auto obj = wb.objs.front();

    //谨记：>=0, -1&&EAGAIN, -1&&EINTR, -1&&其他
    char* s = obj->buff.get() + obj->start;
    int len = obj->len - obj->start;
    int n = write(fd, s, len);
    if(n < 0 && errno == EINTR) { }; //仅提醒你要注意
    //情况1-1：全部写完
    if(n >= 0 && n == len) {
        wb.objs.pop_front(); //出队
        return true;
    }
    //情况1-2：写一部分（或没写入）
    if( (n > 0 && n < len) || (n < 0 && errno == EAGAIN) ) {
        obj->start += n;
        return false;
    }
    //情况1-3：真的发生错误
    cout << "WriteFrontObj write error " << endl;
    return false;
}

//（写缓冲）发送缓冲（worker线程调用）
int SocketServer::SendBuffer(int fd, shared_ptr<char> buff, size_t len) {
    if(GetConn(fd) == NULL) {
        return -1;
    }
    pthread_spin_lock(&writeBuffersLock);
    {
        ConnWriteBuffer& wb = writeBuffers[fd];
        pthread_spin_lock(&wb.lock);
        {
            if(wb.isClosing) {
                pthread_spin_unlock(&wb.lock);
                pthread_spin_unlock(&writeBuffersLock);
                return -1;
            }
            //情况1：没有待写入数据，先尝试写入
            if(wb.objs.empty()) {
                EntireWriteWhenEmpty(fd, wb, buff, len);
            }
            //情况2：有待写入数据，添加到末尾
            else {
                EntireWriteWhenNotEmpty(wb, buff, len);
            }
        }
        pthread_spin_unlock(&wb.lock);
    }
    pthread_spin_unlock(&writeBuffersLock);
    return 0;
}

//（写缓冲）EPOLLOUT 触发时刷写（socket线程调用）
void SocketServer::OnWriteable(int fd) {
    auto conn = GetConn(fd);
    if(conn == NULL){ //连接已关闭
        return;
    }

    bool needNotify = false;
    bool emptied = false;
    pthread_spin_lock(&writeBuffersLock);
    {
        auto iter = writeBuffers.find(fd);
        if(iter != writeBuffers.end()) {
            ConnWriteBuffer& wb = iter->second;
            pthread_spin_lock(&wb.lock);
            {
                while(WriteFrontObj(fd, wb)) {
                    //循环
                }
                if(wb.objs.empty()) {
                    emptied = true;
                    needNotify = wb.isClosing;
                }
            }
            pthread_spin_unlock(&wb.lock);
        }
    }
    pthread_spin_unlock(&writeBuffersLock);

    //解锁后再操作（避免持锁调用）
    if(needNotify) {
        //通知服务，此处并不是通用做法
        //让read产生 Bad file descriptor报错
        cout << "linger close conn" << endl;
        shutdown(fd, SHUT_RD);
        auto msg = make_shared<SocketMsg>();
        msg->type = BaseMsg::TYPE::SOCKET;
        msg->subtype = SocketMsg::SUBTYPE::CLOSE;
        msg->fd = conn->fd;
        if(listener) {
            listener->OnSocketMsg(msg, conn->serviceId);
        }
    }
    else if(emptied) {
        //刷完关闭EPOLLOUT
        ModifyEvent(fd, false);
    }
    //否则还有数据待写，保持EPOLLOUT继续触发
}

//（写缓冲）全部发完再关闭
void SocketServer::LingerClose(int fd) {
    bool empty = false;
    pthread_spin_lock(&writeBuffersLock);
    {
        auto iter = writeBuffers.find(fd);
        if(iter == writeBuffers.end()) {
            pthread_spin_unlock(&writeBuffersLock);
            RemoveConn(fd);
            close(fd);
            RemoveEvent(fd);
            return;
        }
        ConnWriteBuffer& wb = iter->second;
        pthread_spin_lock(&wb.lock);
        {
            if(wb.isClosing) {
                pthread_spin_unlock(&wb.lock);
                pthread_spin_unlock(&writeBuffersLock);
                return;
            }
            wb.isClosing = true;
            empty = wb.objs.empty();
        }
        pthread_spin_unlock(&wb.lock);
    }
    pthread_spin_unlock(&writeBuffersLock);
    //解锁后再关闭，避免锁内调用RemoveConn（其内部会加writeBuffersLock）
    if(empty) {
        RemoveConn(fd);
        close(fd);
        RemoveEvent(fd);
    }
}