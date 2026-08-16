#include "lua-starnet.h"
#include "lua-netpack.h"
#include "stdint.h"
#include "starnet.h"
#include "starnet_service.h"
#include "starnet_timer.h"
#include "starnet_logger.h"
#include <unistd.h>
#include <string.h>
#include <iostream>

//取当前Service上下文（OnInit 时存入注册表）
static Service* GetCurrentService(lua_State *L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "starnet_service");
    Service* srv = (Service*)lua_touserdata(L, -1);
    lua_pop(L, 1);
    return srv;
}

//注册Lua模块
void LuaAPI::Register(lua_State *luaState) {
    
    static luaL_Reg lualibs[] = {
        { "NewService", NewService },
        { "KillService", KillService },
        { "Send", Send },
        { "send_session", SendSession },
        { "self", Self },
        { "genid", Genid },

        { "Listen", Listen },
        { "CloseConn", CloseConn },
        { "Write", Write },
        { "Udp", Udp },
        { "SetUdpAddress", SetUdpAddress },
        { "SendUdp", SendUdp },

        { "name", Name },
        { "localname", LocalName },

        { "getenv", GetEnv },
        { "setenv", SetEnv },

        { "mem", Mem },

        { "log", Log },

        { "timeout", Timeout },
        { NULL, NULL }
    };

    luaL_newlib (luaState, lualibs);
    //网络封包/粘包半包处理子表（对齐 skynet netpack）
    LuaNetpack::Register(luaState);
    lua_setfield(luaState, -2, "netpack");
    lua_setglobal(luaState, "starnet");
}


//开启新服务
int LuaAPI::NewService(lua_State *luaState) {
    //参数个数
    int num = lua_gettop(luaState);//获取参数的个数
    //参数1：服务类型
    if(lua_isstring(luaState, 1) == 0){  //1:是 0:不是
        lua_pushinteger(luaState, -1);
        return 1;
    }
    size_t len = 0;
    const char *type = lua_tolstring(luaState, 1, &len);
    auto t = make_shared<string>(type, len);  //直接用 Lua 字符串构造，避免裸 new 泄漏
    //处理
    uint32_t id = Starnet::inst->NewService(t);
    //返回值
    lua_pushinteger(luaState, id);
    return 1;
}

int LuaAPI::KillService(lua_State *luaState) {
    //参数
    int num = lua_gettop(luaState);//获取参数的个数
    if(lua_isinteger(luaState, 1) == 0) {
        return 0;
    }
    int id = lua_tointeger(luaState, 1);
    //处理
    Starnet::inst->KillService(id);
    //返回值
    //（无）
    return 0;
}

//发送消息
int LuaAPI::Send(lua_State *luaState) {
    //参数总数
    int num = lua_gettop(luaState);
    if(num != 3) {
        starnet_error("Send fail, num err");
        return 0;
    }
    //参数1:我是谁
    if(lua_isinteger(luaState, 1) == 0) {
        starnet_error("Send fail, arg1 err");
        return 0;
    }
    int source = lua_tointeger(luaState, 1);
    //参数2:发送给谁
    if(lua_isinteger(luaState, 2) == 0) {
        starnet_error("Send fail, arg2 err");
        return 0;
    }
    int toId = lua_tointeger(luaState, 2);
    //参数3:发送的内容
    if(lua_isstring(luaState, 3) == 0){
        starnet_error("Send fail, arg3 err");
        return 0;
    }
    size_t len = 0;
    const char *text = lua_tolstring(luaState, 3, &len);
    //处理
    auto msg= make_shared<ServiceMsg>();
    msg->type = BaseMsg::TYPE::LUA;
    msg->source = source;
    msg->buff.assign(text, len);
    Starnet::inst->Send(toId, msg);
    //返回值
    //（无）
    return 0;
}

//带session发送（RPC，对齐 skynet c.send(addr, type, session, msg, sz)）
//参数：toId, type, session, buff；source 取当前服务
int LuaAPI::SendSession(lua_State *luaState) {
    //参数1：发送给谁
    if(lua_isinteger(luaState, 1) == 0) {
        starnet_error("SendSession fail, arg1 err");
        return 0;
    }
    int toId = lua_tointeger(luaState, 1);
    //参数2：消息类型
    if(lua_isinteger(luaState, 2) == 0) {
        starnet_error("SendSession fail, arg2 err");
        return 0;
    }
    int type = lua_tointeger(luaState, 2);
    //参数3：session
    if(lua_isinteger(luaState, 3) == 0) {
        starnet_error("SendSession fail, arg3 err");
        return 0;
    }
    int session = lua_tointeger(luaState, 3);
    //参数4：发送的内容
    if(lua_isstring(luaState, 4) == 0){
        starnet_error("SendSession fail, arg4 err");
        return 0;
    }
    size_t len = 0;
    const char *text = lua_tolstring(luaState, 4, &len);
    //处理
    Service* srv = GetCurrentService(luaState);
    auto msg = make_shared<ServiceMsg>();
    msg->type = type;
    msg->source = srv ? srv->id : 0;
    msg->session = session;
    msg->buff.assign(text, len);
    Starnet::inst->Send(toId, msg);
    //返回值
    //（无）
    return 0;
}

