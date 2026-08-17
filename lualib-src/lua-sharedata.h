#pragma once

extern "C"  {
    #include "lua.h"
}

//共享只读数据 Lua 绑定（对齐 skynet lualib-src/lua-sharedata.c）
//注册 starnet.sharedata 子表（挂在 starnet 表下，注册后子表在栈顶由调用方 setfield）
class LuaSharedata {
public:
    static void Register(lua_State *luaState);
};