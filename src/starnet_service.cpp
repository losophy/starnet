#include "starnet_service.h"
#include "starnet.h"
#include <iostream>
#include <unistd.h>
#include <string.h>
#include "lua-starnet.h"

//构造函数
Service::Service() {
}

//析构函数
Service::~Service(){
}

//处理一条消息，返回值代表是否处理
bool Service::ProcessMsg() {
    shared_ptr<BaseMsg> msg = mq.Pop();
    if(msg) {
        OnMsg(msg);
        return true;
    }
    else {
        return false;
    }
} 

//处理N条消息，返回值代表是否处理
void Service::ProcessMsgs(int max) {
    for(int i=0; i<max; i++){
        bool succ = ProcessMsg();
        if(!succ){
            break;
        }
    }
}

//创建服务后触发
void Service::OnInit() {
    cout << "[" << id <<"] OnInit"  << endl;
    //新建Lua虚拟机
    luaState = luaL_newstate();
    luaL_openlibs(luaState); 
    //注册Starnet系统API
    LuaAPI::Register(luaState);
    //执行Lua文件
    string filename = "../service/" + *type + "/init.lua";
    int isok = luaL_dofile(luaState, filename.data());
    if(isok == 1){ //成功返回值为0，失败则为1.
         cout << "run lua fail:" << lua_tostring(luaState, -1) << endl;
    }
    //调用Lua函数
    lua_getglobal(luaState, "OnInit"); 
    lua_pushinteger(luaState, id); 
    isok = lua_pcall(luaState, 1, 0, 0);
    if(isok != 0){ //成功返回值为0，否则代表失败.
         cout << "call lua OnInit fail " << lua_tostring(luaState, -1) << endl;
    }
}

//收到客户端数据
void Service::OnSocketData(int fd, const char* buff, int len) {
    //调用Lua函数
    lua_getglobal(luaState, "OnSocketData"); 
    lua_pushinteger(luaState, fd); 
    lua_pushlstring(luaState, buff,len); 
    int isok = lua_pcall(luaState, 2, 0, 0);
    if(isok != 0){ //成功返回值为0，否则代表失败.
         cout << "call lua OnSocketData fail " << lua_tostring(luaState, -1) << endl;
    }
}

//关闭连接前
void Service::OnSocketClose(int fd) {
    cout << "OnSocketClose " << fd << endl;

    //调用Lua函数
    lua_getglobal(luaState, "OnSocketClose"); 
    lua_pushinteger(luaState, fd); 
    int isok = lua_pcall(luaState, 1, 0, 0);
    if(isok != 0){ //成功返回值为0，否则代表失败.
         cout << "call lua OnSocketClose fail " << lua_tostring(luaState, -1) << endl;
    }
}

//收到其他服务发来的消息
void Service::OnServiceMsg(shared_ptr<ServiceMsg> msg) {
    //调用Lua函数
    lua_getglobal(luaState, "OnServiceMsg"); 
    lua_pushinteger(luaState, msg->source); 
    lua_pushlstring(luaState, msg->buff.get(), msg->size); 
    int isok = lua_pcall(luaState, 2, 0, 0);
    if(isok != 0){ //成功返回值为0，否则代表失败.
         cout << "call lua OnServiceMsg fail " << lua_tostring(luaState, -1) << endl;
    }
}

//新连接
void Service::OnAcceptMsg(shared_ptr<SocketAcceptMsg> msg) {
    cout << "OnAcceptMsg " << msg->clientFd << endl;

    //调用Lua函数
    lua_getglobal(luaState, "OnAcceptMsg"); 
    lua_pushinteger(luaState, msg->listenFd); 
    lua_pushinteger(luaState, msg->clientFd); 
    int isok = lua_pcall(luaState, 2, 0, 0);
    if(isok != 0){ //成功返回值为0，否则代表失败.
         cout << "call lua OnAcceptMsg fail " << lua_tostring(luaState, -1) << endl;
    }
}

//套接字可读
void Service::OnRWMsg(shared_ptr<SocketRWMsg> msg) {
    int fd = msg->fd;
    //可读
    if(msg->isRead) {
        const int BUFFSIZE = 512;
        char buff[BUFFSIZE];
        int len = 0;
        do {
            len = read(fd, &buff, BUFFSIZE);
            if(len > 0){
                OnSocketData(fd, buff, len);
            }
        }while(len == BUFFSIZE);

        if(len <= 0 && errno != EAGAIN) {
            if(Starnet::inst->GetConn(fd)) {
                OnSocketClose(fd);
                Starnet::inst->CloseConn(fd);
            }
        }
    }
}



//收到消息时触发
void Service::OnMsg(shared_ptr<BaseMsg> msg) {
    //SERVICE
    if(msg->type == BaseMsg::TYPE::SERVICE) {
        auto m = dynamic_pointer_cast<ServiceMsg>(msg);
        OnServiceMsg(m);
    }
    //SOCKET_ACCEPT
    else if(msg->type == BaseMsg::TYPE::SOCKET_ACCEPT) {
        auto m = dynamic_pointer_cast<SocketAcceptMsg>(msg);
        OnAcceptMsg(m);
    }
    //SOCKET_RW
    else if(msg->type == BaseMsg::TYPE::SOCKET_RW) {
        auto m = dynamic_pointer_cast<SocketRWMsg>(msg);
        OnRWMsg(m);
    }
}


//退出服务时触发
void Service::OnExit() {
    cout << "[" << id <<"] OnExit"  << endl;
    //调用Lua函数
    lua_getglobal(luaState, "OnExit"); 
    int isok = lua_pcall(luaState, 0, 0, 0);
    if(isok != 0){ //成功返回值为0，否则代表失败.
         cout << "call lua OnExit fail " << 
            lua_tostring(luaState, -1) << endl;
    }
    //关闭lua虚拟机
    lua_close(luaState);
}