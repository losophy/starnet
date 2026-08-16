#pragma once
#include <stdint.h>

//定时器系统（对齐 skynet_timer.c 的 4 级时间轮）

//初始化定时器
void starnet_timer_init();

//注册定时器：time 为 centisecond(1/100秒)，到期后向 handle 服务投递 TimerMsg(session)
//time<=0 时立即投递；返回 session
int starnet_timeout(uint32_t handle, int time, int session);

//驱动定时器（timer线程每2.5ms调用，对齐 skynet thread_timer）
void starnet_updatetime();

//当前tick（centisecond）
uint64_t starnet_now();