//返回当前服务id
int LuaAPI::Self(lua_State *luaState) {
    Service* srv = GetCurrentService(luaState);
    lua_pushinteger(luaState, srv ? srv->id : 0);
    return 1;
}

//分配RPC会话号
int LuaAPI::Genid(lua_State *luaState) {
    Service* srv = GetCurrentService(luaState);
    lua_pushinteger(luaState, srv ? srv->Genid() : 0);
    return 1;
}

//开启网络监听
int LuaAPI::Listen(lua_State *luaState){
    //参数个数
    int num = lua_gettop(luaState);
    //参数1：端口
    if(lua_isinteger(luaState, 1) == 0) {
        lua_pushinteger(luaState, -1);
        return 1;
    }
    int port = lua_tointeger(luaState, 1);
    //参数2：服务Id
    if(lua_isinteger(luaState, 2) == 0) {
        lua_pushinteger(luaState, -1);
        return 1;
    }
    int id = lua_tointeger(luaState, 2);
    //处理
    int fd = Starnet::inst->Listen(port, id);
    //返回值
    lua_pushinteger(luaState, fd);
    return 1;
}

//关闭连接
int LuaAPI::CloseConn(lua_State *luaState){
    //参数个数
    int num = lua_gettop(luaState);
    //参数1：fd
    if(lua_isinteger(luaState, 1) == 0) {
        return 0;
    }
    int fd = lua_tointeger(luaState, 1);
    //处理
    Starnet::inst->CloseConn(fd);
    //返回值
    //（无）
    return 0;
}

//写套接字
int LuaAPI::Write(lua_State *luaState){
    //参数个数
    int num = lua_gettop(luaState);
    //参数1：fd
    if(lua_isinteger(luaState, 1) == 0) {
        lua_pushinteger(luaState, -1);
        return 1;
    }
    int fd = lua_tointeger(luaState, 1);
    //参数2：buff
    if(lua_isstring(luaState, 2) == 0){
        lua_pushinteger(luaState, -1);
        return 1;
    }
    size_t len = 0;
    const char *buff = lua_tolstring(luaState, 2, &len);
    //拷贝缓冲（Lua字符串内存可能被GC回收）
    //注意：new char[] 必须配数组 deleter，否则 delete 释放数组是 UB
    char *newstr = new char[len];
    memcpy(newstr, buff, len);
    //处理（走SocketIO引擎写缓冲）
    int r = Starnet::inst->Write(fd, shared_ptr<char>(newstr, std::default_delete<char[]>()), len);
    //返回值
    lua_pushinteger(luaState, r);
    return 1;
}

//UDP：创建 socket（对齐 skynet c.udp / c.udp_connect / c.udp_listen）
//参数：addr(string), port(int), bind(bool)；返回 fd（-1 失败）
int LuaAPI::Udp(lua_State *luaState){
    if(lua_isstring(luaState, 1) == 0 || lua_isinteger(luaState, 2) == 0) {
        lua_pushinteger(luaState, -1);
        return 1;
    }
    const char* addr = lua_tostring(luaState, 1);
    int port = (int)lua_tointeger(luaState, 2);
    bool bind_ = lua_toboolean(luaState, 3);
    Service* srv = GetCurrentService(luaState);
    int fd = Starnet::inst->Udp(srv ? srv->id : 0, addr, port, bind_);
    lua_pushinteger(luaState, fd);
    return 1;
}

//UDP：设置默认对端地址（对齐 skynet c.udp_connect）
//参数：fd(int), addr(string), port(int)；返回 0 成功 / -1 失败
int LuaAPI::SetUdpAddress(lua_State *luaState){
    if(lua_isinteger(luaState, 1) == 0 || lua_isstring(luaState, 2) == 0 || lua_isinteger(luaState, 3) == 0) {
        lua_pushinteger(luaState, -1);
        return 1;
    }
    int fd = (int)lua_tointeger(luaState, 1);
    const char* addr = lua_tostring(luaState, 2);
    int port = (int)lua_tointeger(luaState, 3);
    int r = Starnet::inst->SetUdpAddress(fd, addr, port);
    lua_pushinteger(luaState, r);
    return 1;
}

