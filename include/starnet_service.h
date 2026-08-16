#pragma once
#include <thread>
#include "starnet_msg.h"
#include "starnet_mq.h"
#include <unordered_map>

extern "C"  {  
    #include "lua.h"  
    #include "lauxlib.h"
    #include "lualib.h"  
}  

using namespace std;

class Service {
public:
    //为效率灵活性放在public

    //唯一id
    uint32_t id;
    //类型
    shared_ptr<string> type;
    // 是否正在退出
    bool isExiting = false;
    //二级消息队列（对齐 skynet_mq.c 的 message_queue）
    StarnetMQ mq;
public:       
    //构造和析构函数
    Service();
    ~Service();
    //回调函数（编写服务逻辑）
    void OnInit();
    void OnMsg(shared_ptr<BaseMsg> msg);
    void OnExit();
    //执行消息
    bool ProcessMsg();
    void ProcessMsgs(int max);  
private:
    //Lua虚拟机
    lua_State *luaState;
private:
    //消息处理方法
    void OnServiceMsg(shared_ptr<ServiceMsg> msg);
    void OnAcceptMsg(shared_ptr<SocketAcceptMsg> msg);
    void OnRWMsg(shared_ptr<SocketRWMsg> msg);
    void OnTimeout(shared_ptr<TimerMsg> msg);
    void OnSocketData(int fd, const char* buff, int len);
    void OnSocketClose(int fd);
};