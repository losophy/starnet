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

    static int Timeout(lua_State *luaState);
};
