// =====================================================================
// xprobe/ThreadPool.h
// 固定大小线程池（std::thread + condition_variable 实现，无外部依赖）
// =====================================================================
#ifndef XPROBE_THREAD_POOL_H
#define XPROBE_THREAD_POOL_H

#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace xprobe {

class ThreadPool {
public:
    // 构造即启动 threadCount 个工作线程
    explicit ThreadPool(size_t threadCount = 8);

    // 析构自动 shutdown()
    ~ThreadPool();

    // 禁止拷贝与移动
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    // 提交任务（shutdown 后提交被忽略）
    void submit(std::function<void()> task);

    // 停止接收新任务，唤醒全部线程，等待已提交任务执行完毕后返回
    void shutdown();

    // 是否已停止
    bool isStopped() const;

private:
    // 工作线程主循环
    void workerLoop();

    std::vector<std::thread> workers_;          // 固定工作线程集合
    std::queue<std::function<void()>> tasks_;   // 无界任务队列
    mutable std::mutex mtx_;                    // 保护 tasks_ / stop_
    std::condition_variable cv_;                // 任务到达 / 停止通知
    bool stopped_ = false;                      // 停止标记
};

} // namespace xprobe

#endif // XPROBE_THREAD_POOL_H
