#include "starnet_service.h"
#include "starnet.h"
#include <iostream>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include "lua-starnet.h"

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
        cout << "call lua " << func << " fail " << lua_tostring(luaState, -1) << endl;
        lua_pop(luaState, 1);
    }
}

//创建服务后触发
void Service::OnInit() {
    cout << "[" << id <<"] OnInit"  << endl;
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
        cout << "require starnet fail " << lua_tostring(luaState, -1) << endl;
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
                cout << "run lua fail:" << lua_tostring(luaState, -1) << endl;
            }
            break;
        }
        else {
            //加载失败（文件不存在或语法错误），继续尝试下一个模板
            lua_pop(luaState, 1);
        }
    }
    if(!loaded) {
        cout << "service not found: " << *type << endl;
    }
}

//收到服务间消息（LUA请求 或 RESPONSE响应）
void Service::OnServiceMsg(shared_ptr<ServiceMsg> msg) {
    //调 Lua 宿主调度入口（协程化分发，对齐 skynet.dispatch_message）
    lua_pushinteger(luaState, msg->type);
    lua_pushinteger(luaState, msg->session);
    lua_pushinteger(luaState, msg->source);
    if(msg->buff && msg->size > 0) {
        lua_pushlstring(luaState, msg->buff.get(), msg->size);
    }
    else {
        lua_pushlstring(luaState, "", 0);
    }
    lua_pushinteger(luaState, (int)msg->size);
    CallStarnetLua("dispatch_message", 5);
}

//socket 消息（accept / data / close，对齐 skynet PTYPE_SOCKET + SKYNET_SOCKET_TYPE_*）
void Service::OnSocketMsg(shared_ptr<SocketMsg> msg) {
    //新连接
    if(msg->subtype == SocketMsg::SUBTYPE::ACCEPT) {
        cout << "OnAcceptMsg " << msg->fd << endl;
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
    //套接字可读（DATA）
    int fd = msg->fd;
    const int BUFFSIZE = 512;
    char buff[BUFFSIZE];
    int len = 0;
    do {
        len = read(fd, buff, BUFFSIZE);
        if(len > 0){
            //协程化分发 socket 数据（对齐 skynet.dispatch("socket", ...)）
            lua_pushinteger(luaState, SocketMsg::SUBTYPE::DATA);
            lua_pushinteger(luaState, fd);
            lua_pushlstring(luaState, buff, len);
            lua_pushinteger(luaState, len);
            CallStarnetLua("dispatch_socket", 4);
        }
    }while(len == BUFFSIZE);

    if(len <= 0 && errno != EAGAIN) {
        if(Starnet::inst->GetConn(fd)) {
            //关闭通知（协程化分发 close）
            lua_pushinteger(luaState, SocketMsg::SUBTYPE::CLOSE);
            lua_pushinteger(luaState, fd);
            lua_pushnil(luaState);
            lua_pushinteger(luaState, -1);
            CallStarnetLua("dispatch_socket", 4);
            Starnet::inst->CloseConn(fd);
        }
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
    cout << "[" << id <<"] OnExit"  << endl;
    //调用Lua函数（新风格脚本无全局 OnExit，需判空）
    lua_getglobal(luaState, "OnExit"); 
    if(lua_isfunction(luaState, -1)) {
        int isok = lua_pcall(luaState, 0, 0, 0);
        if(isok != 0){ //成功返回值为0，否则代表失败.
             cout << "call lua OnExit fail " << 
                lua_tostring(luaState, -1) << endl;
        }
    }
    else {
        lua_pop(luaState, 1);
    }
    //关闭lua虚拟机
    lua_close(luaState);
}