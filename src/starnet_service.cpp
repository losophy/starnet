#include "starnet_service.h"
#include "starnet.h"
#include "starnet_logger.h"
#include <iostream>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "lua-starnet.h"

//解析 UDP 二进制地址（family + port + ip，对齐 skynet gen_udp_address）→ ip 字符串 + 端口
static void ParseUdpAddr(const string& udpAddr, string& ip, int& port) {
    ip = "";
    port = 0;
    if(udpAddr.size() < 3) {
        return;
    }
    int family = (uint8_t)udpAddr[0];
    uint16_t p = 0;
    if(family == AF_INET && udpAddr.size() >= 7) {
        char buf[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, udpAddr.data() + 3, buf, sizeof(buf));
        ip = buf;
        memcpy(&p, udpAddr.data() + 1, 2);
    }
    else if(family == AF_INET6 && udpAddr.size() >= 19) {
        char buf[INET6_ADDRSTRLEN];
        inet_ntop(AF_INET6, udpAddr.data() + 3, buf, sizeof(buf));
        ip = buf;
        memcpy(&p, udpAddr.data() + 1, 2);
    }
    port = ntohs(p);
}

//构造函数
Service::Service() {
}

//析构函数
Service::~Service(){
}

//处理一条消息，返回值代表是否处理
bool Service::ProcessMsg() {
    shared_ptr<BaseMsg> msg = mq.Pop();
    if(msg) {
        //overload 告警（对齐 skynet_server.c：队列曾超阈值则打日志）
        int overload = mq.Overload();
        if(overload) {
            starnet_error("error: May overload, message queue length = %d", overload);
        }
        OnMsg(msg);
        return true;
    }
    else {
        return false;
    }
} 

//处理N条消息，返回值代表是否处理
void Service::ProcessMsgs(int max) {
    for(int i=0; i<max; i++){
        bool succ = ProcessMsg();
        if(!succ){
            break;
        }
    }
}

//调用全局 starnet 表上的函数（starnet.lua 宿主库定义）
void Service::CallStarnetLua(const char* func, int nargs) {
    lua_getglobal(luaState, "starnet");
    if(lua_isnil(luaState, -1)) {
        lua_pop(luaState, nargs + 1);
        return;
    }
    lua_getfield(luaState, -1, func);
    if(lua_isnil(luaState, -1)) {
        lua_pop(luaState, nargs + 2);
        return;
    }
    //栈布局：... arg1..argn, starnet表, func
    //把函数移动到所有参数之下（函数在下、参数在上，pcall 才能正确取参）
    lua_insert(luaState, -nargs - 2);
    //弹出 starnet 表，栈变为：func, arg1..argn
    lua_pop(luaState, 1);
    int isok = lua_pcall(luaState, nargs, 0, 0);
    if(isok != 0){
        starnet_error("call lua %s fail: %s", func, lua_tostring(luaState, -1));
        lua_pop(luaState, 1);
    }
}

//创建服务后触发
void Service::OnInit() {
    starnet_log("[%u] OnInit", id);
    //新建Lua虚拟机
    luaState = luaL_newstate();
    luaL_openlibs(luaState); 
    //注册Starnet系统API（C绑定，全局 starnet 表）
    LuaAPI::Register(luaState);
    //设置Lua路径（lualib 宿主库，对齐 skynet lua_path）
    string luaPath = Starnet::inst->GetLuaPath();
    lua_getglobal(luaState, "package");
    lua_getfield(luaState, -1, "path");
    const char* old = lua_tostring(luaState, -1);
    string newpath = luaPath + ";" + (old ? old : "");
    lua_pop(luaState, 1);
    lua_pushstring(luaState, newpath.data());
    lua_setfield(luaState, -2, "path");
    lua_pop(luaState, 1); //package
    //加载 starnet 宿主库（require "starnet"，其定义的 Lua 表覆盖全局 starnet）
    lua_getglobal(luaState, "require");
    lua_pushliteral(luaState, "starnet");
    if(lua_pcall(luaState, 1, 1, 0) != 0) {
        starnet_error("require starnet fail: %s", lua_tostring(luaState, -1));
        lua_pop(luaState, 1);
    }
    else {
        lua_pop(luaState, 1); //返回的 starnet 表
    }
    //注册表存 Service 上下文（C绑定 genid/self/send source 使用）
    lua_pushlightuserdata(luaState, this);
    lua_setfield(luaState, LUA_REGISTRYINDEX, "starnet_service");
    //执行Lua文件（按模板顺序查找，对齐 skynet loader.lua 的 LUA_SERVICE 拆分）
    string serviceTemplate = Starnet::inst->GetService();
    bool loaded = false;
    size_t pos = 0;
    while(pos != string::npos) {
        size_t semi = serviceTemplate.find(';', pos);
        string pattern = serviceTemplate.substr(pos, semi == string::npos ? string::npos : semi - pos);
        pos = (semi == string::npos) ? string::npos : semi + 1;
        //?替换为服务名
        string filename = "";
        size_t q = 0;
        while(q != string::npos) {
            size_t found = pattern.find('?', q);
            if(found == string::npos) {
                filename += pattern.substr(q);
                break;
            }
            filename += pattern.substr(q, found - q) + *type;
            q = found + 1;
        }
        int status = luaL_loadfile(luaState, filename.data());
        if(status == 0) {
            loaded = true;
            int err = lua_pcall(luaState, 0, 0, 0);
            if(err != 0) {
                starnet_error("run lua fail: %s", lua_tostring(luaState, -1));
            }
            break;
        }
        else {
            //加载失败（文件不存在或语法错误），继续尝试下一个模板
            lua_pop(luaState, 1);
        }
    }
    if(!loaded) {
        starnet_error("service not found: %s", type->c_str());
    }
}

