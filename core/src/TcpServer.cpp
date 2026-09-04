// =====================================================================
// xprobe/TcpServer.cpp
// 跨平台 select() TCP 服务端实现
// =====================================================================
#include "xprobe/TcpServer.h"

#include "xprobe/logger.h"

namespace xprobe {

// send() 标志：Linux 使用 MSG_NOSIGNAL 防止 SIGPIPE；
// macOS 已在套接字上设置 SO_NOSIGPIPE；Windows 无此概念
#ifdef _WIN32
static constexpr int kSendFlags = 0;
#elif defined(MSG_NOSIGNAL)
static constexpr int kSendFlags = MSG_NOSIGNAL;
#else
static constexpr int kSendFlags = 0;
#endif

TcpServer::TcpServer() = default;

TcpServer::~TcpServer() {
    stop();
}

void TcpServer::setCallbacks(ConnectedCb onConnected, DisconnectedCb onDisconnected, MessageCb onMessage) {
    onConnected_ = std::move(onConnected);
    onDisconnected_ = std::move(onDisconnected);
    onMessage_ = std::move(onMessage);
}

bool TcpServer::setNonBlocking(socket_t fd) {
#ifdef _WIN32
    u_long mode = 1; // 1 = 非阻塞
    return ioctlsocket(fd, FIONBIO, &mode) == 0;
#else
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return false;
    }
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

void TcpServer::closeSocket(socket_t fd) {
    if (fd == kInvalidSocket) {
        return;
    }
#ifdef _WIN32
    closesocket(fd);
#else
    ::close(fd);
#endif
}

void TcpServer::shutdownSocket(socket_t fd) {
    if (fd == kInvalidSocket) {
        return;
    }
#ifdef _WIN32
    ::shutdown(fd, SD_BOTH);
#else
    ::shutdown(fd, SHUT_RDWR);
#endif
}

bool TcpServer::start(uint16_t port) {
    if (running_.load()) {
        return true; // 已在运行
    }

#ifdef _WIN32
    // Windows：初始化 Winsock（每个 start 成功对应一次 WSACleanup）
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        log(LogLevel::ERROR, "tcp", "WSAStartup 失败");
        return false;
    }
    wsaStarted_ = true;
#endif

    // 创建监听套接字
    listenFd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listenFd_ == kInvalidSocket) {
        log(LogLevel::ERROR, "tcp", "创建监听套接字失败");
#ifdef _WIN32
        WSACleanup();
        wsaStarted_ = false;
#endif
        return false;
    }

    // 允许地址复用，便于快速重启
    int reuse = 1;
    ::setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));

    // 绑定 0.0.0.0:port
    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    if (::bind(listenFd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        log(LogLevel::ERROR, "tcp", "bind 0.0.0.0:" + std::to_string(port) + " 失败");
        closeSocket(listenFd_);
        listenFd_ = kInvalidSocket;
#ifdef _WIN32
        WSACleanup();
        wsaStarted_ = false;
#endif
        return false;
    }

    if (::listen(listenFd_, 16) != 0) {
        log(LogLevel::ERROR, "tcp", "listen 失败");
        closeSocket(listenFd_);
        listenFd_ = kInvalidSocket;
#ifdef _WIN32
        WSACleanup();
        wsaStarted_ = false;
#endif
        return false;
    }

    if (!setNonBlocking(listenFd_)) {
        log(LogLevel::ERROR, "tcp", "设置监听套接字非阻塞失败");
        closeSocket(listenFd_);
        listenFd_ = kInvalidSocket;
#ifdef _WIN32
        WSACleanup();
        wsaStarted_ = false;
#endif
        return false;
    }

    log(LogLevel::INFO, "tcp", "服务已启动，监听 0.0.0.0:" + std::to_string(port));
    running_.store(true);
    loopThread_ = std::thread([this] { eventLoop(); });
    return true;
}

void TcpServer::stop() {
    if (!running_.exchange(false)) {
        return; // 未运行或已停止
    }
    if (loopThread_.joinable()) {
        loopThread_.join(); // 事件循环将在超时周期内感知停止标记并退出
    }
#ifdef _WIN32
    if (wsaStarted_) {
        WSACleanup();
        wsaStarted_ = false;
    }
#endif
    log(LogLevel::INFO, "tcp", "服务已停止");
}

bool TcpServer::isRunning() const {
    return running_.load();
}

