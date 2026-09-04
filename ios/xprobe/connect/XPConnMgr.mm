//
//  XPConnMgr.mm
//  XplatApiAutoProbe —— iOS 统一测试 RPC 库
//
//  连接管理器实现：
//  - 专用 NSThread + RunLoop 承载 CFSocket 监听与各连接的 CFStream，
//    使 startTest 的调用线程不再影响网络层可用性；
//  - accept 回调在本网络线程上创建 XPServerConn 并挂到同一 RunLoop；
//  - lastActiveConn 用原子属性跨线程读写，sendResult 发往最近活跃连接。
//

#import "XPConnMgr.h"
#import "XPLog.h"

#import <sys/socket.h>
#import <netinet/in.h>
#import <arpa/inet.h>

// 服务端默认监听端口（线协议约定 9000）
static const NSInteger kXPDefaultListenPort = 9000;

#pragma mark - CFSocket 上下文持有 ObjC 对象（ARC 安全）

// 让 CFSocketContext 以 +1 方式持有 ObjC 对象（CFSocketContext 回调参数为 const void *）
static const void *XPSocketContextRetain(const void *info) {
    return CFBridgingRetain((__bridge id)(info));
}

// 与 XPSocketContextRetain 配对，释放持有的 ObjC 对象
static void XPSocketContextRelease(const void *info) {
    if (info) {
        (void)CFBridgingRelease(info);
    }
}

#pragma mark - XPConnMgr 私有声明（置于 C 回调之前，保证方法可见性）

@interface XPConnMgr () <XPServerConnDelegate>
{
    CFSocketRef _listenSocket; // 监听 socket（仅网络线程访问）
}

@property (nonatomic, weak, nullable) id<XPConnMgrDelegate> delegate;
@property (nonatomic, strong, nullable) NSThread *connThread;                 // 网络线程
@property (nonatomic, strong) NSMutableArray<XPServerConn *> *connections;   // 连接表（仅网络线程访问）
// 最近活跃连接：网络线程写（收到数据 / 连接断开 / 清理），sendResult 会在任意
// 调用线程读，必须 atomic —— nonatomic strong 的读不 retain、写不原子，
// 读写并发时会读到半初始化或已释放的对象，直接 EXC_BAD_ACCESS。
@property (atomic, strong, nullable) XPServerConn *lastActiveConn;
// 停止标记：由 openConn / closeConn 所在线程写，网络线程循环读，同样需要 atomic
@property (atomic, assign) BOOL shouldStopThread;

// 在网络线程上处理新接入的连接
- (void)onAcceptNativeHandle:(CFSocketNativeHandle)handle;

@end

#pragma mark - CFSocket accept 回调（在网络线程的 RunLoop 上触发）

static void XPConnMgrAcceptCallback(CFSocketRef socket,
                                    CFSocketCallBackType type,
                                    CFDataRef address,
                                    const void *data,
                                    void *info) {
    (void)socket;   // 未使用：句柄保存在监听 socket 内部
    (void)address;  // 未使用：对端地址不需要
    if (type != kCFSocketAcceptCallBack || data == NULL) {
        return;
    }
    // data 指向新连接的原生 socket 句柄
    CFSocketNativeHandle handle = *(const CFSocketNativeHandle *)data;
    XPConnMgr *mgr = (__bridge XPConnMgr *)(info);
    [mgr onAcceptNativeHandle:handle];
}

@implementation XPConnMgr

#pragma mark - 生命周期

- (instancetype)initWithDelegate:(id<XPConnMgrDelegate>)delegate {
    self = [super init];
    if (self) {
        _delegate = delegate;
        _connections = [NSMutableArray array];
        _listenSocket = NULL;
        _shouldStopThread = NO;
    }
    return self;
}

- (void)dealloc {
    // 兜底：未显式 closeConn 时在网络线程上清理（不应依赖此处）
    if (self.connThread && !self.connThread.finished) {
        [self closeConn];
    }
}

+ (NSInteger)defaultListenPort {
    return kXPDefaultListenPort;
}

#pragma mark - 启停

- (void)openConn:(NSInteger)port {
    if (port <= 0) {
        port = kXPDefaultListenPort;
    }
    // 幂等：先停掉旧的监听
    [self closeConn];

    self.shouldStopThread = NO;
    NSThread *thread = [[NSThread alloc] initWithTarget:self
                                               selector:@selector(connThreadMain:)
                                                 object:@(port)];
    thread.name = @"com.xprobe.rpc.conn";
    self.connThread = thread;
    [thread start];
}

- (void)closeConn {
    NSThread *thread = self.connThread;
    if (!thread || thread.finished) {
        self.connThread = nil;
        return;
    }
    self.shouldStopThread = YES;
    if (thread.executing) {
        // 在网络线程上执行清理并停止其 RunLoop；同步等待，
        // 保证 closeConn 返回后旧监听 socket 已释放，可立即重绑端口
        [self performSelector:@selector(stopRunLoopOnConnThread)
                     onThread:thread
                   withObject:nil
                waitUntilDone:YES];
    }
    self.connThread = nil;
    XPLog(XPLogLevelInfo, @"conn", @"监听已停止，全部客户端连接已关闭");
}

