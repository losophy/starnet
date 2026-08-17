#pragma once
#include <stdint.h>
#include <atomic>

//监视器（对齐 skynet_monitor.c）：检测 worker 线程卡死（服务死循环不响应）
//每个 worker 一个 StarnetMonitor：
//  worker 处理消息前 trigger（记录 source/dest/session，version++）
//  worker 处理完一批消息后 check（version 推进则同步，否则清空记录）
//独立 monitor 线程每 intervalSec 秒检查所有 monitor：
//  若 version != checkVersion 且 destination 非 0 → 该 worker 卡在一条消息上，告警

struct StarnetMonitor {
    std::atomic<uint32_t> version{0};        //trigger 次数（worker 写，monitor 读）
    std::atomic<uint32_t> checkVersion{0};   //已同步的版本（worker/monitor 维护）
    std::atomic<uint32_t> source{0};         //当前处理消息的来源服务
    std::atomic<uint32_t> destination{0};    //当前处理的服务
    std::atomic<uint32_t> session{0};        //当前处理消息的 session
};

//worker 开始处理一条消息前调用（对齐 skynet_monitor_trigger）
void starnet_monitor_trigger(StarnetMonitor* sm, uint32_t source, uint32_t destination, uint32_t session);
//worker 处理完一批消息后调用（对齐 skynet_monitor_check）
void starnet_monitor_check(StarnetMonitor* sm);
//monitor 线程：每 intervalSec 秒检查 n 个 monitor，卡死时打 starnet_error 告警
//exitFlag：优雅退出标志（置位则退出循环，NULL 表示不检查）
void starnet_monitor_run(StarnetMonitor** sm, int n, int intervalSec, const std::atomic<bool>* exitFlag);