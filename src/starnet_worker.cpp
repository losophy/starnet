#include <iostream>
#include <unistd.h>
#include "starnet_worker.h"
#include "starnet_start.h"
#include "starnet_service.h"
using namespace std;

//那些调Starnet的通过传参数解决
//状态是不在队列中，global=true
void Worker::CheckAndPutGlobal(shared_ptr<Service> srv) {
    //退出中（跨线程标记，对齐 skynet retire：由 worker 线程在安全点执行退出清理）
    if(srv->isExiting){
        //只执行一次 OnExit（lua_close 归 worker 线程，避免与正在处理的 Lua 调用并发）
        if(!srv->exited.exchange(true)) {
            srv->OnExit();
            srv->mq.Clear();  //丢弃残留消息
        }
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
            //监视器：开始处理前记录（对齐 skynet_monitor_trigger）
            if(monitor) {
                starnet_monitor_trigger(monitor, 0, srv->id, 0);
            }
            srv->ProcessMsgs(eachNum);
            //处理完检查（对齐 skynet_monitor_check）
            if(monitor) {
                starnet_monitor_check(monitor);
            }
            CheckAndPutGlobal(srv);
        }
    }
}