bool TcpServer::sendToConnection(ConnId id, const std::string& payload) {
    // 仅把帧数据入队（含长度头），实际写出由事件循环完成，天然串行免竞争
    std::lock_guard<std::mutex> lock(connsMtx_);
    auto it = connections_.find(id);
    if (it == connections_.end()) {
        return false;
    }
    it->second.sendBuf.append(FrameCodec::encode(payload));
    return true;
}

void TcpServer::closeConnection(ConnId id) {
    // 只关闭读写通道，句柄由事件循环统一回收，
    // 避免另一线程 close 后 select 继续引用该句柄的未定义行为
    socket_t fd = kInvalidSocket;
    {
        std::lock_guard<std::mutex> lock(connsMtx_);
        auto it = connections_.find(id);
        if (it != connections_.end()) {
            fd = it->second.fd;
        }
    }
    if (fd != kInvalidSocket) {
        shutdownSocket(fd);
    }
}

void TcpServer::eventLoop() {
    // 事件循环：每轮快照连接表 → select → 分发读写事件
    while (running_.load()) {
        // ---- 构造 fd 集合（持锁快照，锁内不做阻塞操作） ----
        fd_set readSet;
        fd_set writeSet;
        FD_ZERO(&readSet);
        FD_ZERO(&writeSet);

        socket_t maxFd = listenFd_;
        std::vector<std::pair<ConnId, socket_t>> snapshot;
        {
            std::lock_guard<std::mutex> lock(connsMtx_);
            FD_SET(listenFd_, &readSet);
            for (auto& kv : connections_) {
#ifndef _WIN32
                // POSIX 下 fd_set 按 fd 值索引，超出 FD_SETSIZE 无法参与 select，跳过
                if (kv.second.fd >= static_cast<socket_t>(FD_SETSIZE)) {
                    continue;
                }
#endif
                FD_SET(kv.second.fd, &readSet);
                if (kv.second.hasPendingData()) {
                    FD_SET(kv.second.fd, &writeSet); // 有积压数据才关注可写
                }
                if (kv.second.fd > maxFd) {
                    maxFd = kv.second.fd;
                }
                snapshot.emplace_back(kv.first, kv.second.fd);
            }
        }

        // ---- select（100ms 超时，兼顾停止响应与空闲功耗） ----
        timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 100 * 1000;
        int rc = ::select(static_cast<int>(maxFd) + 1, &readSet, &writeSet, nullptr, &tv);
        if (rc < 0) {
#ifdef _WIN32
            int err = WSAGetLastError();
            if (err == WSAEINTR) {
                continue;
            }
#else
            if (errno == EINTR) {
                continue;
            }
#endif
            break; // select 发生不可恢复错误，退出循环
        }
        if (rc == 0) {
            continue; // 超时：回到循环头检查运行标记
        }

        // ---- 新连接 ----
        if (FD_ISSET(listenFd_, &readSet)) {
            acceptClients();
        }

        // ---- 可读事件：接收数据并切帧 ----
        for (const auto& p : snapshot) {
            if (FD_ISSET(p.second, &readSet) && !handleReadable(p.first, p.second)) {
                if (removeConnection(p.first) && onDisconnected_) {
                    onDisconnected_(p.first);
                }
            }
        }

        // ---- 可写事件：冲刷发送缓冲 ----
        for (const auto& p : snapshot) {
            if (FD_ISSET(p.second, &writeSet) && !handleWritable(p.first, p.second)) {
                if (removeConnection(p.first) && onDisconnected_) {
                    onDisconnected_(p.first);
                }
            }
        }
    }

    // 退出路径：关闭监听与全部连接（仅本线程触碰套接字句柄，安全）
    closeAllSockets();
}

void TcpServer::acceptClients() {
    while (true) {
        sockaddr_in peerAddr;
#ifdef _WIN32
        int addrLen = static_cast<int>(sizeof(peerAddr));
#else
        socklen_t addrLen = sizeof(peerAddr);
#endif
        socket_t fd = ::accept(listenFd_, reinterpret_cast<sockaddr*>(&peerAddr), &addrLen);
        if (fd == kInvalidSocket) {
            // 非阻塞 accept：无等待连接时直接返回
            break;
        }

#ifndef _WIN32
        // POSIX 下 fd_set 按 fd 值索引，超出 FD_SETSIZE 无法参与 select，直接拒绝
        if (fd >= static_cast<socket_t>(FD_SETSIZE)) {
            closeSocket(fd);
            continue;
        }
#endif

        if (!setNonBlocking(fd)) {
            closeSocket(fd);
            continue;
        }
#ifdef __APPLE__
        // macOS：防止 send 对已关闭连接触发 SIGPIPE
        int nosig = 1;
        ::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &nosig, sizeof(nosig));
#endif

        ConnId id;
        {
            std::lock_guard<std::mutex> lock(connsMtx_);
            id = nextConnId_++;
            connections_[id].fd = fd;
        }
        log(LogLevel::INFO, "tcp", "连接建立 id=" + std::to_string(id));
        if (onConnected_) {
            onConnected_(id);
        }
    }
}

