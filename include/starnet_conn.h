#pragma once
#include <string>
using namespace std; 

class Conn {
public:
    enum TYPE {          //连接类型
        LISTEN = 1, 
        CLIENT = 2,
        UDP = 3,         //UDP socket（无连接，报式收发）
    }; 

    uint8_t type;
    int fd;
    uint32_t serviceId;
    string udpAddr;      //UDP 默认对端地址（二进制打包，对齐 skynet socket 的 p.udp_address）
    bool connecting = false;  //主动连接中（非阻塞 connect 等待 EPOLLOUT 完成，对齐 skynet SOCKET_TYPE_CONNECTING）

};