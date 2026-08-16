#pragma once
#include <memory>
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
    char load[999999]{};    //用于检测内存泄漏
    virtual ~BaseMsg(){};
};

//服务间消息（含RPC：session>0 表示请求/响应匹配，对齐 skynet_message.session）
class ServiceMsg : public BaseMsg  {
public: 
    uint32_t source;        //消息发送方
    int session;            //会话号（0 表示无需响应）
    shared_ptr<char> buff;  //消息内容
    size_t size;            //消息内容大小
};

//socket 消息（type=SOCKET，子类型对齐 socket_server.h 的 SKYNET_SOCKET_TYPE_*）
class SocketMsg : public BaseMsg {
public:
    enum SUBTYPE {          //socket 子类型（对齐 skynet socket_server.h）
        DATA = 1,           // SKYNET_SOCKET_TYPE_DATA（原始字节流，Lua 侧 netpack 解析）
        CLOSE = 3,          // SKYNET_SOCKET_TYPE_CLOSE
        ACCEPT = 4,         // SKYNET_SOCKET_TYPE_ACCEPT
    };
    int subtype;            //socket 子类型（SUBTYPE）
    int fd;                 //连接 fd
    int listenFd;           //ACCEPT 时监听的 fd
};