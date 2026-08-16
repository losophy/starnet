#include "starnet_socket_server.h"
#include "starnet_logger.h"
#include <iostream>
#include <stdio.h>
#include <unistd.h>
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>

using namespace std;

//打包 sockaddr → udpAddr（二进制：1 字节 family + 2 字节端口 + 4/16 字节 IP，对齐 skynet gen_udp_address）
static string PackUdpAddress(const struct sockaddr* sa) {
    string addr;
    if(sa->sa_family == AF_INET) {
        const struct sockaddr_in* v4 = (const struct sockaddr_in*)sa;
        addr.push_back((char)AF_INET);
        addr.append((const char*)&v4->sin_port, 2);
        addr.append((const char*)&v4->sin_addr, 4);
    }
    else {
        const struct sockaddr_in6* v6 = (const struct sockaddr_in6*)sa;
        addr.push_back((char)AF_INET6);
        addr.append((const char*)&v6->sin6_port, 2);
        addr.append((const char*)&v6->sin6_addr, 16);
    }
    return addr;
}

//解包 udpAddr → sockaddr（供 sendto）
static int UnpackUdpAddress(const string& udpAddr, struct sockaddr_storage* sa, socklen_t* slen) {
    if(udpAddr.size() < 3) return -1;
    int family = (uint8_t)udpAddr[0];
    if(family == AF_INET) {
        if(udpAddr.size() < 7) return -1;
        struct sockaddr_in* v4 = (struct sockaddr_in*)sa;
        memset(v4, 0, sizeof(*v4));
        v4->sin_family = AF_INET;
        memcpy(&v4->sin_port, udpAddr.data() + 1, 2);
        memcpy(&v4->sin_addr, udpAddr.data() + 3, 4);
        *slen = sizeof(*v4);
    }
    else if(family == AF_INET6) {
        if(udpAddr.size() < 19) return -1;
        struct sockaddr_in6* v6 = (struct sockaddr_in6*)sa;
        memset(v6, 0, sizeof(*v6));
        v6->sin6_family = AF_INET6;
        memcpy(&v6->sin6_port, udpAddr.data() + 1, 2);
        memcpy(&v6->sin6_addr, udpAddr.data() + 3, 16);
        *slen = sizeof(*v6);
    }
    else {
        return -1;
    }
    return 0;
}

