#include "starnet_timer.h"
#include "starnet.h"
#include "starnet_logger.h"
#include <iostream>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <iostream>
#include <time.h>
#include <atomic>

#define TIME_NEAR_SHIFT 8
#define TIME_NEAR (1 << TIME_NEAR_SHIFT)
#define TIME_LEVEL_SHIFT 6
#define TIME_LEVEL (1 << TIME_LEVEL_SHIFT)
#define TIME_NEAR_MASK (TIME_NEAR-1)
#define TIME_LEVEL_MASK (TIME_LEVEL-1)

//定时器事件（跟随 timer_node 存储在节点之后）
struct timer_event {
    uint32_t handle;
    int session;
};

struct timer_node {
    struct timer_node* next;
    uint32_t expire;
};

struct link_list {
    struct timer_node head;
    struct timer_node* tail;
};

struct timer {
    struct link_list near[TIME_NEAR];
    struct link_list t[4][TIME_LEVEL];
    pthread_spinlock_t lock;
    uint32_t time;
    uint32_t starttime;
    uint64_t current;
    uint64_t current_point;
};

static struct timer* TI = NULL;

static inline struct timer_node*
link_clear(struct link_list* list) {
    struct timer_node* ret = list->head.next;
    list->head.next = 0;
    list->tail = &(list->head);
    return ret;
}

static inline void
link(struct link_list* list, struct timer_node* node) {
    list->tail->next = node;
    list->tail = node;
    node->next = 0;
}

static void
add_node(struct timer* T, struct timer_node* node) {
    uint32_t time = node->expire;
    uint32_t current_time = T->time;

    if ((time|TIME_NEAR_MASK)==(current_time|TIME_NEAR_MASK)) {
        link(&T->near[time&TIME_NEAR_MASK], node);
    } else {
        int i;
        uint32_t mask = TIME_NEAR << TIME_LEVEL_SHIFT;
        for (i=0;i<3;i++) {
            if ((time|(mask-1))==(current_time|(mask-1))) {
                break;
            }
            mask <<= TIME_LEVEL_SHIFT;
        }
        link(&T->t[i][((time>>(TIME_NEAR_SHIFT + i*TIME_LEVEL_SHIFT)) & TIME_LEVEL_MASK)], node);
    }
}

static void
timer_add(struct timer* T, void* arg, size_t sz, int time) {
    struct timer_node* node = (struct timer_node*)new char[sizeof(*node)+sz];
    memcpy(node+1, arg, sz);

    pthread_spin_lock(&T->lock);
    {
        node->expire = time + T->time;
        add_node(T, node);
    }
    pthread_spin_unlock(&T->lock);
}

static void
move_list(struct timer* T, int level, int idx) {
    struct timer_node* current = link_clear(&T->t[level][idx]);
    while (current) {
        struct timer_node* temp = current->next;
        add_node(T, current);
        current = temp;
    }
}

static void
timer_shift(struct timer* T) {
    int mask = TIME_NEAR;
    uint32_t ct = ++T->time;
    if (ct == 0) {
        move_list(T, 3, 0);
    } else {
        uint32_t time = ct >> TIME_NEAR_SHIFT;
        int i = 0;
        while ((ct & (mask-1))==0) {
            int idx = time & TIME_LEVEL_MASK;
            if (idx != 0) {
                move_list(T, i, idx);
                break;
            }
            mask <<= TIME_LEVEL_SHIFT;
            time >>= TIME_LEVEL_SHIFT;
            ++i;
        }
    }
}

static inline void
dispatch_list(struct timer_node* current) {
    do {
        struct timer_event* event = (struct timer_event*)(current+1);
        //构造定时器消息投递给目标服务（对齐 skynet_timer 投递 PTYPE_RESPONSE）
        auto msg = make_shared<ServiceMsg>();
        msg->type = BaseMsg::TYPE::RESPONSE;
        msg->source = 0;
        msg->session = event->session;
        Starnet::inst->Send(event->handle, msg);

        struct timer_node* temp = current;
        current = current->next;
        delete[] (char*)temp;
    } while (current);
}

