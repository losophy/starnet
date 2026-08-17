#include "starnet_start.h"
#include "starnet_worker.h"
#include "starnet_socket_server.h"
#include "starnet_socket.h"
#include "starnet_timer.h"
#include "starnet_mq.h"
#include "starnet_logger.h"
#include "starnet.h"
#include <iostream>
#include <unistd.h>

using namespace std;

//构造函数
StarnetStart::StarnetStart() {
    pthread_cond_init(&sleepCond, NULL);
    pthread_mutex_init(&sleepMtx, NULL);
}

//设置worker线程数（对齐 skynet config.thread）
void StarnetStart::SetWorkerNum(int num) {
    if(num > 0) {
        workerNum = num;
    }
}

//开启worker线程
void StarnetStart::StartWorker() {
    //weight 调度表（对齐 skynet_start.c 的硬编码 weight[]：前32个线程，超出取 0）
    static int weight[] = {
        -1, -1, -1, -1, 0, 0, 0, 0,
        1, 1, 1, 1, 1, 1, 1, 1,
        2, 2, 2, 2, 2, 2, 2, 2,
        3, 3, 3, 3, 3, 3, 3, 3, };
    for (int i = 0; i < workerNum; i++) {
        starnet_log("start worker thread:%d", i);
        //创建线程对象
        Worker* worker = new Worker();
        worker->start = this;
        worker->id = i;
        if(i < (int)(sizeof(weight)/sizeof(weight[0]))) {
            worker->weight = weight[i];
        }
        else {
            worker->weight = 0;
        }
        //每 worker 一个监视器（对齐 skynet monitor）
        worker->monitor = new StarnetMonitor();
        monitors.push_back(worker->monitor);
        //创建线程
        thread* wt = new thread(*worker);
        //添加到列表
        workers.push_back(worker);
        workerThreads.push_back(wt);
    }
}

//开启Socket线程
void StarnetStart::StartSocket() {
    //创建网络IO引擎
    socketServer = new SocketServer();
    //初始化
    socketServer->Init();
    //接入退出标志（优雅退出：唤醒 epoll + 收尾关闭连接）
    socketServer->SetExitFlag(&exitFlag);
    //创建桥接层并接线
    socketBridge = new SocketBridge();
    socketServer->SetListener(socketBridge);
    //创建线程
    socketThread = new thread(*socketServer);
}

//Timer线程驱动（对齐 skynet_start.c thread_timer：每2.5ms驱动一次）
void StarnetStart::TimerLoop() {
    while(!IsExit()) {
        starnet_updatetime();
        usleep(2500);
    }
}

//开启Timer线程
void StarnetStart::StartTimer() {
    timerThread = new thread(&StarnetStart::TimerLoop, this);
}

//开启监视器线程（对齐 skynet_start.c 的 monitor 线程：每 5 秒检查 worker 卡死）
void StarnetStart::StartMonitor() {
    monitorThread = new thread(starnet_monitor_run, monitors.data(), (int)monitors.size(), 5, &exitFlag);
}

//开启系统线程池
void StarnetStart::Start() {
    //开启Worker
    StartWorker();
    //开启Socket线程
    StartSocket();
    //开启Timer线程（对齐 skynet THREAD_TIMER）
    StartTimer();
    //开启监视器线程（对齐 skynet monitor）
    StartMonitor();
}

//等待运行（优雅全局退出：等待退出请求 → 排空服务 → 置标志唤醒 → join 全部线程）
void StarnetStart::Wait() {
    //等待退出请求（SIGINT/SIGTERM 或 starnet.globalexit）
    while(!Starnet::inst->IsExitRequested()) {
        usleep(50000);
    }
    //1. 全部服务退休：触发 OnExit/lua_close + 残留消息清理回 PTYPE_ERROR
    //   （对齐 skynet_context_dispatchall 的排空语义；在置退出标志前执行，保证 worker 不会提前退出）
    Starnet::inst->KillAllServices();
    //2. 置退出标志 + 广播唤醒全部 worker（worker 处理完队列到空后退出）
    SetExit();
    WakeUpAll();
    //3. 收尾全部线程（对齐 skynet_start.c：join worker + timer + socket + monitor）
    for(auto* t : workerThreads) {
        if(t) {
            t->join();
        }
    }
    if(timerThread) {
        timerThread->join();
    }
    if(socketThread) {
        socketThread->join();
    }
    if(monitorThread) {
        monitorThread->join();
    }
}

//优雅退出标志：worker/timer/monitor 轮询
bool StarnetStart::IsExit() {
    return exitFlag.load(std::memory_order_relaxed);
}

void StarnetStart::SetExit() {
    exitFlag.store(true, std::memory_order_relaxed);
    //唤醒 socket 线程（写 eventfd → epoll_wait 返回 → 收尾关闭连接）
    if(socketServer) {
        socketServer->WakeUp();
    }
}

//唤醒全部休眠 worker（退出时广播，对齐 skynet_start.c 的 cond_broadcast）
void StarnetStart::WakeUpAll() {
    pthread_mutex_lock(&sleepMtx);
    pthread_cond_broadcast(&sleepCond);
    pthread_mutex_unlock(&sleepMtx);
}

//Worker线程调用，进入休眠
void StarnetStart::WorkerWait(){
    pthread_mutex_lock(&sleepMtx);
    sleepCount++;
    pthread_cond_wait(&sleepCond, &sleepMtx);
    sleepCount--;
    pthread_mutex_unlock(&sleepMtx); 
}

//检查并唤醒线程
void StarnetStart::CheckAndWeakUp(){
    //unsafe
    if(sleepCount == 0) {
        return;
    }
    if( workerNum - sleepCount <= starnet_globalmq_length() ) {
        pthread_cond_signal(&sleepCond);
    }
}

//获取Socket线程对象
SocketServer* StarnetStart::GetSocketServer() {
    return socketServer;
}