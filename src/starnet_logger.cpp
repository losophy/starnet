#include "starnet_logger.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <sys/time.h>

//日志器（对齐 skynet_log.c 的 log_service：时间戳 + 落盘/stderr）
//全局单例，互斥锁保证多线程写安全（worker 线程 / timer 线程 / 主线程共用）

static FILE* g_file = NULL;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

bool starnet_logger_init(const char* filename) {
    pthread_mutex_lock(&g_lock);
    if(filename == NULL || filename[0] == '\0') {
        g_file = NULL;  //写 stderr
    }
    else {
        FILE* f = fopen(filename, "a");
        if(f == NULL) {
            pthread_mutex_unlock(&g_lock);
            return false;
        }
        g_file = f;
    }
    pthread_mutex_unlock(&g_lock);
    return true;
}

void starnet_logger_close() {
    pthread_mutex_lock(&g_lock);
    if(g_file) {
        fclose(g_file);
        g_file = NULL;
    }
    pthread_mutex_unlock(&g_lock);
}

//写一条带时间戳的日志（对齐 skynet_log 的 HH:MM:SS.mmm 格式）
static void write_log(const char* level, const char* msg) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    struct tm t;
    localtime_r(&tv.tv_sec, &t);
    char stamp[64];
    snprintf(stamp, sizeof(stamp), "%02d:%02d:%02d.%03d",
        t.tm_hour, t.tm_min, t.tm_sec, (int)(tv.tv_usec / 1000));

    pthread_mutex_lock(&g_lock);
    {
        FILE* out = g_file ? g_file : stderr;
        fprintf(out, "%s [%s] %s\n", stamp, level, msg);
        fflush(out);
    }
    pthread_mutex_unlock(&g_lock);
}

static void vlog(const char* level, const char* fmt, va_list ap) {
    char buf[4096];
    vsnprintf(buf, sizeof(buf), fmt, ap);
    write_log(level, buf);
}

void starnet_log(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vlog("info", fmt, ap);
    va_end(ap);
}

void starnet_error(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vlog("error", fmt, ap);
    va_end(ap);
}