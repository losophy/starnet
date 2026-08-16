#pragma once

extern "C"  {
    #include "lua.h"
}

//网络封包/粘包半包处理（对齐 skynet lualib-src/lua-netpack.c）
class LuaNetpack {
public:
    //注册 netpack 子表（挂在 starnet 表下，注册后子表在栈顶由调用方 setfield）
    static void Register(lua_State *luaState);
};