bool TcpServer::handleReadable(ConnId id, socket_t fd) {
    // 循环读空内核接收缓冲（非阻塞）
    std::string received;
    char buf[16 * 1024];
    bool dead = false;
    while (true) {
        int n = static_cast<int>(::recv(fd, buf, sizeof(buf), 0));
        if (n > 0) {
            received.append(buf, static_cast<size_t>(n));
            continue;
        }
        if (n == 0) {
            dead = true; // 对端正常关闭
            break;
        }
#ifdef _WIN32
        int err = WSAGetLastError();
        if (err == WSAEWOULDBLOCK) {
            break; // 本轮数据已读完
        }
        if (err == WSAEINTR || err == WSAEINPROGRESS) {
            continue;
        }
#else
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            break; // 本轮数据已读完
        }
        if (errno == EINTR) {
            continue;
        }
#endif
        dead = true; // 其余错误视为连接失效
        break;
    }

    if (received.empty() && !dead) {
        return true;
    }

    // 切帧（持锁，防止与发送缓冲操作竞争连接对象）
    std::vector<std::string> frames;
    if (!received.empty()) {
        std::lock_guard<std::mutex> lock(connsMtx_);
        auto it = connections_.find(id);
        if (it == connections_.end()) {
            return false; // 连接已被移除
        }
        frames = it->second.codec.feed(received);
        if (it->second.codec.hasError()) {
            log(LogLevel::ERROR, "tcp", "帧长超限（协议错误），断开连接 id=" + std::to_string(id));
            return false; // 帧长超限等协议错误：断开连接
        }
    }

    // 回调在锁外执行（回调中可能调用发送接口）
    if (onMessage_) {
        for (auto& f : frames) {
            onMessage_(id, f);
        }
    }
    return !dead;
}

bool TcpServer::handleWritable(ConnId id, socket_t fd) {
    std::lock_guard<std::mutex> lock(connsMtx_);
    auto it = connections_.find(id);
    if (it == connections_.end()) {
        return true; // 连接已移除，无需处理
    }
    Connection& conn = it->second;
    while (conn.hasPendingData()) {
        int n = static_cast<int>(::send(fd, conn.sendBuf.data() + conn.sentOffset,
                                        static_cast<int>(conn.sendBuf.size() - conn.sentOffset), kSendFlags));
        if (n > 0) {
            conn.sentOffset += static_cast<size_t>(n);
            continue;
        }
        if (n < 0) {
#ifdef _WIN32
            int err = WSAGetLastError();
            if (err == WSAEWOULDBLOCK) {
                return true; // 内核发送缓冲满：留待下轮
            }
            if (err == WSAEINTR) {
                continue;
            }
#else
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return true; // 内核发送缓冲满：留待下轮
            }
            if (errno == EINTR) {
                continue;
            }
#endif
            log(LogLevel::ERROR, "tcp", "发送失败，断开连接 id=" + std::to_string(id));
            return false; // 发送错误：断开连接
        }
        log(LogLevel::ERROR, "tcp", "send 返回 0，断开连接 id=" + std::to_string(id));
        return false; // send 返回 0 视为异常
    }
    // 全部发出：清空缓冲
    conn.sendBuf.clear();
    conn.sentOffset = 0;
    return true;
}

bool TcpServer::removeConnection(ConnId id) {
    socket_t fd = kInvalidSocket;
    {
        std::lock_guard<std::mutex> lock(connsMtx_);
        auto it = connections_.find(id);
        if (it == connections_.end()) {
            return false; // 已被移除（可能读写事件重复触发）
        }
        fd = it->second.fd;
        connections_.erase(it);
    }
    closeSocket(fd);
    log(LogLevel::INFO, "tcp", "连接断开 id=" + std::to_string(id));
    return true;
}

void TcpServer::closeAllSockets() {
    std::lock_guard<std::mutex> lock(connsMtx_);
    for (auto& kv : connections_) {
        closeSocket(kv.second.fd);
    }
    connections_.clear();
    if (listenFd_ != kInvalidSocket) {
        closeSocket(listenFd_);
        listenFd_ = kInvalidSocket;
    }
}

} // namespace xprobe
