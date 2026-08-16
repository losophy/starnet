#pragma once
#include <memory>
#include <string>
#include <stdint.h>
using namespace std; 

//消息基类
class BaseMsg {
public:
    enum TYPE {          //消息类型（对齐 skynet.h 的 PTYPE_*）
        TEXT = 0,        // PTYPE_TEXT
        RESPONSE = 1,    // PTYPE_RESPONSE（RPC响应/定时器到期）
        MULTICAST = 2,   // PTYPE_MULTICAST
        CLIENT = 3,      // PTYPE_CLIENT
        SYSTEM = 4,      // PTYPE_SYSTEM
        HARBOR = 5,      // PTYPE_HARBOR
        SOCKET = 6,      // PTYPE_SOCKET（socket 消息，子类型见 SocketMsg::SUBTYPE）
        ERROR = 7,       // PTYPE_ERROR
        RESERVED_QUEUE = 8,  // PTYPE_RESERVED_QUEUE
        RESERVED_DEBUG = 9,  // PTYPE_RESERVED_DEBUG
        LUA = 10,        // PTYPE_LUA（服务间 lua 消息）
        RESERVED_SNAX = 11,  // PTYPE_RESERVED_SNAX
    }; 
    uint8_t type;           //消息类型
    virtual ~BaseMsg(){};
};

//服务间消息（含RPC：session>0 表示请求/响应匹配，对齐 skynet_message.session）
class ServiceMsg : public BaseMsg  {
public: 
    uint32_t source;        //消息发送方
    int session;            //会话号（0 表示无需响应）
    string buff;            //消息内容（std::string 自管内存，size 取 buff.size()）
};

//socket 消息（type=SOCKET，子类型对齐 socket_server.h 的 SKYNET_SOCKET_TYPE_*）
class SocketMsg : public BaseMsg {
public:
    enum SUBTYPE {          //socket 子类型（对齐 skynet socket_server.h）
        DATA = 1,           // SKYNET_SOCKET_TYPE_DATA（原始字节流，Lua 侧 netpack 解析）
        CONNECT = 2,        // SKYNET_SOCKET_TYPE_CONNECT（主动连接成功）
        CLOSE = 3,          // SKYNET_SOCKET_TYPE_CLOSE
        ACCEPT = 4,         // SKYNET_SOCKET_TYPE_ACCEPT
        ERROR = 5,          // SKYNET_SOCKET_TYPE_ERROR（连接失败等错误，buff 为错误描述）
        UDP = 6,            // SKYNET_SOCKET_TYPE_UDP（UDP 数据报，报式无粘包）
    };
    int subtype;            //socket 子类型（SUBTYPE）
    int fd;                 //连接 fd
    int listenFd;           //ACCEPT 时监听的 fd
    string buff;            //DATA/UDP 数据（socket 线程已读出，worker 直接用）
    string udpAddr;         //UDP 对端地址（二进制打包：1 字节 family + 2 字节端口 + 4/16 字节 IP，对齐 skynet gen_udp_address）
};