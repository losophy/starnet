#pragma once
#include <stdint.h>

//服务句柄 / 名字服务（对齐 skynet_handle.c）
//保留高 8 位用于远程 id（harbor），单节点 harbor=0
#define HANDLE_MASK 0xffffff
#define HANDLE_REMOTE_SHIFT 24

//注册本地名（name 不带 '.' 前缀），重名返回 false
bool starnet_handle_namehandle(uint32_t handle, const char* name);
//按名字查 handle（0=未找到）
uint32_t starnet_handle_findname(const char* name);
//清除某 handle 的所有名字（服务退休时调用）
void starnet_handle_removename(uint32_t handle);
//初始化
void starnet_handle_init();