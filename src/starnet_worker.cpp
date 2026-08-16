#include <iostream>
#include <unistd.h>
#include "starnet_worker.h"
#include "starnet_start.h"
#include "starnet_service.h"
using namespace std;

//那些调Starnet的通过传参数解决
//状态是不在队列中，global=true
void Worker::CheckAndPutGlobal(shared_ptr<Service> srv) {
    //退出中（只能自己调退出，isExiting不会线程冲突）
    if(srv->isExiting){ 
        return; 
    }
    //队列非空则重新入全局队列，否则置inGlobal=false
    srv->mq.FinishDispatch(srv);
}



//线程函数
void Worker::operator()() {
    while(true) {
        shared_ptr<Service> srv = starnet_globalmq_pop();
        if(!srv){
            start->WorkerWait();
        }
        else{
            srv->ProcessMsgs(eachNum);
            CheckAndPutGlobal(srv);
        }
    }
}