static inline void
timer_execute(struct timer* T) {
    int idx = T->time & TIME_NEAR_MASK;

    while (T->near[idx].head.next) {
        struct timer_node* current = link_clear(&T->near[idx]);
        pthread_spin_unlock(&T->lock);
        // dispatch_list 不需要锁
        dispatch_list(current);
        pthread_spin_lock(&T->lock);
    }
}

static void
timer_update(struct timer* T) {
    pthread_spin_lock(&T->lock);
    {
        // try to dispatch timeout 0 (rare condition)
        timer_execute(T);
        // shift time first, and then dispatch timer message
        timer_shift(T);
        timer_execute(T);
    }
    pthread_spin_unlock(&T->lock);
}

static struct timer*
timer_create_timer() {
    struct timer* r = (struct timer*)new timer;
    memset(r, 0, sizeof(*r));

    int i, j;
    for (i=0;i<TIME_NEAR;i++) {
        link_clear(&r->near[i]);
    }
    for (i=0;i<4;i++) {
        for (j=0;j<TIME_LEVEL;j++) {
            link_clear(&r->t[i][j]);
        }
    }
    pthread_spin_init(&r->lock, 0);
    r->current = 0;
    return r;
}

int
starnet_timeout(uint32_t handle, int time, int session) {
    if (time <= 0) {
        //立即投递（对齐 skynet_timer 的 PTYPE_RESPONSE）
        auto msg = make_shared<ServiceMsg>();
        msg->type = BaseMsg::TYPE::RESPONSE;
        msg->source = 0;
        msg->session = session;
        Starnet::inst->Send(handle, msg);
    } else {
        struct timer_event event;
        event.handle = handle;
        event.session = session;
        timer_add(TI, &event, sizeof(event), time);
    }
    return session;
}

// centisecond: 1/100 second
static void
systime(uint32_t* sec, uint32_t* cs) {
    struct timespec ti;
    clock_gettime(CLOCK_REALTIME, &ti);
    *sec = (uint32_t)ti.tv_sec;
    *cs = (uint32_t)(ti.tv_nsec / 10000000);
}

static uint64_t
gettime() {
    uint64_t t;
    struct timespec ti;
    clock_gettime(CLOCK_MONOTONIC, &ti);
    t = (uint64_t)ti.tv_sec * 100;
    t += ti.tv_nsec / 10000000;
    return t;
}

void
starnet_updatetime(void) {
    uint64_t cp = gettime();
    if(cp < TI->current_point) {
        starnet_error("time diff error, cp=%llu current_point=%llu", cp, TI->current_point);
        TI->current_point = cp;
    } else if (cp != TI->current_point) {
        uint32_t diff = (uint32_t)(cp - TI->current_point);
        TI->current_point = cp;
        TI->current += diff;
        int i;
        for (i=0;i<diff;i++) {
            timer_update(TI);
        }
    }
}

uint64_t
starnet_now(void) {
    return TI->current;
}

void
starnet_timer_init(void) {
    TI = timer_create_timer();
    uint32_t current = 0;
    systime(&TI->starttime, &current);
    TI->current = current;
    TI->current_point = gettime();
}

//性能统计（对齐 skynet_timer.c 的 skynet_thread_time / skynet_profile_enable）

#define TIMER_MICROSEC 1000000

//当前线程 CPU 时间（微秒，CLOCK_THREAD_CPUTIME_ID）
uint64_t
starnet_thread_time(void) {
    struct timespec ti;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ti);
    return (uint64_t)ti.tv_sec * TIMER_MICROSEC + (uint64_t)ti.tv_nsec / 1000;
}

//全局 profile 开关（默认开，对齐 skynet optboolean("profile",1)）
static std::atomic<bool> g_profile_enable{true};

void
starnet_profile_enable(int enable) {
    g_profile_enable.store(enable != 0, std::memory_order_relaxed);
}

bool
starnet_profile_enabled(void) {
    return g_profile_enable.load(std::memory_order_relaxed);
}