//UDP：发送（对齐 skynet c.send_udp）
//参数：fd(int), addr(string 可空), port(int 可空), msg(string)；返回实际发送字节数（-1 失败）
int LuaAPI::SendUdp(lua_State *luaState){
    if(lua_isinteger(luaState, 1) == 0 || lua_isstring(luaState, 4) == 0) {
        lua_pushinteger(luaState, -1);
        return 1;
    }
    int fd = (int)lua_tointeger(luaState, 1);
    const char* addr = lua_isstring(luaState, 2) ? lua_tostring(luaState, 2) : NULL;
    int port = lua_isinteger(luaState, 3) ? (int)lua_tointeger(luaState, 3) : 0;
    size_t len = 0;
    const char* buff = lua_tolstring(luaState, 4, &len);
    char* newstr = new char[len];
    memcpy(newstr, buff, len);
    int r = Starnet::inst->SendUdp(fd, addr, port, shared_ptr<char>(newstr, std::default_delete<char[]>()), len);
    lua_pushinteger(luaState, r);
    return 1;
}

//名字服务：注册本地名（对齐 skynet.name → cmd_name）
//参数：handle, name；'.' 前缀去点（本地名）
int LuaAPI::Name(lua_State *luaState){
    if(lua_isinteger(luaState, 1) == 0 || lua_isstring(luaState, 2) == 0) {
        lua_pushboolean(luaState, 0);
        return 1;
    }
    int handle = (int)lua_tointeger(luaState, 1);
    size_t len = 0;
    const char *name = lua_tolstring(luaState, 2, &len);
    if(len > 0 && name[0] == '.') {
        ++name;
        --len;
    }
    bool ok = Starnet::inst->NameService((uint32_t)handle, name);
    lua_pushboolean(luaState, ok);
    return 1;
}

//名字服务：按名字查 handle（对齐 skynet.localname → cmd_query）
//参数：name；返回 handle（0=未找到）
int LuaAPI::LocalName(lua_State *luaState){
    if(lua_isstring(luaState, 1) == 0) {
        lua_pushinteger(luaState, 0);
        return 1;
    }
    size_t len = 0;
    const char *name = lua_tolstring(luaState, 1, &len);
    if(len > 0 && name[0] == '.') {
        ++name;
        --len;
    }
    uint32_t handle = Starnet::inst->FindServiceByName(name);
    lua_pushinteger(luaState, handle);
    return 1;
}

//环境配置：查询（对齐 skynet.getenv；不存在返回 nil）
int LuaAPI::GetEnv(lua_State *luaState){
    if(lua_isstring(luaState, 1) == 0) {
        lua_pushnil(luaState);
        return 1;
    }
    const char* key = lua_tostring(luaState, 1);
    bool found = false;
    string v = Starnet::inst->GetEnv(key, &found);
    if(!found) {
        lua_pushnil(luaState);
    }
    else {
        lua_pushstring(luaState, v.c_str());
    }
    return 1;
}

//环境配置：设置（对齐 skynet.setenv）
int LuaAPI::SetEnv(lua_State *luaState){
    if(lua_isstring(luaState, 1) == 0 || lua_isstring(luaState, 2) == 0) {
        return 0;
    }
    size_t klen = 0;
    const char* key = lua_tolstring(luaState, 1, &klen);
    size_t vlen = 0;
    const char* val = lua_tolstring(luaState, 2, &vlen);
    string k(key, klen);
    string v(val, vlen);
    Starnet::inst->SetEnv(k.c_str(), v.c_str());
    return 0;
}

//内存统计（对齐 skynet.mem：进程 RSS，KB）
int LuaAPI::Mem(lua_State *luaState){
    size_t used = Starnet::inst->MemoryUsed();
    lua_pushinteger(luaState, (lua_Integer)used);
    return 1;
}

//写日志（对齐 skynet.log：走统一日志器，时间戳 + 级别 + 落盘/stderr）
//参数：多个参数以空格拼接后写入
int LuaAPI::Log(lua_State *luaState){
    int n = lua_gettop(luaState);
    string text;
    for(int i = 1; i <= n; i++) {
        if(i > 1) {
            text += " ";
        }
        size_t len = 0;
        const char* s = lua_tolstring(luaState, i, &len);
        if(s) {
            text.append(s, len);
        }
        else {
            text += lua_typename(luaState, lua_type(luaState, i));
        }
    }
    starnet_log("%s", text.c_str());
    return 0;
}

//注册定时器
int LuaAPI::Timeout(lua_State *luaState){
    //参数个数
    int num = lua_gettop(luaState);
    //参数1：目标服务Id
    if(lua_isinteger(luaState, 1) == 0) {
        lua_pushinteger(luaState, -1);
        return 1;
    }
    int id = lua_tointeger(luaState, 1);
    //参数2：延时（centisecond，1/100秒）
    if(lua_isinteger(luaState, 2) == 0) {
        lua_pushinteger(luaState, -1);
        return 1;
    }
    int time = lua_tointeger(luaState, 2);
    //参数3：session
    if(lua_isinteger(luaState, 3) == 0) {
        lua_pushinteger(luaState, -1);
        return 1;
    }
    int session = lua_tointeger(luaState, 3);
    //处理（对齐 skynet_timeout）
    int r = starnet_timeout(id, time, session);
    //返回值
    lua_pushinteger(luaState, r);
    return 1;
}