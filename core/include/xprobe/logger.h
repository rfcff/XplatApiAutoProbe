// =====================================================================
// xprobe/logger.h
// 统一日志接口（三端对齐，勿改动级别数值与回调签名）：
//   - 日志级别：0=VERBOSE 1=DEBUG 2=INFO 3=WARN 4=ERROR
//   - 回调签名：void(int level, const std::string& tag, const std::string& msg)
//   - 默认行为（未注册回调时）：[xprobe][LEVEL][tag] msg
//     输出到 stdout（ERROR/WARN 输出到 stderr）
// =====================================================================
#ifndef XPROBE_LOGGER_H
#define XPROBE_LOGGER_H

#include <functional>
#include <string>

namespace xprobe {

// 日志级别常量（三端对齐）
namespace LogLevel {
enum {
    VERBOSE = 0,
    DEBUG = 1,
    INFO = 2,
    WARN = 3,
    ERROR = 4,
};
} // namespace LogLevel

// 日志回调：level 取值同 LogLevel 常量；tag 为模块名（如 "tcp"/"mgr"/"parser"/"pool"）；
// 回调可能在网络线程/线程池线程并发调用，实现方需自行保证线程安全
using LogCallback = std::function<void(int level, const std::string& tag, const std::string& msg)>;

// 注册日志回调（传 nullptr 恢复默认输出）；线程安全，可在任意时刻调用
void setLogCallback(LogCallback cb);

// 获取当前注册的回调（未注册时返回空 function；内部使用）
LogCallback getLogCallback();

// 统一日志入口：已注册回调时转发给回调，否则按默认格式输出到 stdout/stderr；线程安全
void log(int level, const std::string& tag, const std::string& msg);

} // namespace xprobe

#endif // XPROBE_LOGGER_H
