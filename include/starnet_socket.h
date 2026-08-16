#pragma once
#include "starnet_socket_server.h"

//Socket桥接层（对齐 skynet_socket.c：把网络事件投递给服务）
class SocketBridge : public SocketServerListener {
public:
    //socket 消息（accept / data / close，按子类型投递）
    virtual void OnSocketMsg(shared_ptr<SocketMsg> msg, uint32_t serviceId);
};