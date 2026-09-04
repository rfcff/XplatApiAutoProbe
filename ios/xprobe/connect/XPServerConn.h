//
//  XPServerConn.h
//  XplatApiAutoProbe —— iOS 统一测试 RPC 库
//
//  单个客户端连接：基于 CFStream 实现，承载一条 TCP 连接的
//  帧读取（4 字节大端长度头 + UTF-8 payload）、特殊文本帧处理
//  （PING/PONG/GET_API）以及带发送缓冲的帧写入。
//
//  线程模型：
//  - 读缓冲与流事件只在所属 RunLoop 线程（runLoopThread）上处理；
//  - sendData: 可在任意线程调用，发送数据先入队再投递到 RunLoop 线程冲刷。
//

#import <Foundation/Foundation.h>

@class XPServerConn;

NS_ASSUME_NONNULL_BEGIN

/// 连接事件回调（由 XPConnMgr 实现）
@protocol XPServerConnDelegate <NSObject>

/// 收到任意数据（含 PING/PONG 等心跳帧），用于更新“最近活跃连接”
- (void)serverConnDidReceiveData:(XPServerConn *)conn;

/// 收到一条 JSON 命令帧的 payload（非特殊文本帧），交由上层分发执行
- (void)serverConn:(XPServerConn *)conn didReceiveCommand:(NSString *)command;

/// 连接断开（对端关闭 / 流错误 / 主动关闭），在此之后连接不再可用
- (void)serverConnDidDisconnect:(XPServerConn *)conn;

@end

/// 单个客户端连接对象（服务端视角）
@interface XPServerConn : NSObject

/**
 * 创建连接对象
 *
 * @param handle        accept 得到的原生 socket 句柄（构造后由本对象托管）
 * @param runLoopThread 流所在的 RunLoop 线程
 * @param delegate      连接事件回调，弱引用
 */
- (instancetype)initWithNativeHandle:(int)handle
                       runLoopThread:(NSThread *)runLoopThread
                             delegate:(nullable id<XPServerConnDelegate>)delegate NS_DESIGNATED_INITIALIZER;

- (instancetype)init NS_UNAVAILABLE;
+ (instancetype)new NS_UNAVAILABLE;

/// 创建读写流并挂到当前 RunLoop（必须在 runLoopThread 上调用）
- (void)openOnCurrentRunLoop;

/// 关闭连接并释放流资源（必须在 runLoopThread 上调用；重复调用安全）
- (void)close;

/// 发送一帧文本（内部自动加 4 字节大端长度头）；可在任意线程调用
- (void)sendData:(NSString *)data;

/// 连接是否已关闭
@property (nonatomic, readonly, getter=isClosed) BOOL closed;

/// 底层原生 socket 句柄（由本对象托管，外部不要关闭）
@property (nonatomic, readonly) int nativeHandle;

@end

NS_ASSUME_NONNULL_END