// 网络线程主函数：创建监听并常驻 RunLoop，直到 closeConn 触发停止
- (void)connThreadMain:(NSNumber *)portNumber {
    @autoreleasepool {
        NSRunLoop *runLoop = [NSRunLoop currentRunLoop];
        // 挂一个保活 Port，保证 RunLoop 不会自然退出
        [runLoop addPort:[NSPort port] forMode:NSDefaultRunLoopMode];

        // 在本线程创建监听 socket 并挂到本线程 RunLoop
        [self setupListenSocketWithPort:portNumber.integerValue];

        while (!self.shouldStopThread) {
            [runLoop runMode:NSDefaultRunLoopMode beforeDate:[NSDate distantFuture]];
        }

        [self cleanupOnConnThread];
    }
}

// 创建监听 socket（仅网络线程调用）
- (void)setupListenSocketWithPort:(NSInteger)port {
    CFSocketContext context = {0, (__bridge void *)self, XPSocketContextRetain, XPSocketContextRelease, NULL};
    CFSocketRef socket = CFSocketCreate(kCFAllocatorDefault,
                                        PF_INET,
                                        SOCK_STREAM,
                                        IPPROTO_TCP,
                                        kCFSocketAcceptCallBack,
                                        XPConnMgrAcceptCallback,
                                        &context);
    if (socket == NULL) {
        XPLog(XPLogLevelError, @"conn", @"创建监听 socket 失败");
        return;
    }

    // 允许端口复用，便于快速重启
    int reuse = 1;
    setsockopt(CFSocketGetNative(socket), SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_len = sizeof(address);
    address.sin_family = AF_INET;
    address.sin_port = htons((uint16_t)port);
    address.sin_addr.s_addr = htonl(INADDR_ANY); // 绑定 0.0.0.0

    CFDataRef addressData = CFDataCreate(kCFAllocatorDefault, (const UInt8 *)&address, sizeof(address));
    CFSocketError error = kCFSocketSuccess;
    if (addressData) {
        error = CFSocketSetAddress(socket, addressData);
        CFRelease(addressData);
    } else {
        error = kCFSocketError;
    }
    if (error != kCFSocketSuccess) {
        XPLog(XPLogLevelError, @"conn", @"绑定监听地址失败（端口 %ld 可能被占用）", (long)port);
        CFRelease(socket);
        return;
    }

    // 创建 RunLoop source 并挂到当前（网络线程）RunLoop
    CFRunLoopSourceRef source = CFSocketCreateRunLoopSource(kCFAllocatorDefault, socket, 0);
    if (source) {
        CFRunLoopAddSource(CFRunLoopGetCurrent(), source, kCFRunLoopCommonModes);
        CFRelease(source);
        self->_listenSocket = socket;
        XPLog(XPLogLevelInfo, @"conn", @"监听已启动 0.0.0.0:%ld", (long)port);
    } else {
        CFRelease(socket);
    }
}

// 网络线程停止前的清理（仅网络线程调用）
- (void)stopRunLoopOnConnThread {
    [self cleanupOnConnThread];
    // 令 connThreadMain 的 runMode 循环退出
    CFRunLoopStop(CFRunLoopGetCurrent());
}

- (void)cleanupOnConnThread {
    // 摘除监听 socket（invalidate 会将其从 RunLoop 移除并关闭底层 fd）
    if (self->_listenSocket) {
        CFSocketInvalidate(self->_listenSocket);
        CFRelease(self->_listenSocket);
        self->_listenSocket = NULL;
    }
    // 关闭全部客户端连接
    for (XPServerConn *conn in [self.connections copy]) {
        [conn close];
    }
    [self.connections removeAllObjects];
    self.lastActiveConn = nil;
}

#pragma mark - accept 处理（网络线程）

- (void)onAcceptNativeHandle:(CFSocketNativeHandle)handle {
    if (self.shouldStopThread || self->_listenSocket == NULL) {
        close(handle); // 已停止监听：直接关闭新到的连接
        return;
    }
    XPServerConn *conn = [[XPServerConn alloc] initWithNativeHandle:handle
                                                      runLoopThread:[NSThread currentThread]
                                                            delegate:self];
    [conn openOnCurrentRunLoop];
    [self.connections addObject:conn];
    XPLog(XPLogLevelInfo, @"conn", @"客户端已接入，当前连接数: %lu", (unsigned long)self.connections.count);
}

#pragma mark - XPServerConnDelegate

- (void)serverConnDidReceiveData:(XPServerConn *)conn {
    // 任意数据到达即视为最近活跃连接（原子属性，跨线程安全）
    self.lastActiveConn = conn;
}

- (void)serverConn:(XPServerConn *)conn didReceiveCommand:(NSString *)command {
    [self.delegate connMgr:self didReceiveCommand:command];
}

- (void)serverConnDidDisconnect:(XPServerConn *)conn {
    [self.connections removeObject:conn];
    if (self.lastActiveConn == conn) {
        // 最近活跃连接断开：回落到最近接入的其余连接
        self.lastActiveConn = self.connections.lastObject;
    }
    XPLog(XPLogLevelInfo, @"conn", @"客户端已断开，当前连接数: %lu", (unsigned long)self.connections.count);
}

#pragma mark - 发送

- (void)sendResult:(NSString *)data {
    XPServerConn *conn = self.lastActiveConn;
    if (conn) {
        [conn sendData:data];
    } else {
        XPLog(XPLogLevelVerbose, @"conn", @"无可用客户端连接，回包丢弃: %@", data);
    }
}

@end