//收到服务间消息（LUA请求 或 RESPONSE响应）
void Service::OnServiceMsg(shared_ptr<ServiceMsg> msg) {
    //调 Lua 宿主调度入口（协程化分发，对齐 skynet.dispatch_message）
    lua_pushinteger(luaState, msg->type);
    lua_pushinteger(luaState, msg->session);
    lua_pushinteger(luaState, msg->source);
    if(!msg->buff.empty()) {
        lua_pushlstring(luaState, msg->buff.data(), msg->buff.size());
    }
    else {
        lua_pushlstring(luaState, "", 0);
    }
    lua_pushinteger(luaState, (int)msg->buff.size());
    CallStarnetLua("dispatch_message", 5);
}

//socket 消息（accept / data / close，对齐 skynet PTYPE_SOCKET + SKYNET_SOCKET_TYPE_*）
void Service::OnSocketMsg(shared_ptr<SocketMsg> msg) {
    //新连接
    if(msg->subtype == SocketMsg::SUBTYPE::ACCEPT) {
        starnet_log("[%u] OnAccept fd:%d", id, msg->fd);
        //协程化分发 accept（对齐 skynet.dispatch("accept", ...)）
        lua_pushinteger(luaState, SocketMsg::SUBTYPE::ACCEPT);
        lua_pushinteger(luaState, msg->fd);
        lua_pushinteger(luaState, msg->listenFd);
        lua_pushinteger(luaState, 0);
        CallStarnetLua("dispatch_socket", 4);
        return;
    }
    //连接关闭（写缓冲刷完后触发 / 读失败检测）
    if(msg->subtype == SocketMsg::SUBTYPE::CLOSE) {
        //协程化分发 close（对齐 skynet.dispatch("close", ...)）
        lua_pushinteger(luaState, SocketMsg::SUBTYPE::CLOSE);
        lua_pushinteger(luaState, msg->fd);
        lua_pushnil(luaState);
        lua_pushinteger(luaState, -1);
        CallStarnetLua("dispatch_socket", 4);
        return;
    }
    //主动连接成功（对齐 skynet dispatch("connect", fd, ip)）
    if(msg->subtype == SocketMsg::SUBTYPE::CONNECT) {
        lua_pushinteger(luaState, SocketMsg::SUBTYPE::CONNECT);
        lua_pushinteger(luaState, msg->fd);
        lua_pushlstring(luaState, msg->buff.data(), msg->buff.size());
        lua_pushinteger(luaState, 0);
        CallStarnetLua("dispatch_socket", 4);
        return;
    }
    //连接失败等错误（对齐 skynet dispatch("error", fd, err)）
    if(msg->subtype == SocketMsg::SUBTYPE::ERROR) {
        lua_pushinteger(luaState, SocketMsg::SUBTYPE::ERROR);
        lua_pushinteger(luaState, msg->fd);
        lua_pushlstring(luaState, msg->buff.data(), msg->buff.size());
        lua_pushnil(luaState);
        CallStarnetLua("dispatch_socket", 4);
        return;
    }
    //套接字可读（DATA：socket 线程已读出数据，直接分发，对齐 skynet 引擎统一读）
    if(msg->subtype == SocketMsg::SUBTYPE::DATA) {
        //协程化分发 socket 数据（对齐 skynet.dispatch("socket", ...)）
        lua_pushinteger(luaState, SocketMsg::SUBTYPE::DATA);
        lua_pushinteger(luaState, msg->fd);
        lua_pushlstring(luaState, msg->buff.data(), msg->buff.size());
        lua_pushinteger(luaState, (int)msg->buff.size());
        CallStarnetLua("dispatch_socket", 4);
        return;
    }
    //UDP 数据报（报式无粘包，解析对端地址后分发，对齐 skynet.dispatch("udp", fd, msg, addr, port)）
    if(msg->subtype == SocketMsg::SUBTYPE::UDP) {
        string ip;
        int port = 0;
        ParseUdpAddr(msg->udpAddr, ip, port);
        lua_pushinteger(luaState, SocketMsg::SUBTYPE::UDP);
        lua_pushinteger(luaState, msg->fd);
        lua_pushlstring(luaState, msg->buff.data(), msg->buff.size());
        lua_pushstring(luaState, ip.c_str());
        lua_pushinteger(luaState, port);
        CallStarnetLua("dispatch_socket", 5);
        return;
    }
}

//收到消息时触发
void Service::OnMsg(shared_ptr<BaseMsg> msg) {
    //LUA / RESPONSE（服务间消息，含RPC）
    if(msg->type == BaseMsg::TYPE::LUA || msg->type == BaseMsg::TYPE::RESPONSE) {
        auto m = dynamic_pointer_cast<ServiceMsg>(msg);
        OnServiceMsg(m);
    }
    //SOCKET（accept / data / close）
    else if(msg->type == BaseMsg::TYPE::SOCKET) {
        auto m = dynamic_pointer_cast<SocketMsg>(msg);
        OnSocketMsg(m);
    }
}


//退出服务时触发
void Service::OnExit() {
    starnet_log("[%u] OnExit", id);
    //调用Lua函数（新风格脚本无全局 OnExit，需判空）
    lua_getglobal(luaState, "OnExit"); 
    if(lua_isfunction(luaState, -1)) {
        int isok = lua_pcall(luaState, 0, 0, 0);
        if(isok != 0){ //成功返回值为0，否则代表失败.
             starnet_error("call lua OnExit fail: %s", lua_tostring(luaState, -1));
        }
    }
    else {
        lua_pop(luaState, 1);
    }
    //关闭lua虚拟机
    lua_close(luaState);
}