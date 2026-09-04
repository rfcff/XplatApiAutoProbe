//
//  XPTypeDefine.h
//  XplatApiAutoProbe —— iOS 统一测试 RPC 库
//
//  命令帧 param_type 数组支持的类型名常量。
//  同时兼容 Java 风格（int/String/boolean，扁平形式常用）
//  与 Objective-C 风格（NSInteger/NSString/BOOL，嵌套形式常用）的写法。
//

#ifndef XplatApiAutoProbe_XPTypeDefine_h
#define XplatApiAutoProbe_XPTypeDefine_h

#import <Foundation/Foundation.h>

// ---------- 基础数值类型 ----------
static NSString * const XPTypeInt          = @"int";
static NSString * const XPTypeShort        = @"short";
static NSString * const XPTypeLong         = @"long";
static NSString * const XPTypeLongLong     = @"long long";
static NSString * const XPTypeNSInteger    = @"NSInteger";
static NSString * const XPTypeNSUInteger   = @"NSUInteger";
static NSString * const XPTypeUInt64       = @"UInt64";
static NSString * const XPTypeFloat        = @"float";
static NSString * const XPTypeDouble       = @"double";
static NSString * const XPTypeCGFloat      = @"CGFloat";

// ---------- 布尔类型 ----------
static NSString * const XPTypeBool         = @"bool";
static NSString * const XPTypeBoolean      = @"Boolean";   // Java 风格别名
static NSString * const XPTypeBOOL         = @"BOOL";

// ---------- 字符/指针类型 ----------
static NSString * const XPTypeChar         = @"char";
static NSString * const XPTypeCharPointer  = @"char*";

// ---------- 对象类型 ----------
static NSString * const XPTypeString       = @"String";    // Java 风格别名
static NSString * const XPTypeNSString     = @"NSString";
static NSString * const XPTypeNSObject     = @"NSObject";
static NSString * const XPTypeNSNumber     = @"NSNumber";
static NSString * const XPTypeNSSet        = @"NSSet";

// ---------- 结构体与数组 ----------
static NSString * const XPTypeCGPoint      = @"CGPoint";
static NSString * const XPTypeCGRect       = @"CGRect";
static NSString * const XPTypeFloatArray   = @"float[]";

#endif /* XplatApiAutoProbe_XPTypeDefine_h */
