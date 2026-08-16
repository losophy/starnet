#include "starnet_start.h"
#include "starnet_worker.h"
#include "starnet_socket_server.h"
#include "starnet_socket.h"
#include "starnet_timer.h"
#include "starnet_mq.h"
#include "starnet_logger.h"
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
    for (int i = 0; i < workerNum; i++) {
        starnet_log("start worker thread:%d", i);
        //创建线程对象
        Worker* worker = new Worker();
        worker->start = this;
        worker->id = i;
        worker->eachNum = 2 << i;
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
    //创建桥接层并接线
    socketBridge = new SocketBridge();
    socketServer->SetListener(socketBridge);
    //创建线程
    socketThread = new thread(*socketServer);
}

//Timer线程驱动（对齐 skynet_start.c thread_timer：每2.5ms驱动一次）
static void timerLoop() {
    while(true) {
        starnet_updatetime();
        usleep(2500);
    }
}

//开启Timer线程
void StarnetStart::StartTimer() {
    timerThread = new thread(timerLoop);
}

//开启监视器线程（对齐 skynet_start.c 的 monitor 线程：每 5 秒检查 worker 卡死）
void StarnetStart::StartMonitor() {
    monitorThread = new thread(starnet_monitor_run, monitors.data(), (int)monitors.size(), 5);
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

//等待
void StarnetStart::Wait() {
    if( workerThreads[0]) {
        workerThreads[0]->join();
    }
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