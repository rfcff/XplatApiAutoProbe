// =====================================================================
// xprobe/TcpServer.h
// 跨平台（Windows/macOS/Linux）TCP 服务端：
//   - Windows 基于 winsock2（WSAStartup + ws2_32.lib）
//   - POSIX 基于 sys/socket
//   - select() 事件循环，支持多客户端连接
//   - 帧协议：4 字节大端长度头 + payload（内部用 FrameCodec 处理粘包/半包）
//   - 发送采用“入队 + 事件循环统一写出”模型，任意线程可安全调用发送接口
// =====================================================================
#ifndef XPROBE_TCP_SERVER_H
#define XPROBE_TCP_SERVER_H

// Windows 下扩大 fd_set 容量（默认仅 64，必须在包含 winsock2.h 之前定义）
#ifdef _WIN32
#ifndef FD_SETSIZE
#define FD_SETSIZE 4096
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "xprobe/protocol.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace xprobe {

class TcpServer {
public:
    // 连接标识（服务端内部递增分配）
    using ConnId = uint64_t;

    // 事件回调（均在事件循环线程中被调用，回调内不应执行耗时操作；
    // 也不得在回调中直接调用 stop()）
    using ConnectedCb = std::function<void(ConnId id)>;
    using DisconnectedCb = std::function<void(ConnId id)>;
    using MessageCb = std::function<void(ConnId id, const std::string& payload)>;

    TcpServer();
    ~TcpServer();

    // 禁止拷贝与移动
    TcpServer(const TcpServer&) = delete;
    TcpServer& operator=(const TcpServer&) = delete;

    // 注册回调（须在 start 之前调用）
    void setCallbacks(ConnectedCb onConnected, DisconnectedCb onDisconnected, MessageCb onMessage);

    // 启动服务：监听 0.0.0.0:port 并开启事件循环线程
    bool start(uint16_t port);

    // 停止服务：结束事件循环、关闭全部连接（不得在回调线程中调用）
    void stop();

    // 是否处于运行状态
    bool isRunning() const;

    // 发送一帧（自动加长度头）到指定连接；数据先入队，由事件循环写出。
    // 连接不存在（或已停止）时返回 false。线程安全。
    bool sendToConnection(ConnId id, const std::string& payload);

    // 主动关闭指定连接（触发 onDisconnected）
    void closeConnection(ConnId id);

private:
    // 跨平台 socket 句柄与错误码统一
#ifdef _WIN32
    using socket_t = SOCKET;
    static constexpr socket_t kInvalidSocket = INVALID_SOCKET;
#else
    using socket_t = int;
    static constexpr socket_t kInvalidSocket = -1;
#endif

    // 单个连接的上下文
    struct Connection {
        socket_t fd = kInvalidSocket; // 套接字
        FrameCodec codec;             // 接收解码器（处理粘包/半包）
        std::string sendBuf;          // 待发送数据（含长度头）
        size_t sentOffset = 0;        // 已发送字节数

        bool hasPendingData() const { return sentOffset < sendBuf.size(); }
    };

    // 事件循环主函数（独立线程执行）
    void eventLoop();

    // 接受新连接（循环 accept 直至耗尽 backlog）
    void acceptClients();

    // 处理连接可读事件；返回 false 表示连接已死亡
    bool handleReadable(ConnId id, socket_t fd);

    // 处理连接可写事件（冲刷发送缓冲）；返回 false 表示连接已死亡
    bool handleWritable(ConnId id, socket_t fd);

    // 移除连接并关闭套接字；返回 fd（用于事件循环外触发 onDisconnected）
    bool removeConnection(ConnId id);

    // 关闭监听与全部连接套接字（仅事件循环线程退出路径调用）
    void closeAllSockets();

    // 设置套接字为非阻塞（macOS 同时设置 SO_NOSIGPIPE）
    static bool setNonBlocking(socket_t fd);

    // 关闭套接字（跨平台封装）
    static void closeSocket(socket_t fd);

    // 关闭连接的读写通道（触发对端/本端检测到断开，但不释放句柄）
    static void shutdownSocket(socket_t fd);

    socket_t listenFd_ = kInvalidSocket;      // 监听套接字
    std::map<ConnId, Connection> connections_; // 连接表
    mutable std::mutex connsMtx_;              // 保护 connections_ 及各连接发送缓冲
    ConnId nextConnId_ = 1;                    // 连接 id 分配器
    std::thread loopThread_;                   // 事件循环线程
    std::atomic<bool> running_{false};         // 运行标记
#ifdef _WIN32
    bool wsaStarted_ = false;                  // Windows 下 WSAStartup 成功标记
#endif

    // 用户回调
    ConnectedCb onConnected_;
    DisconnectedCb onDisconnected_;
    MessageCb onMessage_;
};

} // namespace xprobe

#endif // XPROBE_TCP_SERVER_H
