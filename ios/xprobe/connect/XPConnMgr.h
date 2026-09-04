//
//  XPConnMgr.h
//  XplatApiAutoProbe —— iOS 统一测试 RPC 库
//
//  连接管理器：CFSocket + 专用 RunLoop 线程实现 TCP 服务端。
//  - 监听 0.0.0.0:port（默认 9000），可接受多个客户端连接；
//  - 维护连接表与“最近活跃连接”，sendResult 默认发往最近活跃连接；
//  - 网络线程生命周期随 openConn/closeConn 管理。
//

#import <Foundation/Foundation.h>

#import "XPServerConn.h"

@class XPConnMgr;

NS_ASSUME_NONNULL_BEGIN

/// 连接管理器事件回调（由 XPTestMgr 实现）
@protocol XPConnMgrDelegate <NSObject>

/// 收到一条 JSON 命令帧 payload，交由上层解析分发
- (void)connMgr:(XPConnMgr *)mgr didReceiveCommand:(NSString *)command;

@end

/// TCP 服务端连接管理器
@interface XPConnMgr : NSObject

- (instancetype)initWithDelegate:(nullable id<XPConnMgrDelegate>)delegate NS_DESIGNATED_INITIALIZER;

- (instancetype)init NS_UNAVAILABLE;
+ (instancetype)new NS_UNAVAILABLE;

/// 启动监听（port <= 0 时使用默认端口 9000）；重复调用会先停止旧监听
- (void)openConn:(NSInteger)port;

/// 停止监听并关闭全部客户端连接
- (void)closeConn;

/// 向最近活跃的客户端连接发送一帧文本；无可用连接时静默丢弃
- (void)sendResult:(NSString *)data;

/// 服务端默认监听端口
@property (nonatomic, class, readonly) NSInteger defaultListenPort;

@end

NS_ASSUME_NONNULL_END
