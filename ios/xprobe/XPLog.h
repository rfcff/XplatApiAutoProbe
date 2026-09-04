//
//  XPLog.h
//  XplatApiAutoProbe —— iOS 统一测试 RPC 库
//
//  统一日志接口：SDK 内部全部日志经 XPLog() 输出。
//  业务与测试用例可通过 XPLogSetHandler() 注册回调接管日志
//  （自行写文件、上报日志平台、转发给测试框架等）；
//  未注册回调时默认走 NSLog，输出格式：
//    [XplatApiAutoProbe][LEVEL][tag] message
//
//  线程安全：网络线程、后台命令队列、主队列都会打日志，
//  回调的注册与读取均有锁保护，回调 block 会被拷贝持有。
//

#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

/// 日志级别（与其他端对齐）
typedef NS_ENUM(NSUInteger, XPLogLevel) {
    XPLogLevelVerbose = 0,   // 最详细的过程日志（如每条回包内容）
    XPLogLevelDebug   = 1,   // 调试日志（如命令派发明细）
    XPLogLevelInfo    = 2,   // 关键路径状态日志（服务启停、连接增减）
    XPLogLevelWarn    = 3,   // 可恢复异常（如命令解析失败）
    XPLogLevelError   = 4,   // 错误（如监听创建失败、命令执行异常）
};

/// 日志回调：level 级别、tag 模块名（如 @"conn"/@"cmd"/@"reflect"）、message 已完成格式化的日志文本
typedef void (^XPLogHandler)(XPLogLevel level, NSString *tag, NSString *message);

/// 注册日志回调；传 nil 恢复默认 NSLog 输出。回调会被拷贝持有，可在任意线程重复调用
FOUNDATION_EXPORT void XPLogSetHandler(XPLogHandler _Nullable handler);

/// 输出一条内部日志：已注册回调时转发给回调，否则默认 NSLog 输出
/// [XplatApiAutoProbe][LEVEL][tag] message
FOUNDATION_EXPORT void XPLog(XPLogLevel level, NSString *tag, NSString *format, ...) NS_FORMAT_FUNCTION(3, 4);

NS_ASSUME_NONNULL_END
