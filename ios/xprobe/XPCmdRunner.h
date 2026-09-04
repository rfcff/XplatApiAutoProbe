//
//  XPCmdRunner.h
//  XplatApiAutoProbe —— iOS 统一测试 RPC 库
//
//  命令分发层：
//  - 解析命令帧 JSON（单对象或对象数组，数组内命令顺序执行）；
//  - 按 threadMode 派发：1 → 主队列 dispatch_async，
//    0/缺省 → 串行后台队列（保证同一帧内命令顺序执行）；
//  - 把单条命令交给 XPReflect 反射执行。
//

#import <Foundation/Foundation.h>

@class XPTestMgr;

NS_ASSUME_NONNULL_BEGIN

/// 命令分发器（由 XPTestMgr 持有）
@interface XPCmdRunner : NSObject

- (instancetype)initWithTestMgr:(XPTestMgr *)testMgr NS_DESIGNATED_INITIALIZER;

- (instancetype)init NS_UNAVAILABLE;
+ (instancetype)new NS_UNAVAILABLE;

/// 处理一帧 JSON 命令文本（来自网络层）
- (void)onCommandFrame:(NSString *)frame;

/// 停止分发：丢弃尚未执行的排队命令
- (void)stop;

@end

NS_ASSUME_NONNULL_END
