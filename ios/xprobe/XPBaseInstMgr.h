//
//  XPBaseInstMgr.h
//  XplatApiAutoProbe —— iOS 统一测试 RPC 库
//
//  实例管理协议：反射调用实例方法前，通过本协议获取被测类的实例；
//  自定义类型的参数也经由本协议构造。
//

#ifndef XplatApiAutoProbe_XPBaseInstMgr_h
#define XplatApiAutoProbe_XPBaseInstMgr_h

#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

@protocol XPBaseInstMgr <NSObject>

@required

/**
 * 获取类名对应的实例（一般是被测 SDK 的核心对象缓存）
 *
 * @param className 完整类名，如 @"com.foo.Engine"
 * @return 类对应的实例；业务未创建过该实例时返回 nil
 */
- (nullable id)getInstOfClass:(NSString *)className;

@optional

/**
 * 判断类名对应的实例是否已初始化（可选实现）。
 * 实现了本方法且返回 NO 时，反射层不会再调用 getInstOfClass:，
 * 用于避免在实例未创建时触发业务侧的惰性创建逻辑。
 */
- (BOOL)isInstInitialize:(NSString *)className;

/**
 * 构造自定义类型参数（可选实现）。
 * 返回值按指针大小透传给被调方法的对应参数位，
 * 仅适用于参数本身为指针/对象引用的自定义类型。
 *
 * @param typeName 参数类型名
 * @param value    参数值（param_value 数组元素）
 */
- (nullable void *)generateCustomType:(NSString *)typeName WithValue:(nullable id)value;

/**
 * 默认类名（对应 PROTOCOL.md §4.2 规则 1 的 ObjectManager.getSDKPackageName()）。
 *
 * api 字段不含 "." 时，本返回值作为类名、整个 api 字符串作为方法名使用，
 * 例：api="joinRoom" + 本方法返回 @"XPFooEngine" → 调用 [XPFooEngine joinRoom]。
 * 未实现本方法或返回空串时，无 "." 的 api 命令按解析错误上报。
 */
- (nullable NSString *)sdkPackageName;

@end

NS_ASSUME_NONNULL_END

#endif /* XplatApiAutoProbe_XPBaseInstMgr_h */
