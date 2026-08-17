#pragma once
#include <memory>
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
    bool isBind = false;      //绑定已有 fd（外部所有，引擎只管事件不负责 close，对齐 skynet SOCKET_TYPE_BIND）
    bool paused = false;      //读暂停（pause/start 流控：暂停时去掉 EPOLLIN，写缓冲照常刷；对齐 skynet enable_read）
    //动态读缓冲（TCP 用，仅 socket 线程访问；对齐 skynet socket 的 p.size：读满翻倍、读不满减半）
    size_t readSize = 8192;   //当前逻辑缓冲大小（READ_BUFFER_MIN=8192 起步，读满 ×2，上限 READ_BUFFER_MAX=1MB）
    size_t readBuffCap = 0;   //已分配容量（< readSize 才重新分配；缩小复用大缓冲，不缩分配）
    unique_ptr<char[]> readBuff;  //读缓冲（复用，避免每事件重新 malloc）

};