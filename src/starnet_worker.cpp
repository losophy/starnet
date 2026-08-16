#include <iostream>
#include <unistd.h>
#include "starnet_worker.h"
#include "starnet_start.h"
#include "starnet_service.h"
#include "starnet.h"
using namespace std;

//丢弃消息时的回调（对齐 skynet_server.c 的 drop_message）：
//服务退出丢弃未处理消息时，给发送方回 PTYPE_ERROR（保留原 session），
//让等待 call 的协程报错退出（Lua 侧 starnet.lua 处理 PTYPE_ERROR）
static void dropMessage(shared_ptr<BaseMsg> msg) {
    //SocketMsg 无发送方语义（网络消息 source 恒为 0），跳过（对齐 skynet 实际行为）
    if(msg->type == BaseMsg::TYPE::SOCKET) {
        return;
    }
    shared_ptr<ServiceMsg> sm = static_pointer_cast<ServiceMsg>(msg);
    //source==0 也跳过，避免 Send(0,..) 打噪音日志（skynet 对 handle 0 同样静默）
    if(sm->source == 0) {
        return;
    }
    auto err = make_shared<ServiceMsg>();
    err->type = BaseMsg::TYPE::ERROR;
    err->source = 0;
    err->session = sm->session;
    Starnet::inst->Send(sm->source, err);
}

//那些调Starnet的通过传参数解决
//状态是不在队列中，global=true
void Worker::CheckAndPutGlobal(shared_ptr<Service> srv) {
    //退出中（跨线程标记，对齐 skynet retire：由 worker 线程在安全点执行退出清理）
    if(srv->isExiting){
        //只执行一次 OnExit（lua_close 归 worker 线程，避免与正在处理的 Lua 调用并发）
        if(!srv->exited.exchange(true)) {
            srv->OnExit();
            srv->mq.Clear(dropMessage);  //丢弃残留消息（回 PTYPE_ERROR 通知发送方）
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
            //weight 调度（对齐 skynet_server.c dispatch：weight<0 每轮 1 条；
            //>=0 每轮处理 队列长度>>weight 条，避免独占、实现加权公平）
            int n = 1;
            if(weight >= 0) {
                n = srv->mq.Length() >> weight;
            }
            srv->ProcessMsgs(n);
            //处理完检查（对齐 skynet_monitor_check）
            if(monitor) {
                starnet_monitor_check(monitor);
            }
            CheckAndPutGlobal(srv);
        }
    }
}