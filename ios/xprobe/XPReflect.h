//
//  XPReflect.h
//  XplatApiAutoProbe —— iOS 统一测试 RPC 库
//
//  反射执行层：把单条命令字典归一化（ver=2 自定义调用 /
//  ver=1 扁平与嵌套形式 / field 命令），并通过 NSInvocation
//  完成方法查找、参数类型转换与返回值字符串化。
//

#import <Foundation/Foundation.h>

@class XPTestMgr;

NS_ASSUME_NONNULL_BEGIN

/// 反射执行器（由 XPCmdRunner 持有并调用）
@interface XPReflect : NSObject

- (instancetype)initWithTestMgr:(XPTestMgr *)testMgr NS_DESIGNATED_INITIALIZER;

- (instancetype)init NS_UNAVAILABLE;
+ (instancetype)new NS_UNAVAILABLE;

/// 执行单条命令字典（执行异常在内部捕获并经 sendError 上报）
- (void)executeCommand:(NSDictionary<NSString *, id> *)cmdDict;

/// 枚举指定类的方法列表（实例方法 + 类方法，跳过无参方法），
/// 生成 GET_API 命令的响应：[{"api":"类名.方法名","param_name":[...],"param_type":[...]}]
/// 类名非法或类不存在时返回 "[]"
+ (NSString *)apiListJSONOfClassNamed:(NSString *)className;

@end

NS_ASSUME_NONNULL_END
