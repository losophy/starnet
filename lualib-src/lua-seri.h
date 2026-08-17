#pragma once

extern "C"  {
    #include "lua.h"
}

//序列化（对齐 skynet lualib-src/lua-seri.c）
//starnet 简化：打包返回字符串（GC 自管），不做 skynet 的 userdata+len 手动释放方案
class LuaSeri {
public:
    //序列化栈上参数为原始字节字符串（对齐 skynet.serialize）
    static int Serialize(lua_State *luaState);
    //反序列化字符串为多个返回值（对齐 skynet.unserialize）
    static int Unserialize(lua_State *luaState);
    //序列化 + 4 字节大端长度前缀（网络/跨节点友好，对齐 skynet.packstring）
    static int PackString(lua_State *luaState);
    //读 4 字节大端长度前缀 + 反序列化（对齐 skynet.unpackstring）
    static int UnpackString(lua_State *luaState);
};