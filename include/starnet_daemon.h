#pragma once

//守护进程化（对齐 skynet_daemon.c）：
//  starnet_daemon_init(pidfile)：检查重复运行 → daemon 化（fork+setsid）→ flock 写 pidfile → stdio 重定向 /dev/null
//  starnet_daemon_exit(pidfile)：删除 pidfile
//注意：daemon 化后 stdio 进入 /dev/null，config.logger 必须配文件才有日志（对齐 skynet）

//守护进程化；pidfile 非空时后台运行。成功返回 0；失败（已运行 / daemon 化失败 / 写 pidfile 失败）返回 1
int starnet_daemon_init(const char* pidfile);

//退出时删除 pidfile
int starnet_daemon_exit(const char* pidfile);
