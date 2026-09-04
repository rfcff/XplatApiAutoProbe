//
//  XPCustomInvocation.h
//  XplatApiAutoProbe —— iOS 统一测试 RPC 库
//
//  自定义调用协议（对应线协议 ver = 2 命令）：
//  业务方实现本协议后，ver = 2 的命令不再走反射，
//  而是把 apiName 与 params 原样透传给业务自行解析并分发。
//

#ifndef XplatApiAutoProbe_XPCustomInvocation_h
#define XplatApiAutoProbe_XPCustomInvocation_h

#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

@protocol XPCustomInvocation <NSObject>

@required

/**
 * ver = 2 命令的执行入口
 *
 * @param apiName 命令帧中的 apiName，原样透传（扁平形式时为 api 字符串本身）
 * @param params  命令帧中的 params（任意 JSON 对象），原样透传；无 params 时为 nil
 */
- (void)callMethod:(NSString *)apiName Params:(nullable NSDictionary *)params;

@end

NS_ASSUME_NONNULL_END

#endif /* XplatApiAutoProbe_XPCustomInvocation_h */
