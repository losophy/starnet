#pragma once
#include <stddef.h>

//内存统计（对齐 skynet mem_info.c：进程 RSS 报告）
//实现：读 /proc/self/status 的 VmRSS（KB），返回进程常驻内存
//决策：不做全局 operator new 重载（malloc_hook 宏替换在 C++ 不可行——
//统计不到 new/delete 与 std 容器；C++ 等价物为全局 new 重载，侵入整个进程
//所有分配、须处理异常/对齐语义、且同样无泄漏定位能力）；泄漏排查用外部
//工具（valgrind/heaptrack）

//返回当前进程常驻内存（KB，对齐 skynet.mem 的单位）；读取失败返回 0
size_t starnet_memory_used();