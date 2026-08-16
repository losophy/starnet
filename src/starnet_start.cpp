#include "starnet_start.h"
#include "starnet_worker.h"
#include "starnet_socket_server.h"
#include "starnet_socket.h"
#include "starnet_mq.h"
#include <iostream>

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
        cout << "start worker thread:" << i << endl;
        //创建线程对象
        Worker* worker = new Worker();
        worker->start = this;
        worker->id = i;
        worker->eachNum = 2 << i;
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

//开启系统线程池
void StarnetStart::Start() {
    //开启Worker
    StartWorker();
    //开启Socket线程
    StartSocket();
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
        cout << "weakup" << endl; 
        pthread_cond_signal(&sleepCond);
    }
}

//获取Socket线程对象
SocketServer* StarnetStart::GetSocketServer() {
    return socketServer;
}