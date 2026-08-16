#include "starnet_mem.h"

#include <stdio.h>
#include <string.h>

//读 /proc/self/status 的 VmRSS（KB），进程常驻内存（对齐 skynet mem_info.c）
size_t starnet_memory_used() {
    FILE* f = fopen("/proc/self/status", "r");
    if(f == NULL) {
        return 0;
    }
    size_t rss = 0;
    char line[256];
    while(fgets(line, sizeof(line), f)) {
        if(strncmp(line, "VmRSS:", 6) == 0) {
            sscanf(line + 6, "%zu", &rss);
            break;
        }
    }
    fclose(f);
    return rss;
}