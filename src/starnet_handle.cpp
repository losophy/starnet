#include "starnet_handle.h"
#include <unordered_map>
#include <string>
#include <string.h>
#include <pthread.h>

//名字服务（对齐 skynet_handle.c 的 namehandle/findname）
//名字不带 '.' 前缀，'.' 前缀由 Lua 层处理（对齐 skynet cmd_name/cmd_query）

static std::unordered_map<std::string, uint32_t> nameMap;
static pthread_rwlock_t nameLock;
static bool inited = false;

static void
name_wlock() {
    pthread_rwlock_wrlock(&nameLock);
}

static void
name_wunlock() {
    pthread_rwlock_unlock(&nameLock);
}

static void
name_rlock() {
    pthread_rwlock_rdlock(&nameLock);
}

static void
name_runlock() {
    pthread_rwlock_unlock(&nameLock);
}

//注册本地名，重名返回 false（对齐 skynet_handle_namehandle）
bool
starnet_handle_namehandle(uint32_t handle, const char* name) {
    name_wlock();
    {
        std::unordered_map<std::string, uint32_t>::iterator iter = nameMap.find(name);
        if(iter != nameMap.end()) {
            name_wunlock();
            return false;
        }
        nameMap.emplace(name, handle);
    }
    name_wunlock();
    return true;
}

//按名字查 handle（0=未找到，对齐 skynet_handle_findname）
uint32_t
starnet_handle_findname(const char* name) {
    uint32_t handle = 0;
    name_rlock();
    {
        std::unordered_map<std::string, uint32_t>::iterator iter = nameMap.find(name);
        if(iter != nameMap.end()) {
            handle = iter->second;
        }
    }
    name_runlock();
    return handle;
}

//清除某 handle 的所有名字（服务退休时调用，对齐 skynet_handle_retire 内名字清理）
void
starnet_handle_removename(uint32_t handle) {
    name_wlock();
    {
        std::unordered_map<std::string, uint32_t>::iterator iter = nameMap.begin();
        while(iter != nameMap.end()) {
            if(iter->second == handle) {
                iter = nameMap.erase(iter);
            } else {
                ++iter;
            }
        }
    }
    name_wunlock();
}

void
starnet_handle_init() {
    if(!inited) {
        pthread_rwlock_init(&nameLock, NULL);
        inited = true;
    }
}