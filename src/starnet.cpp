#include "starnet.h"
#include "starnet_start.h"
#include <iostream>
#include <assert.h>

#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>

using namespace std;

//单例
Starnet* Starnet::inst;
Starnet::Starnet(){
    inst = this;
}

//开启系统（对齐 skynet_start(&config)）
void Starnet::Start(StarnetConfig& cfg) {
    cout << "Hello Starnet" << endl;
    //保存配置
    config = cfg;
    //忽略SIGPIPE信号
    signal(SIGPIPE, SIG_IGN);
    //锁
    pthread_rwlock_init(&servicesLock, NULL);
    starnet_globalmq_init();
    //开启系统线程池
    start = new StarnetStart();
    start->SetWorkerNum(config.thread);
    start->Start();
    socketServer = start->GetSocketServer();
}

//获取服务搜索模板
string Starnet::GetService() {
    return config.service;
}

//等待
void Starnet::Wait() {
    start->Wait();
}

//新建服务
uint32_t Starnet::NewService(shared_ptr<string> type) {
    auto srv = make_shared<Service>();
    srv->type = type;
    pthread_rwlock_wrlock(&servicesLock);
    {
        srv->id = maxId; 
        maxId++;
        services.emplace(srv->id, srv);
    }
    pthread_rwlock_unlock(&servicesLock);
    srv->OnInit(); //初始化
    return srv->id;
}

//由id查找服务
shared_ptr<Service> Starnet::GetService(uint32_t id) {
    shared_ptr<Service> srv = NULL;
    pthread_rwlock_rdlock(&servicesLock);
    {
        unordered_map<uint32_t, shared_ptr<Service>>::iterator iter = services.find (id);
        if (iter != services.end()){
            srv = iter->second;
        }
    }
    pthread_rwlock_unlock(&servicesLock);
    return srv;
}

//删除服务
//只能service自己调自己，因为srv->OnExit、srv->isExiting不加锁
void Starnet::KillService(uint32_t id) {
    shared_ptr<Service> srv = GetService(id);
    if(!srv){
        return;
    }
    //退出前
    srv->OnExit();
    srv->isExiting = true;
    //删列表
    pthread_rwlock_wrlock(&servicesLock);
    {
        services.erase(id);
    }
    pthread_rwlock_unlock(&servicesLock);
}


//发送消息
void Starnet::Send(uint32_t toId, shared_ptr<BaseMsg> msg){
    shared_ptr<Service> toSrv = GetService(toId);
    if(!toSrv){
        cout << "Send fail, toSrv not exist toId:" << toId << endl;
        return;
    }
    toSrv->mq.Push(msg);
    //检查并放入全局队列
    //为缩小临界区灵活控制，破坏封装性
    //唤起进程，不放在临界区里面
    if(toSrv->mq.TryEnterGlobal(toSrv)) {
        start->CheckAndWeakUp();
    }
}

//仅测试用，buff须由new产生
shared_ptr<BaseMsg> Starnet::MakeMsg(uint32_t source, char* buff, int len) {
    auto msg= make_shared<ServiceMsg>();
    msg->type = BaseMsg::TYPE::SERVICE;
    msg->source = source;
    //基本类型的对象没有析构函数
    //所以回收基本类型组成的数组空间用delete 和 delete[]都可以
    //无需重新析构方法
    msg->buff = shared_ptr<char>(buff);
    msg->size = len;
    return msg;
}

//添加连接（转发到SocketIO引擎）
int Starnet::AddConn(int fd, uint32_t id, Conn::TYPE type) {
    return socketServer->AddConn(fd, id, type);
}

//由id查找连接（转发到SocketIO引擎）
shared_ptr<Conn> Starnet::GetConn(int fd) {
    return socketServer->GetConn(fd);
}

//删除连接（转发到SocketIO引擎）
bool Starnet::RemoveConn(int fd) {
    return socketServer->RemoveConn(fd);
}

int Starnet::Listen(uint32_t port, uint32_t serviceId) {
    //创建Socket
    int listenFd = socket(AF_INET, SOCK_STREAM, 0);
    if(listenFd <= 0){
        cout << "listen error, listenFd <= 0" << endl;
        return -1;
    }
    fcntl(listenFd, F_SETFL, O_NONBLOCK);
    //创建地址结构
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    //bind
    int r = bind(listenFd, (struct sockaddr*)&addr, sizeof(addr));
    if( r == -1){
        cout << "listen error, bind fail" << endl;
        return -1;
    }
    //listen
    r = listen(listenFd, 64); //see
    if(r < 0){
        return -1;
    }
    //添加到管理结构
    AddConn(listenFd, serviceId, Conn::TYPE::LISTEN);
    //Epoll事件（跨线程）
    socketServer->AddEvent(listenFd);
    return listenFd;
}


void Starnet::CloseConn(uint32_t fd) {
    //删除管理结构
    bool succ = RemoveConn(fd);
    //关闭
    close(fd);
    //Epoll事件（跨线程）
    if(succ) {
        socketServer->RemoveEvent(fd);
    }
}

void Starnet::ModifyEvent(int fd, bool epollOut) {
    socketServer->ModifyEvent(fd, epollOut);
}

//发送缓冲（转发到SocketIO引擎写缓冲）
int Starnet::Write(int fd, shared_ptr<char> buff, size_t len) {
    return socketServer->SendBuffer(fd, buff, len);
}