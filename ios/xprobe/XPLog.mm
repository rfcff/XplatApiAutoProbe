//
//  XPLog.mm
//  XplatApiAutoProbe —— iOS 统一测试 RPC 库
//
//  统一日志实现：
//  - 全局 handler（拷贝持有）+ NSLock 保护读写，任意线程可并发打日志；
//  - 调用回调前先把 block 读出到局部强引用再解锁，
//    既避免持锁执行业务代码，也支持在回调内再次调用 XPLog；
//  - 未注册回调时回退 NSLog：[XplatApiAutoProbe][LEVEL][tag] message。
//

#import "XPLog.h"

// 全局日志回调（跨线程读写，须持锁访问）
static XPLogHandler _Nullable gXPLogHandler = nil;

// 回调读写锁（dispatch_once 惰性初始化，线程安全）
static NSLock *XPLogLock(void) {
    static NSLock *lock = nil;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        lock = [[NSLock alloc] init];
    });
    return lock;
}

// 级别对应的名称（默认 NSLog 输出使用）
static NSString *XPLogLevelName(XPLogLevel level) {
    switch (level) {
        case XPLogLevelVerbose: return @"Verbose";
        case XPLogLevelDebug:   return @"Debug";
        case XPLogLevelInfo:    return @"Info";
        case XPLogLevelWarn:    return @"Warn";
        case XPLogLevelError:   return @"Error";
    }
    return @"Unknown"; // 非法级别兜底
}

void XPLogSetHandler(XPLogHandler handler) {
    // 显式 copy：把栈 block 拷贝到堆并交由全局变量持有
    XPLogHandler copied = [handler copy];
    [XPLogLock() lock];
    gXPLogHandler = copied;
    [XPLogLock() unlock];
}

void XPLog(XPLogLevel level, NSString *tag, NSString *format, ...) {
    // 先完成变参格式化
    va_list args;
    va_start(args, format);
    NSString *message = [[NSString alloc] initWithFormat:format arguments:args];
    va_end(args);

    // 持锁读出回调（ARC 下局部变量即强引用），解锁后再调用，
    // 保证回调执行期间不持锁（回调内再打日志也不会死锁）
    [XPLogLock() lock];
    XPLogHandler handler = gXPLogHandler;
    [XPLogLock() unlock];

    NSString *safeTag = tag ?: @"";
    if (handler) {
        handler(level, safeTag, message ?: @"");
    } else {
        NSLog(@"[XplatApiAutoProbe][%@][%@] %@", XPLogLevelName(level), safeTag, message);
    }
}
