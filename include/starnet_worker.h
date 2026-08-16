#pragma once
#include <thread> 
#include "starnet_service.h"
#include "starnet_monitor.h"
class StarnetStart;

using namespace std;

class Worker { 
public:
    StarnetStart* start;   //线程池引用
    int id;             //编号
    int weight;         //调度权重（对齐 skynet weight：<0 每轮 1 条；>=0 每轮 队列长度>>weight 条）
    StarnetMonitor* monitor;  //卡死监视器（由 StarnetStart 创建并分配）
    void operator()();  //线程函数
private:
    //辅助函数
    void CheckAndPutGlobal(shared_ptr<Service> srv);
};