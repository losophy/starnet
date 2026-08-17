#pragma once

extern "C"  {  
    #include "lua.h"  
}  

#include "lua-seri.h"

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
    //写套接字（低优先级，对齐 skynet socket.send_low）
    static int WriteLow(lua_State *luaState);

    //主动连接（对齐 skynet c.connect）
    static int Connect(lua_State *luaState);

    //绑定已有 fd（对齐 skynet c.bind）
    static int Bind(lua_State *luaState);

    //连接控制（对齐 skynet socket.nodelay / pause / start / shutdown）
    static int NoDelay(lua_State *luaState);
    static int Pause(lua_State *luaState);
    static int Start(lua_State *luaState);
    static int Shutdown(lua_State *luaState);

    //连接状态查询（对齐 skynet socket_info.h；Lua 侧 starnet.socket.info(fd)）
    static int SocketInfo(lua_State *luaState);

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

    //优雅全局退出（对齐 skynet：业务主动触发停机，引擎排空 + 收尾）
    static int GlobalExit(lua_State *luaState);
    //立即终止进程（非优雅，对齐 skynet 的 ABORT 命令）
    static int Abort(lua_State *luaState);

    //时间（对齐 skynet.now / skynet.starttime / skynet.time）
    static int Now(lua_State *luaState);        //当前 tick（centisecond）
    static int StartTime(lua_State *luaState);  //启动时间戳（unix 秒）
    static int Time(lua_State *luaState);       //当前 unix 秒（now/100 + starttime）

    //性能统计（对齐 skynet 的 cmd_stat：当前服务累计 CPU 秒 / 当前消息耗时秒 / 累计消息数）
    static int Cpu(lua_State *luaState);
    static int MsgTime(lua_State *luaState);    //当前正在处理消息的耗时（秒）
    static int Message(lua_State *luaState);

    //管理命令通道（对齐 skynet.command：STARTTIME / ABORT / STAT）
    static int Command(lua_State *luaState);

    static int Timeout(lua_State *luaState);
};
