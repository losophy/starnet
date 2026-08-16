#pragma once
#include <queue>
#include <memory>
#include <pthread.h>
#include "starnet_msg.h"

using namespace std;

//队列 overload 阈值（对齐 skynet_mq.c 的 MQ_OVERLOAD）
constexpr int MQ_OVERLOAD = 1024;

class Service;

//每服务一个的二级消息队列（对齐 skynet_mq.c 的 message_queue）
class StarnetMQ {
public:
    StarnetMQ();
    ~StarnetMQ();
    //插入消息
    void Push(shared_ptr<BaseMsg> msg);
    //取出一条消息
    shared_ptr<BaseMsg> Pop();
    //队列是否为空
    bool Empty();
    //设置/查询是否在全局队列
    void SetInGlobal(bool isIn);
    bool IsInGlobal();
    //入全局队列并标记，返回是否首次进入（需要唤醒）
    bool TryEnterGlobal(shared_ptr<Service> srv);
    //Worker处理完一批消息后调用：队列非空则重新入全局队列，否则置inGlobal=false
    void FinishDispatch(shared_ptr<Service> srv);
    //清空队列（丢弃残留消息，对齐 skynet_mq 的 message_drop）
    void Clear();
    //读取并清零 overload 告警值（对齐 skynet_mq_overload：非 0 表示队列曾超载）
    int Overload();
    //当前队列消息数（对齐 skynet_mq_length，供 weight 调度）
    int Length();
private:
    //消息列表
    queue<shared_ptr<BaseMsg>> msgQueue;
    pthread_spinlock_t queueLock;
    //标记是否在全局队列  true:在队列中，或正在处理
    bool inGlobal = false;
    pthread_spinlock_t inGlobalLock;
    //overload 告警：pop 时剩余长度超阈值则记录，阈值指数倍增（对齐 skynet_mq.c）
    int overload = 0;
    int overloadThreshold = MQ_OVERLOAD;
};

//全局队列（对齐 skynet_globalmq_*）
void starnet_globalmq_init();
void starnet_globalmq_push(shared_ptr<Service> srv);
shared_ptr<Service> starnet_globalmq_pop();
int starnet_globalmq_length();