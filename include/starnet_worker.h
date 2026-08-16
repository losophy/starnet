#pragma once
#include <thread> 
#include "starnet_service.h"
class StarnetStart;

using namespace std;

class Worker { 
public:
    StarnetStart* start;   //线程池引用
    int id;             //编号
    int eachNum;        //每次处理多少条消息
    void operator()();  //线程函数
private:
    //辅助函数
    void CheckAndPutGlobal(shared_ptr<Service> srv);
};