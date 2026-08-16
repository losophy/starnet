#pragma once
#include <thread>
#include <vector>
#include <pthread.h>

using namespace std;

class Worker;
class SocketServer;
class SocketBridge;

//系统线程池管理（对齐 skynet_start.c：thread_worker/thread_socket + monitor 休眠唤醒）
class StarnetStart {
public:
    //构造函数：初始化休眠唤醒锁
    StarnetStart();
    //设置worker线程数（对齐 skynet config.thread）
    void SetWorkerNum(int num);
    //开启系统线程池
    void Start();
    //等待运行
    void Wait();
    //让工作线程等待（仅工作线程调用）
    void WorkerWait();
    //检查并唤醒线程
    void CheckAndWeakUp();
    //获取Socket线程对象
    SocketServer* GetSocketServer();
private:
    //工作线程
    int workerNum = 3;           //worker线程数（对齐 skynet config.thread）
    vector<Worker*> workers;     //worker对象
    vector<thread*> workerThreads;   //线程
    //Socket线程
    SocketServer* socketServer;
    SocketBridge* socketBridge;
    thread* socketThread;
    //Timer线程（对齐 skynet THREAD_TIMER）
    thread* timerThread;
    //休眠和唤醒
    pthread_mutex_t sleepMtx;
    pthread_cond_t sleepCond;
    int sleepCount = 0;        //休眠工作线程数
private:
    //开启工作线程
    void StartWorker();
    //开启Socket线程
    void StartSocket();
    //开启Timer线程
    void StartTimer();
};