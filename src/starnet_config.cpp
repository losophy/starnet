#include "starnet_config.h"
#include <iostream>

extern "C" {
    #include "lua.h"
    #include "lauxlib.h"
    #include "lualib.h"
}

//默认配置
StarnetConfig StarnetConfig::Default() {
    StarnetConfig cfg;
    cfg.service = "../service/?/init.lua;../examples/?/init.lua";
    cfg.start = "main";
    cfg.thread = 3;
    cfg.luaPath = "../lualib/?.lua";
    return cfg;
}

//从config.lua加载（文件return一个k/v表）
StarnetConfig StarnetConfig::Load(const char* filename) {
    StarnetConfig cfg = Default();

    if(filename == NULL) {
        return cfg;
    }

    lua_State* L = luaL_newstate();
    luaL_openlibs(L);
    int status = luaL_loadfile(L, filename);
    if(status != 0) {
        cout << "config load fail: " << lua_tostring(L, -1) << endl;
        lua_close(L);
        return cfg;
    }
    status = lua_pcall(L, 0, 1, 0);
    if(status != 0) {
        cout << "config run fail: " << lua_tostring(L, -1) << endl;
        lua_close(L);
        return cfg;
    }
    //提取字段（对齐 skynet _init_env：把config表转成k/v）
    if(lua_istable(L, -1)) {
        lua_getfield(L, -1, "luaservice");
        if(lua_isstring(L, -1)) {
            cfg.service = lua_tostring(L, -1);
        }
        lua_pop(L, 1);

        lua_getfield(L, -1, "start");
        if(lua_isstring(L, -1)) {
            cfg.start = lua_tostring(L, -1);
        }
        lua_pop(L, 1);

        lua_getfield(L, -1, "thread");
        if(lua_isinteger(L, -1)) {
            cfg.thread = lua_tointeger(L, -1);
        }
        lua_pop(L, 1);

        lua_getfield(L, -1, "luaPath");
        if(lua_isstring(L, -1)) {
            cfg.luaPath = lua_tostring(L, -1);
        }
        lua_pop(L, 1);
    }
    lua_close(L);
    return cfg;
}