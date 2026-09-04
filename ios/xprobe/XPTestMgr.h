//
//  XPTestMgr.h
//  XplatApiAutoProbe —— iOS 统一测试 RPC 库
//
//  测试管理单例：对外统一入口，负责启动/停止 RPC 服务端，
//  以及向测试客户端回发 return / error / callback 帧。
//
//  对外 API 与 PROTOCOL.md 第 6 节对齐：
//  startTestWithInstMgr:            —— 仅反射调用（ver = 1 / 缺省）
//  startTestWithCustInvoc:          —— 仅自定义调用（ver = 2）
//  startTestWithInstMgr:CustInvoc:  —— 两者同时启用
//  startTestWithPort:InstMgr:CustInvoc: —— 指定监听端口（port <= 0 时用默认 9000）
//

#import <Foundation/Foundation.h>

#import "XPBaseInstMgr.h"
#import "XPCustomInvocation.h"

NS_ASSUME_NONNULL_BEGIN

/// 测试管理单例
@interface XPTestMgr : NSObject

/// 单例入口
+ (instancetype)sharedInstance;

- (instancetype)init NS_UNAVAILABLE;
+ (instancetype)new NS_UNAVAILABLE;

#pragma mark - 启停（每次启动会完全覆盖上一次的实例管理器与自定义调用配置）

/// 仅注册实例管理器（反射调用），使用默认端口启动服务
- (void)startTestWithInstMgr:(nullable id<XPBaseInstMgr>)mgr;

/// 仅注册自定义调用（ver = 2），使用默认端口启动服务
- (void)startTestWithCustInvoc:(nullable id<XPCustomInvocation>)inv;

/// 同时注册实例管理器与自定义调用，使用默认端口启动服务
- (void)startTestWithInstMgr:(nullable id<XPBaseInstMgr>)mgr
                    CustInvoc:(nullable id<XPCustomInvocation>)inv;

/// 指定端口启动服务（port <= 0 时使用默认端口 9000；mgr/inv 可为 nil）
- (void)startTestWithPort:(NSInteger)port
                  InstMgr:(nullable id<XPBaseInstMgr>)mgr
                 CustInvoc:(nullable id<XPCustomInvocation>)inv;

/// 停止服务：关闭监听与全部客户端连接，丢弃排队命令
- (void)stopTest;

#pragma mark - 回包（发往最近活跃连接）

/// 方法执行结果回包（type = return）
- (void)sendReturn:(NSString *)key value:(nullable NSString *)value;

/// 错误回包（type = error；解析错误 key 固定为 parseError）
- (void)sendError:(NSString *)key msg:(nullable NSString *)msg;

/// 异步回调文本回包（type = callback）
- (void)sendCallback:(NSString *)name info:(nullable NSString *)info;

/// 异步回调 JSON 对象回包（type = callback；字典序列化为 JSON 字符串作为 value）
/// 与 PROTOCOL.md §6 的 sendCallbackJson 等价，本名为协议统一命名，推荐新代码使用
- (void)sendCallbackJson:(NSString *)name json:(nullable NSDictionary<NSString *, id> *)json;

/// 异步回调 JSON 对象回包（等价命名，与 sendCallbackJson:json: 行为一致）
- (void)sendCallback:(NSString *)name infoDict:(nullable NSDictionary<NSString *, id> *)info;

#pragma mark - 配置（由各 start 方法设置）

/// 实例管理器（反射调用实例方法时使用）
@property (nonatomic, strong, nullable) id<XPBaseInstMgr> instMgr;

/// 自定义调用（ver = 2 命令透传目标）
@property (nonatomic, strong, nullable) id<XPCustomInvocation> custInvoc;

/// 服务端默认监听端口（9000）
@property (nonatomic, class, readonly) NSInteger defaultListenPort;

@end

NS_ASSUME_NONNULL_END
