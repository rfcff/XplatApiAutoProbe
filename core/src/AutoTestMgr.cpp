// =====================================================================
// xprobe/AutoTestMgr.cpp
// 门面实现：消息入口、特殊帧、命令分组派发、返回帧发送
// =====================================================================
#include "xprobe/AutoTestMgr.h"

#include "xprobe/logger.h"

namespace xprobe {

AutoTestMgr::AutoTestMgr() = default;

AutoTestMgr::~AutoTestMgr() {
    stop();
}

void AutoTestMgr::start(ICustomInvocation* invocation, uint16_t port) {
    if (started_) {
        return; // 已在运行，防止重复启动
    }
    invocation_ = invocation;

    // 创建固定大小后台线程池
    pool_ = std::make_unique<ThreadPool>(kThreadPoolSize);

    // 注册网络事件回调
    server_.setCallbacks(
        [](TcpServer::ConnId id) {
            (void)id; // 连接建立：无需处理（发往“最近活跃连接”的策略在收到消息时生效）
        },
        [this](TcpServer::ConnId id) { onDisconnected(id); },
        [this](TcpServer::ConnId id, const std::string& payload) { handlePayload(id, payload); });

    if (!server_.start(port)) {
        pool_.reset();
        invocation_ = nullptr;
        log(LogLevel::ERROR, "mgr", "启动失败：无法监听端口 " + std::to_string(port));
        return;
    }
    started_ = true;
    log(LogLevel::INFO, "mgr", "AutoTestMgr 已启动（端口 " + std::to_string(port) + "）");
}

void AutoTestMgr::stop() {
    if (!started_) {
        return;
    }
    started_ = false;
    // 先停事件循环（不再产生新任务，sendToConnection 之后安全返回 false）
    server_.stop();
    // 再关闭线程池（等待存量任务执行完毕；任务内的发送因连接已清空而安全）
    if (pool_) {
        pool_->shutdown();
        pool_.reset();
    }
    invocation_ = nullptr;
    {
        std::lock_guard<std::mutex> lock(latestMtx_);
        latestConnId_ = 0;
    }
    log(LogLevel::INFO, "mgr", "AutoTestMgr 已停止");
}

void AutoTestMgr::setMainThreadPoster(IMainThreadPoster* poster) {
    poster_ = (poster != nullptr) ? poster : &defaultPoster_;
}

void AutoTestMgr::sendReturn(const std::string& key, const std::string& value) {
    sendTypedJson("return", key, value);
}

void AutoTestMgr::sendError(const std::string& key, const std::string& msg) {
    sendTypedJson("error", key, msg);
}

void AutoTestMgr::sendCallback(const std::string& key, const std::string& value) {
    sendTypedJson("callback", key, value);
}

void AutoTestMgr::sendCallbackJson(const std::string& key, const JsonValue& json) {
    // 公开 API 属边界：json 非对象时降级为空对象，保证回包始终是合法 JSON
    if (!json.isObject()) {
        log(LogLevel::ERROR, "mgr", "sendCallbackJson 的 json 参数不是对象，降级为空对象: " + key);
        sendTypedJson("callback", key, "{}");
        return;
    }
    sendTypedJson("callback", key, json.serialize());
}

void AutoTestMgr::handlePayload(TcpServer::ConnId id, const std::string& payload) {
    // 记录最近活跃连接（后续 sendReturn/sendError/sendCallback 的目标）
    {
        std::lock_guard<std::mutex> lock(latestMtx_);
        latestConnId_ = id;
    }

    // ---- 特殊文本帧（非 JSON） ----
    if (payload == "PING") {
        // 心跳：回复 PONG
        server_.sendToConnection(id, "PONG");
        return;
    }
    if (payload == "PONG") {
        // 心跳响应：忽略
        return;
    }
    if (payload.rfind("GET_API:", 0) == 0) {
        // 反射枚举接口：C++ 无运行时反射，返回空数组
        server_.sendToConnection(id, "[]");
        return;
    }

    // ---- JSON 命令帧 ----
    try {
        std::vector<Command> commands = MessageParser::parse(payload);
        dispatchCommands(std::move(commands));
    } catch (const MessageParseException& e) {
        // 解析失败：错误消息含原文
        sendError("parseError", e.what());
    }
}

void AutoTestMgr::dispatchCommands(std::vector<Command> commands) {
    // 把同一帧内「连续的同线程模式」命令合并为一个任务，
    // 保证同组命令在同一线程顺序执行；线程模式变化时切换任务
    size_t i = 0;
    while (i < commands.size()) {
        int mode = commands[i].threadMode;
        size_t j = i;
        while (j < commands.size() && commands[j].threadMode == mode) {
            ++j;
        }
        // 移动构造 [i, j) 区间为一个执行组
        std::vector<Command> group(std::make_move_iterator(commands.begin() + static_cast<long>(i)),
                                   std::make_move_iterator(commands.begin() + static_cast<long>(j)));

        log(LogLevel::DEBUG, "mgr",
            "派发命令组: count=" + std::to_string(group.size()) +
                ", threadMode=" + (mode == 1 ? "MAIN" : "BACKGROUND") +
                ", first=" + group.front().commandName);

        auto task = [this, group = std::move(group)]() {
            for (const Command& cmd : group) {
                executeCommand(cmd);
            }
        };

        if (mode == 1) {
            // MAIN：投递到主线程（或注入的 poster）执行
            poster_->post(std::move(task));
        } else {
            // BACKGROUND：提交后台线程池
            if (pool_) {
                pool_->submit(std::move(task));
            }
        }
        i = j;
    }
}

void AutoTestMgr::executeCommand(const Command& cmd) {
    if (invocation_ == nullptr) {
        return;
    }
    try {
        if (cmd.isVer2) {
            // ver2：新式自定义调用，直接透传方法名与参数对象
            invocation_->callMethod(cmd.apiName, cmd.params);
        } else {
            // ver!=2：把 apiRaw（Method 帧为 api 子对象，Field 帧为整条命令对象）序列化后透传
            invocation_->callMethod(cmd.apiRaw.serialize(), JsonValue::makeObject());
        }
    } catch (const std::exception& e) {
        // 执行异常：以命令名作为 key 回报错误
        log(LogLevel::ERROR, "mgr", "命令执行异常: " + cmd.commandName + " -> " + e.what());
        sendError(cmd.commandName, e.what());
    } catch (...) {
        log(LogLevel::ERROR, "mgr", "命令执行异常: " + cmd.commandName + " -> unknown exception");
        sendError(cmd.commandName, "unknown exception");
    }
}

void AutoTestMgr::onDisconnected(TcpServer::ConnId id) {
    std::lock_guard<std::mutex> lock(latestMtx_);
    if (latestConnId_ == id) {
        latestConnId_ = 0; // 最近活跃连接已断开：后续发送暂无目标
    }
}

void AutoTestMgr::sendTypedJson(const char* type, const std::string& key, const std::string& value) {
    // 组装统一返回帧：{"type":"...","key":"...","value":"..."}
    JsonValue json = JsonValue::makeObject();
    json["type"] = type;
    json["key"] = key;
    json["value"] = value;

    TcpServer::ConnId target = 0;
    {
        std::lock_guard<std::mutex> lock(latestMtx_);
        target = latestConnId_;
    }
    if (target != 0) {
        server_.sendToConnection(target, json.serialize());
    }
    // 无活跃连接时静默丢弃（与旧实现行为一致）
}

} // namespace xprobe
