#pragma once
#include <string>
#include <unordered_map>

using namespace std;

//启动配置（对齐 skynet config：luaservice / start / thread / lua_path / logger）
struct StarnetConfig {
    //服务搜索模板（;分隔多路径，?为服务名占位，对齐 skynet 的 luaservice/LUA_SERVICE）
    string service;
    //启动服务名（对齐 skynet 的 start）
    string start;
    //worker线程数（对齐 skynet 的 thread）
    int thread;
    //Lua模块搜索路径（lualib 宿主库，对齐 skynet 的 lua_path）
    string luaPath;
    //日志输出文件（对齐 skynet 的 logger）；空串表示写 stderr
    string logger;
    //守护进程化 pidfile（对齐 skynet 的 daemon）；空串表示前台运行
    string daemon;
    //性能统计开关（对齐 skynet 的 profile；服务构造时复制到自身）
    bool profile;
    //完整配置 k/v（对齐 skynet env：config 全部顶层标量键，运行时可 getenv/setenv 查询修改）
    unordered_map<string, string> env;

    //默认配置（对齐 skynet optstring 的默认值精神）
    static StarnetConfig Default();

    //从config.lua加载（文件return一个k/v表）；失败时用默认配置
    static StarnetConfig Load(const char* filename);
};