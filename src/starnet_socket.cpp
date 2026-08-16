#include "starnet_socket.h"
#include "starnet.h"

//socket 消息（accept / data / close，对齐 skynet_socket.c 转发）
void SocketBridge::OnSocketMsg(shared_ptr<SocketMsg> msg, uint32_t serviceId) {
    Starnet::inst->Send(serviceId, msg);
}