//初始化
void SocketServer::Init() {
    starnet_log("SocketServer Init");
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
    starnet_log("OnAccept listenFd:%d", conn->fd);
    //步骤1：accept
    int clientFd = accept(conn->fd, NULL, NULL);
    if (clientFd < 0) {
        starnet_error("accept error, errno=%d", errno);
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
		starnet_error("OnAccept epoll_ctl Fail:%s", strerror(errno));
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

//可读可写（socket 线程执行）
void SocketServer::OnRW(shared_ptr<Conn> conn, bool r, bool w) {
    starnet_log("OnRW fd:%d r:%d w:%d", conn->fd, r, w);
    //可写：主动连接中 → 检查连接结果；否则由引擎内部刷写缓冲（对齐 socket_server.c，不通知服务）
    if(w) {
        if(conn->connecting) {
            OnConnectFinish(conn);
        }
        else {
            OnWriteable(conn->fd);
        }
    }
    //可读：连接未完成时忽略（非阻塞 connect 的 EPOLLIN 空读）；读是引擎操作，由 socket 线程读出数据再投递
    if(r && !conn->connecting) {
        if(conn->type == Conn::TYPE::UDP) {
            ReadUdp(conn);
        }
        else {
            ReadData(conn);
        }
    }
}

//主动连接完成：getsockopt 检查结果（对齐 skynet report_connect）
void SocketServer::OnConnectFinish(shared_ptr<Conn> conn) {
    int error = 0;
    socklen_t len = sizeof(error);
    int code = getsockopt(conn->fd, SOL_SOCKET, SO_ERROR, &error, &len);
    if(code < 0 || error) {
        //失败：通知服务（ERROR），清理
        int err = code < 0 ? errno : error;
        starnet_error("connect fail, fd=%d err=%s", conn->fd, strerror(err));
        auto msg = make_shared<SocketMsg>();
        msg->type = BaseMsg::TYPE::SOCKET;
        msg->subtype = SocketMsg::SUBTYPE::ERROR;
        msg->fd = conn->fd;
        msg->buff = strerror(err);
        if(listener) {
            listener->OnSocketMsg(msg, conn->serviceId);
        }
        RemoveConn(conn->fd);
        close(conn->fd);
        RemoveEvent(conn->fd);
    }
    else {
        //成功：恢复只读（必须关 EPOLLOUT，否则 ET 下持续触发忙循环），通知服务（CONNECT 带对端 ip）
        conn->connecting = false;
        ModifyEvent(conn->fd, false);
        struct sockaddr_storage sa;
        socklen_t slen = sizeof(sa);
        string ip;
        if(getpeername(conn->fd, (struct sockaddr*)&sa, &slen) == 0) {
            char buf[INET6_ADDRSTRLEN];
            void* addr = (sa.ss_family == AF_INET) ? (void*)&((struct sockaddr_in*)&sa)->sin_addr : (void*)&((struct sockaddr_in6*)&sa)->sin6_addr;
            if(inet_ntop(sa.ss_family, addr, buf, sizeof(buf))) {
                ip = buf;
            }
        }
        auto msg = make_shared<SocketMsg>();
        msg->type = BaseMsg::TYPE::SOCKET;
        msg->subtype = SocketMsg::SUBTYPE::CONNECT;
        msg->fd = conn->fd;
        msg->buff = ip;
        if(listener) {
            listener->OnSocketMsg(msg, conn->serviceId);
        }
    }
}

//TCP 可读：循环 read 到读完/EAGAIN，每块投递一条 DATA 消息
void SocketServer::ReadData(shared_ptr<Conn> conn) {
    const int BUFFSIZE = 8192;
    char buff[BUFFSIZE];
    int fd = conn->fd;
    for(;;) {
        int len = read(fd, buff, BUFFSIZE);
        if(len > 0) {
            auto msg = make_shared<SocketMsg>();
            msg->type = BaseMsg::TYPE::SOCKET;
            msg->subtype = SocketMsg::SUBTYPE::DATA;
            msg->fd = fd;
            msg->buff.assign(buff, len);
            if(listener) {
                listener->OnSocketMsg(msg, conn->serviceId);
            }
            //内核已读空则停止（ET 下剩余数据会再次触发 EPOLLIN）
            if(len < BUFFSIZE) {
                break;
            }
        }
        else if(len == 0) {
            //EOF：对端关闭
            NotifyClose(conn);
            break;
        }
        else {
            if(errno != EAGAIN && errno != EINTR) {
                //真实错误：关闭
                NotifyClose(conn);
            }
            break;
        }
    }
}

//UDP 可读：循环 recvfrom 到 EAGAIN，每包投递一条 UDP 消息（数据 + 对端地址）
void SocketServer::ReadUdp(shared_ptr<Conn> conn) {
    const int MAX_UDP = 65536;
    char buff[MAX_UDP];
    int fd = conn->fd;
    for(;;) {
        struct sockaddr_storage sa;
        socklen_t slen = sizeof(sa);
        int len = recvfrom(fd, buff, MAX_UDP, 0, (struct sockaddr*)&sa, &slen);
        if(len > 0) {
            auto msg = make_shared<SocketMsg>();
            msg->type = BaseMsg::TYPE::SOCKET;
            msg->subtype = SocketMsg::SUBTYPE::UDP;
            msg->fd = fd;
            msg->buff.assign(buff, len);
            msg->udpAddr = PackUdpAddress((struct sockaddr*)&sa);
            if(listener) {
                listener->OnSocketMsg(msg, conn->serviceId);
            }
        }
        else if(len < 0) {
            if(errno == EAGAIN || errno == EINTR) {
                break;
            }
            starnet_error("recvfrom error, fd=%d errno=%d", fd, errno);
            break;
        }
        else {
            //UDP 空包，忽略继续
            break;
        }
    }
}

//读 EOF/错误：通知 close + 清理（对齐 skynet read==0 → SOCKET_CLOSE）
void SocketServer::NotifyClose(shared_ptr<Conn> conn) {
    auto msg = make_shared<SocketMsg>();
    msg->type = BaseMsg::TYPE::SOCKET;
    msg->subtype = SocketMsg::SUBTYPE::CLOSE;
    msg->fd = conn->fd;
    if(listener) {
        listener->OnSocketMsg(msg, conn->serviceId);
    }
    RemoveConn(conn->fd);
    close(conn->fd);
    RemoveEvent(conn->fd);
}

//处理事件
void SocketServer::OnEvent(epoll_event ev){
    int fd = ev.data.fd;
    auto conn = GetConn(fd);
    if(conn == NULL){
        starnet_error("OnEvent error, conn == NULL, fd=%d", fd);
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
            starnet_error("OnError fd:%d", conn->fd);
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
    starnet_log("AddEvent fd %d", fd);
    //添加到epoll
    struct epoll_event ev;
	ev.events = EPOLLIN | EPOLLET;
	ev.data.fd = fd;
	if (epoll_ctl(epollFd, EPOLL_CTL_ADD, fd, &ev) == -1) {
		starnet_error("AddEvent epoll_ctl Fail:%s", strerror(errno));
	}
}

//跨线程调用
void SocketServer::RemoveEvent(int fd) {
    starnet_log("RemoveEvent fd %d", fd);
    epoll_ctl(epollFd, EPOLL_CTL_DEL, fd, NULL);
}

//跨线程调用
void SocketServer::ModifyEvent(int fd, bool epollOut) {
    starnet_log("ModifyEvent fd %d %d", fd, epollOut);
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
    starnet_error("EntireWriteWhenEmpty write error, fd=%d errno=%d", fd, errno);
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
    starnet_error("WriteFrontObj write error, fd=%d errno=%d", fd, errno);
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
        starnet_log("linger close conn, fd=%d", fd);
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

//UDP：创建 socket（getaddrinfo 支持 IPv4/IPv6；bind_=true 则 bind 到 addr:port，addr 空为任意地址）
int SocketServer::AddUdp(uint32_t serviceId, const char* addr, int port, bool bind_) {
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    if(bind_ && addr == NULL) {
        hints.ai_flags = AI_PASSIVE;
    }
    char portstr[16];
    snprintf(portstr, sizeof(portstr), "%d", port);
    int rc = getaddrinfo(addr, portstr, &hints, &res);
    if(rc != 0 || res == NULL) {
        starnet_error("AddUdp getaddrinfo fail, addr=%s port=%d rc=%d", addr ? addr : "(null)", port, rc);
        return -1;
    }
    int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if(fd < 0) {
        starnet_error("AddUdp socket fail, errno=%d", errno);
        freeaddrinfo(res);
        return -1;
    }
    fcntl(fd, F_SETFL, O_NONBLOCK);
    if(bind_) {
        if(bind(fd, res->ai_addr, res->ai_addrlen) == -1) {
            starnet_error("AddUdp bind fail, addr=%s port=%d errno=%d", addr ? addr : "(null)", port, errno);
            close(fd);
            freeaddrinfo(res);
            return -1;
        }
    }
    freeaddrinfo(res);
    AddConn(fd, serviceId, Conn::TYPE::UDP);
    AddEvent(fd);
    return fd;
}

//UDP：设置默认对端地址（对齐 skynet socket_server_udp_connect，存 Conn.udpAddr）
int SocketServer::SetUdpAddress(int fd, const char* addr, int port) {
    shared_ptr<Conn> conn = GetConn(fd);
    if(conn == NULL) {
        return -1;
    }
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    char portstr[16];
    snprintf(portstr, sizeof(portstr), "%d", port);
    int rc = getaddrinfo(addr, portstr, &hints, &res);
    if(rc != 0 || res == NULL) {
        starnet_error("SetUdpAddress getaddrinfo fail, addr=%s port=%d rc=%d", addr ? addr : "(null)", port, rc);
        return -1;
    }
    conn->udpAddr = PackUdpAddress(res->ai_addr);
    freeaddrinfo(res);
    return 0;
}

//UDP：发送（addr 为空用默认对端；直接 sendto，无写缓冲）
int SocketServer::SendUdp(int fd, const char* addr, int port, shared_ptr<char> buff, size_t len) {
    shared_ptr<Conn> conn = GetConn(fd);
    if(conn == NULL) {
        return -1;
    }
    struct sockaddr_storage sa;
    socklen_t slen;
    if(addr) {
        struct addrinfo hints, *res = NULL;
        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_DGRAM;
        char portstr[16];
        snprintf(portstr, sizeof(portstr), "%d", port);
        int rc = getaddrinfo(addr, portstr, &hints, &res);
        if(rc != 0 || res == NULL) {
            starnet_error("SendUdp getaddrinfo fail, addr=%s port=%d rc=%d", addr, port, rc);
            return -1;
        }
        memcpy(&sa, res->ai_addr, res->ai_addrlen);
        slen = res->ai_addrlen;
        freeaddrinfo(res);
    }
    else {
        if(conn->udpAddr.empty()) {
            starnet_error("SendUdp no default address, fd=%d", fd);
            return -1;
        }
        if(UnpackUdpAddress(conn->udpAddr, &sa, &slen) < 0) {
            starnet_error("SendUdp unpack address fail, fd=%d", fd);
            return -1;
        }
    }
    int n = sendto(fd, buff.get(), len, 0, (struct sockaddr*)&sa, slen);
    if(n < 0) {
        starnet_error("SendUdp sendto fail, fd=%d errno=%d", fd, errno);
        return -1;
    }
    return n;
}

//主动连接（对齐 skynet socket_server_connect：getaddrinfo 支持 IPv4/IPv6 + 非阻塞 connect）
int SocketServer::Connect(uint32_t serviceId, const char* host, int port) {
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    char portstr[16];
    snprintf(portstr, sizeof(portstr), "%d", port);
    int rc = getaddrinfo(host, portstr, &hints, &res);
    if(rc != 0 || res == NULL) {
        starnet_error("Connect getaddrinfo fail, host=%s port=%d rc=%d", host, port, rc);
        return -1;
    }
    //遍历地址：socket + 非阻塞 + connect（失败且非 EINPROGRESS 换下一个）
    int fd = -1;
    int status = -1;
    struct addrinfo* ai = NULL;
    for(ai = res; ai != NULL; ai = ai->ai_next) {
        fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if(fd < 0) {
            continue;
        }
        fcntl(fd, F_SETFL, O_NONBLOCK);
        status = connect(fd, ai->ai_addr, ai->ai_addrlen);
        if(status != 0 && errno != EINPROGRESS) {
            close(fd);
            fd = -1;
            continue;
        }
        break;
    }
    if(fd < 0) {
        starnet_error("Connect socket/connect fail, host=%s port=%d errno=%d", host, port, errno);
        freeaddrinfo(res);
        return -1;
    }
    //管理 + epoll
    AddConn(fd, serviceId, Conn::TYPE::CLIENT);
    auto conn = GetConn(fd);
    if(status == 0) {
        //立即连接成功：投 CONNECT（带对端 ip）
        if(conn) {
            conn->connecting = false;
        }
        AddEvent(fd);
        string ip;
        if(ai) {
            char buf[INET6_ADDRSTRLEN];
            void* addr = (ai->ai_family == AF_INET) ? (void*)&((struct sockaddr_in*)ai->ai_addr)->sin_addr : (void*)&((struct sockaddr_in6*)ai->ai_addr)->sin6_addr;
            if(inet_ntop(ai->ai_family, addr, buf, sizeof(buf))) {
                ip = buf;
            }
        }
        auto msg = make_shared<SocketMsg>();
        msg->type = BaseMsg::TYPE::SOCKET;
        msg->subtype = SocketMsg::SUBTYPE::CONNECT;
        msg->fd = fd;
        msg->buff = ip;
        if(listener) {
            listener->OnSocketMsg(msg, serviceId);
        }
    }
    else {
        //EINPROGRESS：等 EPOLLOUT 触发检查结果（OnRW → OnConnectFinish）
        if(conn) {
            conn->connecting = true;
        }
        AddEvent(fd);
        ModifyEvent(fd, true);
    }
    freeaddrinfo(res);
    return fd;
}