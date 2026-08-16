#include "starnet.h"
#include "starnet_start.h"
#include "starnet_timer.h"
#include "starnet_handle.h"
#include "starnet_logger.h"
#include "starnet_env.h"
#include "starnet_mem.h"
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
    //保存配置
    config = cfg;
    //初始化环境配置并导入 config 全部键（对齐 skynet_env + config 搬全局）
    starnet_env_init();
    for(auto& kv : config.env) {
        starnet_setenv(kv.first.c_str(), kv.second.c_str());
    }
    //初始化日志系统（对齐 skynet：logger 服务由 config.logger 指定输出文件）
    if(!starnet_logger_init(cfg.logger.empty() ? NULL : cfg.logger.c_str())) {
        starnet_error("open logger file fail: %s", cfg.logger.c_str());
    }
    starnet_log("Hello Starnet");
    //忽略SIGPIPE信号
    signal(SIGPIPE, SIG_IGN);
    //锁
    pthread_rwlock_init(&servicesLock, NULL);
    starnet_globalmq_init();
    starnet_handle_init();  //名字服务初始化（对齐 skynet_handle_init）
    //初始化定时器（对齐 skynet_start 的 skynet_timer_init）
    starnet_timer_init();
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

//获取Lua模块搜索路径
string Starnet::GetLuaPath() {
    return config.luaPath;
}

//等待
void Starnet::Wait() {
    start->Wait();
}

//新建服务（对齐 skynet_context_new + skynet_handle_register，0 保留）
uint32_t Starnet::NewService(shared_ptr<string> type) {
    auto srv = make_shared<Service>();
    srv->type = type;
    pthread_rwlock_wrlock(&servicesLock);
    {
        srv->id = ++maxId;  //id 从 1 开始（0 保留，对齐 skynet handle）
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

//删除服务（异步退休，跨线程安全，对齐 skynet_handle_retire）
//不在此处执行 OnExit/lua_close：由 worker 线程在安全点执行（见 Worker::CheckAndPutGlobal）
void Starnet::KillService(uint32_t id) {
    shared_ptr<Service> srv = GetService(id);
    if(!srv){
        return;
    }
    //摘除：不再可被按 id 寻址
    pthread_rwlock_wrlock(&servicesLock);
    {
        services.erase(id);
    }
    pthread_rwlock_unlock(&servicesLock);
    //清名字
    starnet_handle_removename(id);
    //标记退出
    srv->isExiting = true;
    //兜底：若服务空闲（不在全局队列），重新入队确保 worker 处理退出
    if(!srv->mq.IsInGlobal()) {
        if(srv->mq.TryEnterGlobal(srv)) {
            start->CheckAndWeakUp();
        }
    }
}


//名字服务：注册本地名（对齐 skynet_handle_namehandle）
bool Starnet::NameService(uint32_t handle, const char* name) {
    return starnet_handle_namehandle(handle, name);
}

//名字服务：按名字查 handle（0=未找到，对齐 skynet_handle_findname）
uint32_t Starnet::FindServiceByName(const char* name) {
    return starnet_handle_findname(name);
}

//环境配置：查询（对齐 skynet_getenv）
string Starnet::GetEnv(const char* key, bool* found) {
    return starnet_getenv(key, found);
}

//环境配置：设置（对齐 skynet_setenv）
void Starnet::SetEnv(const char* key, const char* value) {
    starnet_setenv(key, value);
}

//内存统计：进程常驻内存 KB（对齐 skynet.mem / mem_info）
size_t Starnet::MemoryUsed() {
    return starnet_memory_used();
}

//发送消息
void Starnet::Send(uint32_t toId, shared_ptr<BaseMsg> msg){
    shared_ptr<Service> toSrv = GetService(toId);
    if(!toSrv){
        starnet_error("Send fail, toSrv not exist toId:%u", toId);
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

//仅测试用，buff 由调用方管理（MakeMsg 内部拷贝）
shared_ptr<BaseMsg> Starnet::MakeMsg(uint32_t source, char* buff, int len) {
    auto msg= make_shared<ServiceMsg>();
    msg->type = BaseMsg::TYPE::LUA;
    msg->source = source;
    msg->buff.assign(buff, len);
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
        starnet_error("listen error, listenFd <= 0");
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
        starnet_error("listen error, bind fail, errno=%d", errno);
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