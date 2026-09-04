// =====================================================================
// xprobe/ThreadPool.cpp
// 固定大小线程池实现
// =====================================================================
#include "xprobe/ThreadPool.h"

#include <string>

#include "xprobe/logger.h"

namespace xprobe {

ThreadPool::ThreadPool(size_t threadCount) {
    // 线程数至少为 1，防止误传 0 导致无法消费任务
    if (threadCount == 0) {
        threadCount = 1;
    }
    workers_.reserve(threadCount);
    for (size_t i = 0; i < threadCount; ++i) {
        workers_.emplace_back([this] { workerLoop(); });
    }
    log(LogLevel::DEBUG, "pool", "线程池已启动（" + std::to_string(threadCount) + " 个工作线程）");
}

ThreadPool::~ThreadPool() {
    shutdown();
}

void ThreadPool::submit(std::function<void()> task) {
    if (!task) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(mtx_);
        if (stopped_) {
            return; // 已停止：丢弃新任务
        }
        tasks_.push(std::move(task));
    }
    cv_.notify_one();
}

void ThreadPool::shutdown() {
    {
        std::lock_guard<std::mutex> lock(mtx_);
        if (stopped_) {
            return;
        }
        stopped_ = true;
    }
    log(LogLevel::DEBUG, "pool", "线程池已关闭");
    cv_.notify_all(); // 唤醒全部工作线程退出
    for (auto& t : workers_) {
        if (t.joinable()) {
            t.join();
        }
    }
    workers_.clear();
}

bool ThreadPool::isStopped() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return stopped_;
}

void ThreadPool::workerLoop() {
    while (true) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(mtx_);
            cv_.wait(lock, [this] { return stopped_ || !tasks_.empty(); });
            if (stopped_ && tasks_.empty()) {
                return; // 停止且任务清空后退出
            }
            task = std::move(tasks_.front());
            tasks_.pop();
        }
        if (task) {
            task(); // 任务在锁外执行，避免长时间持锁
        }
    }
}

} // namespace xprobe
