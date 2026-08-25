#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#define sphaira_USE_LOG 1

#include <stdarg.h>

#if sphaira_USE_LOG
bool log_file_init();
bool log_nxlink_init();
void log_file_exit();
bool log_is_init();

void log_nxlink_exit();
void log_write(const char* s, ...) __attribute__ ((format (printf, 1, 2)));
void log_write_arg(const char* s, va_list* v);

// 新增功能（网盘、影视仓等）日志：单独写入 logs/ 目录，与主日志 log.txt 分离。
void log_write_feature(const char* s, ...) __attribute__ ((format (printf, 1, 2)));
#else
inline bool log_file_init() {
    return true;
}
inline bool log_nxlink_init() {
    return true;
}
#define log_file_exit()
#define log_nxlink_exit()
#define log_write(...)
#define log_write_arg(...)
#define log_write_feature(...)
#endif

#ifdef __cplusplus
}
#endif
