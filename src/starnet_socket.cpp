#include "starnet_socket.h"
#include "starnet.h"

//新连接
void SocketBridge::OnAcceptMsg(shared_ptr<SocketAcceptMsg> msg, uint32_t serviceId) {
    Starnet::inst->Send(serviceId, msg);
}

//可读可写
void SocketBridge::OnRWMsg(shared_ptr<SocketRWMsg> msg, uint32_t serviceId) {
    Starnet::inst->Send(serviceId, msg);
}