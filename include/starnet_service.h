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
    //RPC会话号自增（对齐 skynet_context.session，C侧 genid 分配）
    uint32_t sessionGen = 0;
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
    //Lua状态（C绑定需要，如 genid 上下文）
    lua_State* GetLuaState() { return luaState; }
    //分配会话号（对齐 skynet_context_newsession）
    uint32_t Genid() { return ++sessionGen; }
private:
    //Lua虚拟机
    lua_State *luaState;
private:
    //消息处理
    void OnServiceMsg(shared_ptr<ServiceMsg> msg);
    void OnSocketMsg(shared_ptr<SocketMsg> msg);
    //调用全局 starnet 表上的 Lua 函数（starnet.lua 宿主库，nargs 为已压栈参数数）
    void CallStarnetLua(const char* func, int nargs);
};