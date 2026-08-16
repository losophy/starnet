#include <iostream>
#include <unistd.h>
#include "starnet_worker.h"
#include "starnet_service.h"
using namespace std;

//那些调Starnet的通过传参数解决
//状态是不在队列中，global=true
void Worker::CheckAndPutGlobal(shared_ptr<Service> srv) {
    //退出中（只能自己调退出，isExiting不会线程冲突）
    if(srv->isExiting){ 
        return; 
    }

    pthread_spin_lock(&srv->queueLock);
    {
        //重新放回全局队列
        if(!srv->msgQueue.empty()) {
            //此时srv->inGlobal一定是true
            Starnet::inst->PushGlobalQueue(srv);
        }
        //不在队列中，重设inGlobal
        else {
            srv->SetInGlobal(false);
        }
    }
    pthread_spin_unlock(&srv->queueLock);
}



//线程函数
void Worker::operator()() {
    while(true) {
        shared_ptr<Service> srv = Starnet::inst->PopGlobalQueue();
        if(!srv){
            Starnet::inst->WorkerWait();
        }
        else{
            srv->ProcessMsgs(eachNum);
            CheckAndPutGlobal(srv);
        }
    }
}