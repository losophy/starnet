#pragma once
#include "starnet_socket_server.h"

//Socket桥接层（对齐 skynet_socket.c：把网络事件投递给服务）
class SocketBridge : public SocketServerListener {
public:
    //新连接
    virtual void OnAcceptMsg(shared_ptr<SocketAcceptMsg> msg, uint32_t serviceId);
    //可读可写
    virtual void OnRWMsg(shared_ptr<SocketRWMsg> msg, uint32_t serviceId);
};