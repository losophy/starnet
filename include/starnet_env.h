#pragma once
#include <string>

//环境配置（对齐 skynet_env.c 的 getenv/setenv 语义）
//实现用 C++ unordered_map + rwlock（skynet 用专用 Lua 表；starnet 为 C++ 单体、
//env 读多写少，故用 STL——语义等价，且规避 skynet_getenv 返回指针跨调用失效的坑）
//config 的全部顶层键在启动时导入（见 StarnetConfig::Load / Starnet::Start）

//初始化（当前为静态存储，提供接口对齐 skynet_env_init）
void starnet_env_init();
//查询：返回 key 的值（拷贝）；found 非空时收到是否存在
std::string starnet_getenv(const char* key, bool* found = NULL);
//设置/覆盖
void starnet_setenv(const char* key, const char* value);