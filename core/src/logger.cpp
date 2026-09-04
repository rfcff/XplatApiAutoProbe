// =====================================================================
// xprobe/logger.cpp
// 统一日志实现：
//   - 回调用互斥锁保护读写（日志可能从网络线程/线程池并发打出）
//   - 回调持有 shared_ptr 快照，锁外调用：既避免回调内再操作日志接口造成死锁，
//     也保证替换/清空回调不会使正在执行的回调失效
//   - 默认输出：[xprobe][LEVEL][tag] msg（ERROR/WARN 到 stderr，其余到 stdout）
// =====================================================================
#include "xprobe/logger.h"

#include <cstdio>
#include <memory>
#include <mutex>

namespace xprobe {

namespace {

// 回调存储（互斥锁保护写入，读取仅复制 shared_ptr 快照）
std::mutex gLogMtx;
std::shared_ptr<LogCallback> gLogCb;

// 级别名（默认输出使用）
const char* levelName(int level) {
    switch (level) {
        case LogLevel::VERBOSE: return "VERBOSE";
        case LogLevel::DEBUG:   return "DEBUG";
        case LogLevel::INFO:    return "INFO";
        case LogLevel::WARN:    return "WARN";
        case LogLevel::ERROR:   return "ERROR";
        default:                return "UNKNOWN";
    }
}

} // namespace

void setLogCallback(LogCallback cb) {
    std::lock_guard<std::mutex> lock(gLogMtx);
    if (cb) {
        gLogCb = std::make_shared<LogCallback>(std::move(cb));
    } else {
        gLogCb.reset(); // 空回调：恢复默认输出
    }
}

LogCallback getLogCallback() {
    std::lock_guard<std::mutex> lock(gLogMtx);
    return gLogCb ? *gLogCb : LogCallback();
}

void log(int level, const std::string& tag, const std::string& msg) {
    // 锁内仅复制 shared_ptr 快照，锁外执行用户回调
    std::shared_ptr<LogCallback> cb;
    {
        std::lock_guard<std::mutex> lock(gLogMtx);
        cb = gLogCb;
    }
    if (cb && *cb) {
        (*cb)(level, tag, msg);
        return;
    }
    // 默认输出：ERROR/WARN 到 stderr，其余到 stdout（stdio 单次调用自身线程安全）
    std::FILE* out = (level >= LogLevel::WARN) ? stderr : stdout;
    std::fprintf(out, "[xprobe][%s][%s] %s\n", levelName(level), tag.c_str(), msg.c_str());
}

} // namespace xprobe
