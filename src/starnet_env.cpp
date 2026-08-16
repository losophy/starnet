#include "starnet_env.h"

#include <unordered_map>
#include <pthread.h>

//环境表（对齐 skynet_env.c：全局 k/v 配置）
//读多写少，用 rwlock；返回 string 拷贝，规避 Lua 指针生命周期问题
using namespace std;

static unordered_map<string, string> g_env;
static pthread_rwlock_t g_lock = PTHREAD_RWLOCK_INITIALIZER;

void starnet_env_init() {
    //静态存储，无需额外分配；保留接口对齐 skynet_env_init
}

string starnet_getenv(const char* key, bool* found) {
    string result;
    pthread_rwlock_rdlock(&g_lock);
    {
        auto it = g_env.find(key);
        if(it != g_env.end()) {
            result = it->second;
            if(found) {
                *found = true;
            }
        }
        else if(found) {
            *found = false;
        }
    }
    pthread_rwlock_unlock(&g_lock);
    return result;
}

void starnet_setenv(const char* key, const char* value) {
    pthread_rwlock_wrlock(&g_lock);
    {
        g_env[key] = value ? value : "";
    }
    pthread_rwlock_unlock(&g_lock);
}