#pragma once
//日志系统（对齐 skynet_error.c + skynet_log.c 的输出能力）
//线程安全，带 HH:MM:SS.mmm 时间戳与级别前缀
//输出目标：config.logger 指定的文件（追加），未指定或为空时写 stderr
//未调用 starnet_logger_init 时也能写 stderr（fallback，便于 config 加载阶段提前打日志）

//初始化：filename 为 NULL 或空字符串 → stderr；否则打开文件追加；返回是否成功
bool starnet_logger_init(const char* filename);
//关闭（释放文件句柄）
void starnet_logger_close();
//普通日志（[info] 级别）
void starnet_log(const char* fmt, ...);
//错误日志（[error] 级别）
void starnet_error(const char* fmt, ...);