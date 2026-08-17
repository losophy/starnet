#pragma once
#include <stdint.h>

//定时器系统（对齐 skynet_timer.c 的 4 级时间轮）

//初始化定时器
void starnet_timer_init();

//注册定时器：time 为 centisecond(1/100秒)，到期后向 handle 服务投递 RESPONSE 消息(session)
//time<=0 时立即投递；返回 session
int starnet_timeout(uint32_t handle, int time, int session);

//驱动定时器（timer线程每2.5ms调用，对齐 skynet thread_timer）
void starnet_updatetime();

//当前tick（centisecond）
uint64_t starnet_now();

//性能统计（对齐 skynet_timer.c skynet_thread_time / skynet_profile_enable）：
//当前线程 CPU 时间，微秒（CLOCK_THREAD_CPUTIME_ID）
uint64_t starnet_thread_time();

//全局 profile 开关（默认开，对齐 skynet optboolean("profile",1)）；服务构造时复制到自身 profile
void starnet_profile_enable(int enable);
bool starnet_profile_enabled();