// =====================================================================
// xprobe/AutoTestMgr.h
// 对外门面：组合 TcpServer + ThreadPool + 协议解析 + 命令分发
// 用法：
//   AutoTestMgr mgr;
//   mgr.start(&myInvocation, 9000);       // 阻塞前启动，之后 sendXxx 可任意线程调用
//   ...
//   mgr.stop();
// =====================================================================
#ifndef XPROBE_AUTO_TEST_MGR_H
#define XPROBE_AUTO_TEST_MGR_H

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "xprobe/json.h"
#include "xprobe/protocol.h"
#include "xprobe/TcpServer.h"
#include "xprobe/ThreadPool.h"

namespace xprobe {

// =====================================================================
// 自定义调用接口：业务方实现并注册；
// ver==2 的命令以 callMethod(apiName, params) 形式分发，
// ver!=2 的命令以 callMethod(api子对象序列化串, 空对象) 形式分发（C++ Core 特有分支）
// =====================================================================
class ICustomInvocation {
public:
    virtual ~ICustomInvocation() = default;

    // apiName：ver2 为 api.apiName；兼容模式为 api 子对象的 JSON 序列化字符串
    // params ：ver2 为 api.params 对象；兼容模式为空对象
    virtual void callMethod(const std::string& apiName, const JsonValue& params) = 0;
};

// =====================================================================
// 主线程投递接口：threadMode==1 的命令经此投递到平台主线程执行
// 桌面宿主可注入真实的主线程 poster（如 Windows 消息循环投递）
// =====================================================================
class IMainThreadPoster {
public:
    virtual ~IMainThreadPoster() = default;
    virtual void post(std::function<void()> task) = 0;
};

// 默认 poster：直接在调用线程同步执行（未注入主线程环境时的兜底行为）
class SyncMainThreadPoster : public IMainThreadPoster {
public:
    void post(std::function<void()> task) override {
        if (task) {
            task();
        }
    }
};

// =====================================================================
// AutoTestMgr：对外门面（非单例，可按需创建；生命周期内管理全部资源）
// =====================================================================
class AutoTestMgr {
public:
    // 后台命令执行线程池的固定线程数
    static constexpr size_t kThreadPoolSize = 8;

    AutoTestMgr();
    ~AutoTestMgr();

    // 禁止拷贝与移动
    AutoTestMgr(const AutoTestMgr&) = delete;
    AutoTestMgr& operator=(const AutoTestMgr&) = delete;

    // 启动服务：注册自定义调用实现并监听 port（缺省 9000）
    void start(ICustomInvocation* invocation, uint16_t port = 9000);

    // 停止服务：断开全部连接、关闭线程池
    void stop();

    // 注入主线程投递器（nullptr 恢复为默认同步执行；可在 start 前调用）
    void setMainThreadPoster(IMainThreadPoster* poster);

    // ---- 以下发送接口线程安全，发往最近活跃连接；无连接时丢弃 ----

    // 方法返回值（type=return）
    void sendReturn(const std::string& key, const std::string& value);

    // 错误回报（type=error；解析失败 key=parseError，执行异常 key=方法名）
    void sendError(const std::string& key, const std::string& msg);

    // 异步回调上报（type=callback），value 为文本
    void sendCallback(const std::string& key, const std::string& value);

    // 异步回调上报（type=callback），value 为 json 对象的序列化字符串。
    // 对应 PROTOCOL.md §6 的 sendCallbackJson(callbackName, Map<String,Object> json)。
    // 边界：json 非对象时记日志并降级为空对象 "{}"，不中断调用方。
    void sendCallbackJson(const std::string& key, const JsonValue& json);

private:
    // 消息总入口：特殊帧处理 + 命令解析分发
    void handlePayload(TcpServer::ConnId id, const std::string& payload);

    // 命令分组派发：连续相同 threadMode 的命令合并为一个任务，
    // threadMode==1 投递到主线程 poster，否则提交线程池
    void dispatchCommands(std::vector<Command> commands);

    // 执行单条命令：ver2 走 (apiName, params)，其余透传 api 序列化串
    void executeCommand(const Command& cmd);

    // 连接断开通知：清理最近活跃连接记录
    void onDisconnected(TcpServer::ConnId id);

    // 组装并发送统一返回帧 {"type":..,"key":..,"value":..}
    void sendTypedJson(const char* type, const std::string& key, const std::string& value);

    TcpServer server_;                       // TCP 服务
    std::unique_ptr<ThreadPool> pool_;       // 后台命令线程池（start 时创建）
    SyncMainThreadPoster defaultPoster_;     // 默认同步 poster
    IMainThreadPoster* poster_ = &defaultPoster_; // 当前生效 poster（不拥有生命周期）
    ICustomInvocation* invocation_ = nullptr;     // 业务调用实现（外部拥有）
    std::mutex latestMtx_;                   // 保护 latestConnId_
    TcpServer::ConnId latestConnId_ = 0;     // 最近活跃连接（0 表示无）
    bool started_ = false;                   // 启动标记
};

} // namespace xprobe

#endif // XPROBE_AUTO_TEST_MGR_H
