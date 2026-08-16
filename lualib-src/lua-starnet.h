#pragma once

extern "C"  {  
    #include "lua.h"  
}  

using namespace std; 

class LuaAPI {
public:
    static void Register(lua_State *luaState);

    static int NewService(lua_State *luaState);
    static int KillService(lua_State *luaState);
    static int Send(lua_State *luaState);
    //RPC：带session发送（source取当前服务），对齐 skynet c.send(addr,type,session,msg)
    static int SendSession(lua_State *luaState);
    //返回当前服务id（对齐 skynet self）
    static int Self(lua_State *luaState);
    //分配RPC会话号（对齐 skynet_context_newsession）
    static int Genid(lua_State *luaState);

    static int Listen(lua_State *luaState);
    static int CloseConn(lua_State *luaState);
    static int Write(lua_State *luaState);

    //主动连接（对齐 skynet c.connect）
    static int Connect(lua_State *luaState);

    //绑定已有 fd（对齐 skynet c.bind）
    static int Bind(lua_State *luaState);

    //UDP（对齐 skynet c.udp / c.udp_connect / c.send_udp）
    static int Udp(lua_State *luaState);
    static int SetUdpAddress(lua_State *luaState);
    static int SendUdp(lua_State *luaState);

    //名字服务（对齐 skynet name / localname）
    static int Name(lua_State *luaState);
    static int LocalName(lua_State *luaState);

    //环境配置（对齐 skynet getenv / setenv）
    static int GetEnv(lua_State *luaState);
    static int SetEnv(lua_State *luaState);

    //内存统计（对齐 skynet.mem：进程 RSS，KB）
    static int Mem(lua_State *luaState);

    //写日志（对齐 skynet.log）
    static int Log(lua_State *luaState);

    static int Timeout(lua_State *luaState);
};
