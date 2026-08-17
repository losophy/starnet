#include "starnet_config.h"
#include "starnet_logger.h"
#include <iostream>
#include <stdio.h>

extern "C" {
    #include "lua.h"
    #include "lauxlib.h"
    #include "lualib.h"
}

//默认配置
StarnetConfig StarnetConfig::Default() {
    StarnetConfig cfg;
    cfg.service = "../service/?/init.lua;../examples/?.lua";
    cfg.start = "main";
    cfg.thread = 3;
    cfg.luaPath = "../lualib/?.lua";
    cfg.logger = "";  //默认 stderr
    cfg.daemon = "";  //默认前台运行
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
        starnet_error("config load fail: %s", lua_tostring(L, -1));
        lua_close(L);
        return cfg;
    }
    status = lua_pcall(L, 0, 1, 0);
    if(status != 0) {
        starnet_error("config run fail: %s", lua_tostring(L, -1));
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

        lua_getfield(L, -1, "logger");
        if(lua_isstring(L, -1)) {
            cfg.logger = lua_tostring(L, -1);
        }
        lua_pop(L, 1);

        lua_getfield(L, -1, "daemon");
        if(lua_isstring(L, -1)) {
            cfg.daemon = lua_tostring(L, -1);
        }
        lua_pop(L, 1);

        //导入全部顶层标量键到 env（对齐 skynet 把 config 表设为全局环境）
        lua_pushnil(L);
        while(lua_next(L, -2)) {
            //key 在 -2，value 在 -1
            if(lua_isstring(L, -2)) {
                const char* k = lua_tostring(L, -2);
                switch(lua_type(L, -1)) {
                    case LUA_TSTRING: {
                        size_t l = 0;
                        const char* v = lua_tolstring(L, -1, &l);
                        cfg.env[k] = string(v, l);
                        break;
                    }
                    case LUA_TBOOLEAN:
                        cfg.env[k] = lua_toboolean(L, -1) ? "true" : "false";
                        break;
                    case LUA_TNUMBER: {
                        char buf[64];
                        snprintf(buf, sizeof(buf), "%lld", (long long)lua_tointeger(L, -1));
                        cfg.env[k] = buf;
                        break;
                    }
                    default:
                        break;  //嵌套表等忽略（config 为标量键值对）
                }
            }
            lua_pop(L, 1);
        }
    }
    lua_close(L);
    return cfg;
}