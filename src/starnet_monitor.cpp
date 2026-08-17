#include "starnet_monitor.h"
#include "starnet_logger.h"
#include <unistd.h>

//监视器（对齐 skynet_monitor.c 的 trigger/check/monitor 线程）

//worker 开始处理消息前调用
void starnet_monitor_trigger(StarnetMonitor* sm, uint32_t source, uint32_t destination, uint32_t session) {
    sm->source = source;
    sm->destination = destination;
    sm->session = session;
    sm->version.fetch_add(1, std::memory_order_relaxed);  //对齐 __sync_fetch_and_add
}

//worker 处理完一批消息后调用
void starnet_monitor_check(StarnetMonitor* sm) {
    uint32_t v = sm->version.load(std::memory_order_relaxed);
    if(v != sm->checkVersion.load(std::memory_order_relaxed)) {
        sm->checkVersion = v;
    }
    else {
        //本批未处理新消息：清空记录（表示空闲）
        sm->source = 0;
        sm->destination = 0;
        sm->session = 0;
    }
}

//monitor 线程：每 intervalSec 秒检查一次（退出标志置位则退出）
void starnet_monitor_run(StarnetMonitor** sm, int n, int intervalSec, const std::atomic<bool>* exitFlag) {
    while(true) {
        if(exitFlag && exitFlag->load(std::memory_order_relaxed)) {
            break;
        }
        for(int i = 0; i < n; i++) {
            StarnetMonitor* p = sm[i];
            uint32_t v = p->version.load(std::memory_order_relaxed);
            if(v != p->checkVersion.load(std::memory_order_relaxed)) {
                //有消息触发过但未完成 check：疑似卡死
                if(p->destination.load(std::memory_order_relaxed)) {
                    starnet_error("A message from [:%08x] to [:%08x] maybe stuck (session:%u)",
                        p->source.load(std::memory_order_relaxed),
                        p->destination.load(std::memory_order_relaxed),
                        p->session.load(std::memory_order_relaxed));
                }
                p->checkVersion = v;
            }
        }
        sleep(intervalSec);
    }
}