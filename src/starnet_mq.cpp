#include "starnet_mq.h"
#include "starnet_service.h"

// ---------- StarnetMQ（二级队列） ----------

//构造函数
StarnetMQ::StarnetMQ() {
    pthread_spin_init(&queueLock, PTHREAD_PROCESS_PRIVATE);
    pthread_spin_init(&inGlobalLock, PTHREAD_PROCESS_PRIVATE);
}

//析构函数
StarnetMQ::~StarnetMQ(){
    pthread_spin_destroy(&queueLock);
    pthread_spin_destroy(&inGlobalLock);
}

//插入消息
void StarnetMQ::Push(shared_ptr<BaseMsg> msg) {
    pthread_spin_lock(&queueLock);
    {
        msgQueue.push(msg);
    }
    pthread_spin_unlock(&queueLock);
}

//取出消息
shared_ptr<BaseMsg> StarnetMQ::Pop() {
    shared_ptr<BaseMsg> msg = NULL;
    pthread_spin_lock(&queueLock);
    {
        if (!msgQueue.empty()) { 
            msg =  msgQueue.front();
            msgQueue.pop();
        }
    }
    pthread_spin_unlock(&queueLock);
    return msg;
}

//队列是否为空
bool StarnetMQ::Empty() {
    bool empty;
    pthread_spin_lock(&queueLock);
    {
        empty = msgQueue.empty();
    }
    pthread_spin_unlock(&queueLock);
    return empty;
}

void StarnetMQ::SetInGlobal(bool isIn) {
    pthread_spin_lock(&inGlobalLock);
    {
        inGlobal = isIn;
    }
    pthread_spin_unlock(&inGlobalLock);
}

bool StarnetMQ::IsInGlobal() {
    bool isIn;
    pthread_spin_lock(&inGlobalLock);
    {
        isIn = inGlobal;
    }
    pthread_spin_unlock(&inGlobalLock);
    return isIn;
}

//入全局队列并标记，返回是否首次进入（需要唤醒）
bool StarnetMQ::TryEnterGlobal(shared_ptr<Service> srv) {
    bool hasPush = false;
    pthread_spin_lock(&inGlobalLock);
    {
        if(!inGlobal) {
            starnet_globalmq_push(srv);
            inGlobal = true;
            hasPush = true;
        }
    }
    pthread_spin_unlock(&inGlobalLock);
    return hasPush;
}

//Worker处理完一批消息后调用
void StarnetMQ::FinishDispatch(shared_ptr<Service> srv) {
    pthread_spin_lock(&queueLock);
    {
        //重新放回全局队列
        if(!msgQueue.empty()) {
            //此时inGlobal一定是true
            starnet_globalmq_push(srv);
        }
        //不在队列中，重设inGlobal
        else {
            SetInGlobal(false);
        }
    }
    pthread_spin_unlock(&queueLock);
}

// ---------- 全局队列（对齐 skynet_globalmq_*） ----------

static queue<shared_ptr<Service>> globalQueue;
static pthread_spinlock_t globalLock;
static int globalLen = 0;
static bool inited = false;

void starnet_globalmq_init() {
    if(!inited) {
        pthread_spin_init(&globalLock, PTHREAD_PROCESS_PRIVATE);
        inited = true;
    }
}

void starnet_globalmq_push(shared_ptr<Service> srv) {
    pthread_spin_lock(&globalLock);
    {
        globalQueue.push(srv);
        globalLen++;
    }
    pthread_spin_unlock(&globalLock);
}

shared_ptr<Service> starnet_globalmq_pop() {
    shared_ptr<Service> srv = NULL;
    pthread_spin_lock(&globalLock);
    {
        if (!globalQueue.empty()) {
            srv = globalQueue.front();
            globalQueue.pop();
            globalLen--;
        }
    }
    pthread_spin_unlock(&globalLock);
    return srv;
}

int starnet_globalmq_length() {
    int len;
    pthread_spin_lock(&globalLock);
    {
        len = globalLen;
    }
    pthread_spin_unlock(&globalLock);
    return len;
}