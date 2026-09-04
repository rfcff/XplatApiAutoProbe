//
//  XPReflect.mm
//  XplatApiAutoProbe —— iOS 统一测试 RPC 库
//
//  反射执行实现：
//  - ver = 2：apiName / params 透传给 XPCustomInvocation；
//  - ver = 1 / 缺省：同时支持扁平形式（param_name/param_type/param_value）
//    与嵌套形式（api.apiName/paramName/paramType/paramValue）；
//    先尝试类方法，再经 XPBaseInstMgr 获取实例调实例方法；
//  - field 命令：按 set<属性名首字母大写>: 规则反射 setter 修改属性；
//  - 参数类型转换：int/long/float/double/BOOL/NSString/NSNumber/NSInteger 等；
//  - GET_API：class_copyMethodList 枚举方法生成 JSON 数组。
//

#import "XPReflect.h"
#import "XPBaseInstMgr.h"
#import "XPCustomInvocation.h"
#import "XPTestMgr.h"
#import "XPTypeDefine.h"
#import "XPLog.h"

#import <objc/runtime.h>
#import <CoreGraphics/CoreGraphics.h>

// ---------- 命令帧 JSON 字段名 ----------
static NSString * const kXPKeyApi            = @"api";
static NSString * const kXPKeyField          = @"field";
static NSString * const kXPKeyVer            = @"ver";
static NSString * const kXPKeyApiName        = @"apiName";
static NSString * const kXPKeyParams         = @"params";
// 嵌套形式（camelCase）
static NSString * const kXPKeyParamName      = @"paramName";
static NSString * const kXPKeyParamType      = @"paramType";
static NSString * const kXPKeyParamValue     = @"paramValue";
// 扁平形式（snake_case）
static NSString * const kXPKeyParamNameFlat  = @"param_name";
static NSString * const kXPKeyParamTypeFlat  = @"param_type";
static NSString * const kXPKeyParamValueFlat = @"param_value";

#pragma mark - 几何字符串解析（不依赖 UIKit，跨 macOS / iOS 可编译）

// 解析 "{x, y}" / "{x, y, w, h}" 风格字符串，拆出数字文本
static NSArray<NSString *> *XPGeometryComponents(NSString *string) {
    if (![string isKindOfClass:[NSString class]]) {
        return @[];
    }
    NSString *clean = [[string stringByReplacingOccurrencesOfString:@"{" withString:@""]
                       stringByReplacingOccurrencesOfString:@"}" withString:@""];
    NSMutableArray<NSString *> *numbers = [NSMutableArray array];
    for (NSString *part in [clean componentsSeparatedByString:@","]) {
        NSString *trimmed = [part stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceCharacterSet]];
        if (trimmed.length > 0) {
            [numbers addObject:trimmed];
        }
    }
    return numbers;
}

static CGPoint XPPointFromString(NSString *string) {
    CGPoint point = CGPointZero;
    NSArray<NSString *> *components = XPGeometryComponents(string);
    if (components.count >= 2) {
        point.x = [components[0] doubleValue];
        point.y = [components[1] doubleValue];
    }
    return point;
}

static CGRect XPRectFromString(NSString *string) {
    CGRect rect = CGRectZero;
    NSArray<NSString *> *components = XPGeometryComponents(string);
    if (components.count >= 4) {
        rect = CGRectMake([components[0] doubleValue], [components[1] doubleValue],
                          [components[2] doubleValue], [components[3] doubleValue]);
    }
    return rect;
}

#pragma mark - XPReflect 实现

@interface XPReflect ()
// 回指单例管理器（弱引用，避免循环持有）
@property (nonatomic, weak, nullable) XPTestMgr *testMgr;
@end

@implementation XPReflect

- (instancetype)initWithTestMgr:(XPTestMgr *)testMgr {
    self = [super init];
    if (self) {
        _testMgr = testMgr;
    }
    return self;
}

#pragma mark - 命令入口

- (void)executeCommand:(NSDictionary<NSString *, id> *)cmdDict {
    @try {
        id api = cmdDict[kXPKeyApi];

        // ver = 2（整数 2 或字符串 "2"）：自定义调用，apiName 与 params 透传
        if ([XPReflect isVer2:cmdDict[kXPKeyVer]]) {
            [self executeCustomInvocation:api];
            return;
        }

        // 归一化：扁平形式 api 为字符串；嵌套形式 api 为字典
        NSDictionary *apiDict = [api isKindOfClass:[NSDictionary class]] ? api : nil;

        NSString *apiName = nil;
        if ([api isKindOfClass:[NSString class]]) {
            // 扁平形式：api 即 "类名.方法名"
            apiName = api;
        } else if (apiDict) {
            // 嵌套形式：apiName 在 api 字典内
            if ([apiDict[kXPKeyApiName] isKindOfClass:[NSString class]]) {
                apiName = apiDict[kXPKeyApiName];
            }
        }

        // field 命令：优先取顶层，缺失时回退到 api 字典内
        NSString *field = [cmdDict[kXPKeyField] isKindOfClass:[NSString class]] ? cmdDict[kXPKeyField] : nil;
        if (field.length == 0 && apiDict) {
            field = [apiDict[kXPKeyField] isKindOfClass:[NSString class]] ? apiDict[kXPKeyField] : nil;
        }

        // 参数三数组：嵌套形式（camel）> 扁平形式（snake）> 兼容互查
        NSArray *paramNames = [self arrayValueForKey:kXPKeyParamName
                                             flatKey:kXPKeyParamNameFlat
                                              cmdDict:cmdDict
                                               apiDict:apiDict];
        NSArray *paramTypes = [self arrayValueForKey:kXPKeyParamType
                                             flatKey:kXPKeyParamTypeFlat
                                              cmdDict:cmdDict
                                               apiDict:apiDict];
        NSArray *paramValues = [self arrayValueForKey:kXPKeyParamValue
                                              flatKey:kXPKeyParamValueFlat
                                               cmdDict:cmdDict
                                                apiDict:apiDict];

        if (apiName.length > 0) {
            // 反射方法调用
            [self invokeApiNamed:apiName
                       paramNames:paramNames
                       paramTypes:paramTypes
                      paramValues:paramValues];
        } else if (field.length > 0) {
            // field 命令：反射 setter 修改属性
            [self setFieldNamed:field
                       paramNames:paramNames
                       paramTypes:paramTypes
                      paramValues:paramValues];
        } else {
            [NSException raise:NSInvalidArgumentException
                        format:@"命令中缺少 api 或 field 字段: %@", cmdDict];
        }
    } @catch (NSException *exception) {
        // 执行异常上报（key 为异常名，value 为描述）
        XPLog(XPLogLevelError, @"reflect", @"命令执行异常: %@ — %@", exception.name, exception.reason);
        [self.testMgr sendError:exception.name msg:exception.reason];
    }
}

#pragma mark - ver = 2 自定义调用

- (void)executeCustomInvocation:(id)api {
    NSString *apiName = nil;
    NSDictionary *params = nil;
    if ([api isKindOfClass:[NSDictionary class]]) {
        if ([api[kXPKeyApiName] isKindOfClass:[NSString class]]) {
            apiName = api[kXPKeyApiName];
        }
        if ([api[kXPKeyParams] isKindOfClass:[NSDictionary class]]) {
            params = api[kXPKeyParams];
        }
    } else if ([api isKindOfClass:[NSString class]]) {
        // 扁平形式 ver = 2：api 字符串直接作为 apiName
        apiName = api;
    }
    if (apiName == nil) {
        apiName = @"";
    }

    id<XPCustomInvocation> custInvoc = self.testMgr.custInvoc;
    if (custInvoc) {
        // apiName 与 params 原样透传，由业务自行解析分发
        XPLog(XPLogLevelDebug, @"reflect", @"ver=2 透传自定义调用 apiName=%@ params=%@",
              apiName, params ?: @{});
        [custInvoc callMethod:apiName Params:params];
    } else {
        XPLog(XPLogLevelError, @"reflect", @"未注册 XPCustomInvocation，无法执行 ver=2 命令: %@",
              apiName);
        [self.testMgr sendError:(apiName.length > 0 ? apiName : @"parseError")
                            msg:@"未注册 XPCustomInvocation，无法执行 ver=2 命令"];
    }
}

#pragma mark - ver = 1 / 缺省：反射方法调用

/**
 * api 不含 "." 时的兜底类名：取自 XPBaseInstMgr 的可选方法 sdkPackageName
 * （对应 Android 的 ObjectManager.getSDKPackageName()，见 PROTOCOL.md §4.2 规则 1）
 */
- (NSString *)defaultClassName {
    id<XPBaseInstMgr> mgr = self.testMgr.instMgr;
    if (![mgr respondsToSelector:@selector(sdkPackageName)]) {
        return nil;
    }
    NSString *name = [mgr sdkPackageName];
    return [name isKindOfClass:[NSString class]] ? name : nil;
}

- (void)invokeApiNamed:(NSString *)apiName
            paramNames:(NSArray *)paramNames
            paramTypes:(NSArray *)paramTypes
           paramValues:(NSArray *)paramValues {
    // api 取最后一个 "." 前为类名、其后为方法名（选择子）；
    // 不含 "." 时按 PROTOCOL.md §4.2 规则 1 取 XPBaseInstMgr 提供的默认类名
    NSRange lastDot = [apiName rangeOfString:@"." options:NSBackwardsSearch];
    BOOL hasClassName = lastDot.location != NSNotFound && lastDot.location > 0 &&
                        lastDot.location < apiName.length - 1;

    NSString *className = nil;
    NSString *selectorName = nil;
    if (hasClassName) {
        className = [apiName substringToIndex:lastDot.location];
        selectorName = [apiName substringFromIndex:lastDot.location + 1];
    } else {
        // 无类名：整个 api 作为方法名，类名用默认类名兜底
        className = [self defaultClassName];
        selectorName = apiName;
        if (className.length == 0) {
            [NSException raise:NSInvalidArgumentException
                        format:@"api 缺少类名且未配置默认类名（应形如 类名.方法名，"
                                "或由 XPBaseInstMgr 实现 sdkPackageName）: %@", apiName];
        }
    }
    XPLog(XPLogLevelVerbose, @"reflect", @"反射调用 %@（类 %@，方法 %@）types=%@ values=%@",
          apiName, className, selectorName, paramTypes, paramValues);

    // 类型/值数组长度必须一致；param_name 若提供也必须一致
    if (paramTypes.count != paramValues.count) {
        [NSException raise:NSInvalidArgumentException
                    format:@"param_type(%lu) 与 param_value(%lu) 数组长度不一致",
                            (unsigned long)paramTypes.count, (unsigned long)paramValues.count];
    }
    if (paramNames != nil && paramNames.count != paramValues.count) {
        [NSException raise:NSInvalidArgumentException
                    format:@"param_name(%lu) 与 param_value(%lu) 数组长度不一致",
                            (unsigned long)paramNames.count, (unsigned long)paramValues.count];
    }

    NSString *result = [self invokeSelectorNamed:selectorName
                                    ofClassNamed:className
                                          values:paramValues
                                           types:paramTypes];
    [self.testMgr sendReturn:apiName value:result];
}

- (void)setFieldNamed:(NSString *)field
            paramNames:(NSArray *)paramNames
            paramTypes:(NSArray *)paramTypes
           paramValues:(NSArray *)paramValues {
    XPLog(XPLogLevelVerbose, @"reflect", @"field 命令 %@ names=%@ types=%@ values=%@",
          field, paramNames, paramTypes, paramValues);
    // field 命令依赖 param_name 提供属性名，三数组长度必须一致
    if (paramNames.count == 0) {
        [NSException raise:NSInvalidArgumentException format:@"field 命令缺少 param_name"];
    }
    if (paramTypes.count != paramNames.count || paramValues.count != paramNames.count) {
        [NSException raise:NSInvalidArgumentException
                    format:@"field 命令 param_name(%lu)/param_type(%lu)/param_value(%lu) 数组长度不一致",
                            (unsigned long)paramNames.count, (unsigned long)paramTypes.count,
                            (unsigned long)paramValues.count];
    }

    for (NSUInteger i = 0; i < paramNames.count; i++) {
        NSString *propertyName = [paramNames[i] isKindOfClass:[NSString class]] ? paramNames[i] : nil;
        if (propertyName.length == 0) {
            [NSException raise:NSInvalidArgumentException
                        format:@"field 命令存在非法属性名: %@", paramNames[i]];
        }
        // 属性名按 set<属性名首字母大写>: 规则组装 setter
        NSString *setterName = [XPReflect setterNameOfPropertyName:propertyName];
        NSArray *singleValue = @[(paramValues[i] ?: [NSNull null])];
        NSArray *singleType = @[(paramTypes[i] ?: XPTypeNSString)];
        // setter 可能是类方法，也可能是实例方法（实例经 XPBaseInstMgr 获取），统一走方法调用流程
        NSString *result = [self invokeSelectorNamed:setterName
                                        ofClassNamed:field
                                              values:singleValue
                                               types:singleType];
        [self.testMgr sendReturn:setterName value:result];
    }
}

/**
 * 核心反射：构造 NSInvocation 并执行，返回值转为字符串。
 * 查找顺序：先尝试类方法（元类），再经 XPBaseInstMgr 获取实例调实例方法。
 */
- (NSString *)invokeSelectorNamed:(NSString *)selectorName
                     ofClassNamed:(NSString *)className
                           values:(NSArray<id> *)values
                            types:(NSArray<NSString *> *)types {
    Class targetClass = NSClassFromString(className);
    if (!targetClass) {
        [NSException raise:NSObjectNotAvailableException format:@"类不存在: %@", className];
    }
    SEL selector = NSSelectorFromString(selectorName);

    NSInvocation *invocation = nil;
    id target = nil;

    // 1. 先尝试类方法（class_getClassMethod 会沿继承链查找）
    Method classMethod = class_getClassMethod(targetClass, selector);
    if (classMethod) {
        NSMethodSignature *signature =
            [NSMethodSignature signatureWithObjCTypes:method_getTypeEncoding(classMethod)];
        if (signature) {
            invocation = [NSInvocation invocationWithMethodSignature:signature];
            target = targetClass; // 类方法的 target 是 Class 对象本身
        }
    }

    // 2. 类方法未命中：经 XPBaseInstMgr 获取实例，尝试实例方法
    if (!invocation) {
        BOOL classHasInstMethod = class_getInstanceMethod(targetClass, selector) != NULL;
        id instance = [self instanceOfClassNamed:className];
        if (!instance) {
            if (classHasInstMethod) {
                [NSException raise:NSObjectNotAvailableException
                            format:@"调用实例方法 %@ 时无法找到 %@ 的实例", selectorName, className];
            }
            [NSException raise:NSInvalidArgumentException
                        format:@"%@ 无法作为 %@ 的类方法或实例方法调用", selectorName, className];
        }
        Method instMethod = class_getInstanceMethod(object_getClass(instance), selector);
        if (!instMethod) {
            [NSException raise:NSInvalidArgumentException
                        format:@"%@ 无法作为 %@ 的类方法或实例方法调用", selectorName, className];
        }
        NSMethodSignature *signature =
            [NSMethodSignature signatureWithObjCTypes:method_getTypeEncoding(instMethod)];
        if (!signature) {
            [NSException raise:NSInternalInconsistencyException
                        format:@"无法获取方法 %@ 的签名", selectorName];
        }
        invocation = [NSInvocation invocationWithMethodSignature:signature];
        target = instance;
    }

    // 参数个数校验（索引 0/1 为 self/_cmd）
    NSUInteger expectedCount = invocation.methodSignature.numberOfArguments - 2;
    if (expectedCount != values.count) {
        [NSException raise:NSInvalidArgumentException
                    format:@"%@ 期望 %lu 个参数，实际提供 %lu 个",
                            selectorName, (unsigned long)expectedCount, (unsigned long)values.count];
    }

    [invocation setTarget:target];
    [invocation setSelector:selector];

    // 设置参数（含类型转换）；scratch 收集需要存活到 invoke 结束的临时缓冲
    NSMutableArray<NSValue *> *scratch = [NSMutableArray array];
    [self setArgumentsForInvocation:invocation values:values types:types scratch:scratch];

    @try {
        [invocation invoke];
    } @finally {
        // 释放临时参数缓冲（float[] 等 malloc 内存；invoke 已完成，安全释放）
        for (NSValue *value in scratch) {
            void *buffer = NULL;
            [value getValue:&buffer];
            free(buffer);
        }
    }

    return [XPReflect stringFromReturnValueOfInvocation:invocation];
}

// 经 XPBaseInstMgr 获取类实例（先过 isInstInitialize: 守卫，未初始化则不获取）
- (id)instanceOfClassNamed:(NSString *)className {
    id<XPBaseInstMgr> instMgr = self.testMgr.instMgr;
    if (!instMgr) {
        return nil;
    }
    if ([instMgr respondsToSelector:@selector(isInstInitialize:)] &&
        ![instMgr isInstInitialize:className]) {
        // 业务侧声明实例未初始化，不触发实例获取
        return nil;
    }
    return [instMgr getInstOfClass:className];
}

#pragma mark - 参数类型转换

- (void)setArgumentsForInvocation:(NSInvocation *)invocation
                           values:(NSArray<id> *)values
                            types:(NSArray<NSString *> *)types
                           scratch:(NSMutableArray<NSValue *> *)scratch {
    for (NSUInteger i = 0; i < values.count; i++) {
        NSString *type = [types[i] isKindOfClass:[NSString class]] ? types[i] : XPTypeNSString;
        id value = values[i];
        NSUInteger index = i + 2; // 索引 0/1 为 self/_cmd

        if ([type isEqualToString:XPTypeInt]) {
            int argument = [XPReflect numericStringFromValue:value].intValue;
            [invocation setArgument:&argument atIndex:index];
        } else if ([type isEqualToString:XPTypeShort]) {
            short argument = (short)[XPReflect numericStringFromValue:value].integerValue;
            [invocation setArgument:&argument atIndex:index];
        } else if ([type isEqualToString:XPTypeLong]) {
            long argument = (long)[XPReflect numericStringFromValue:value].integerValue;
            [invocation setArgument:&argument atIndex:index];
        } else if ([type isEqualToString:XPTypeLongLong] || [type isEqualToString:XPTypeUInt64]) {
            long long argument = [XPReflect numericStringFromValue:value].longLongValue;
            [invocation setArgument:&argument atIndex:index];
        } else if ([type isEqualToString:XPTypeNSInteger]) {
            NSInteger argument = [XPReflect numericStringFromValue:value].integerValue;
            [invocation setArgument:&argument atIndex:index];
        } else if ([type isEqualToString:XPTypeNSUInteger]) {
            NSUInteger argument = (NSUInteger)[XPReflect numericStringFromValue:value].integerValue;
            [invocation setArgument:&argument atIndex:index];
        } else if ([type isEqualToString:XPTypeFloat]) {
            float argument = [XPReflect numericStringFromValue:value].floatValue;
            [invocation setArgument:&argument atIndex:index];
        } else if ([type isEqualToString:XPTypeDouble] || [type isEqualToString:XPTypeCGFloat]) {
            // CGFloat 在 64 位平台为 double；32 位平台由编译期分支按 float 处理
#if CGFLOAT_IS_DOUBLE
            double argument = [XPReflect numericStringFromValue:value].doubleValue;
            [invocation setArgument:&argument atIndex:index];
#else
            float argument = [XPReflect numericStringFromValue:value].floatValue;
            [invocation setArgument:&argument atIndex:index];
#endif
        } else if ([type isEqualToString:XPTypeBool] || [type isEqualToString:XPTypeBoolean] ||
                   [type isEqualToString:XPTypeBOOL]) {
            // 兼容 "true"/"false" 与 1/0 两种布尔写法
            BOOL argument = [XPReflect numericStringFromValue:value].boolValue;
            [invocation setArgument:&argument atIndex:index];
        } else if ([type isEqualToString:XPTypeChar]) {
            char argument = (char)[XPReflect numericStringFromValue:value].intValue;
            [invocation setArgument:&argument atIndex:index];
        } else if ([type isEqualToString:XPTypeCharPointer]) {
            // C 字符串：临时字符串由 autorelease pool 保活，invoke 立即执行因此安全
            const char *argument = [[XPReflect stringFromValue:value] UTF8String];
            [invocation setArgument:&argument atIndex:index];
        } else if ([type isEqualToString:XPTypeNSString] || [type isEqualToString:XPTypeString] ||
                   [type isEqualToString:XPTypeNSObject] || [type isEqualToString:XPTypeNSNumber]) {
            // 对象类型参数：JSON null 转 nil；字符串参数收到 NSNumber 时先转字符串
            id argument = value;
            if ([argument isKindOfClass:[NSNull class]]) {
                argument = nil;
            } else if (([type isEqualToString:XPTypeNSString] || [type isEqualToString:XPTypeString]) &&
                       [argument isKindOfClass:[NSNumber class]]) {
                argument = [(NSNumber *)argument stringValue];
            }
            [invocation setArgument:&argument atIndex:index];
        } else if ([type isEqualToString:XPTypeNSSet]) {
            NSSet *argument = [value isKindOfClass:[NSArray class]] ? [NSSet setWithArray:value] : [NSSet set];
            [invocation setArgument:&argument atIndex:index];
        } else if ([type isEqualToString:XPTypeCGPoint]) {
            CGPoint argument = XPPointFromString([XPReflect stringFromValue:value]);
            [invocation setArgument:&argument atIndex:index];
        } else if ([type isEqualToString:XPTypeCGRect]) {
            CGRect argument = XPRectFromString([XPReflect stringFromValue:value]);
            [invocation setArgument:&argument atIndex:index];
        } else if ([type isEqualToString:XPTypeFloatArray]) {
            // 支持 JSON 数组 [1,2,3] 与逗号分隔字符串 "1,2,3"
            NSArray *items = nil;
            if ([value isKindOfClass:[NSArray class]]) {
                items = value;
            } else if (value && ![value isKindOfClass:[NSNull class]]) {
                items = [[XPReflect stringFromValue:value] componentsSeparatedByString:@","];
            }
            NSUInteger count = items.count;
            float *buffer = (float *)malloc((count > 0 ? count : 1) * sizeof(float));
            for (NSUInteger k = 0; k < count; k++) {
                buffer[k] = [XPReflect numericStringFromValue:items[k]].floatValue;
            }
            [invocation setArgument:buffer atIndex:index];
            [scratch addObject:[NSValue valueWithPointer:buffer]]; // invoke 后统一释放
        } else {
            // 自定义类型：交由 XPBaseInstMgr 构造，按指针透传
            id<XPBaseInstMgr> instMgr = self.testMgr.instMgr;
            if (!instMgr || ![instMgr respondsToSelector:@selector(generateCustomType:WithValue:)]) {
                [NSException raise:NSInvalidArgumentException
                            format:@"不支持的参数类型: %@（需注册实现 generateCustomType:WithValue: 的 XPBaseInstMgr）", type];
            }
            void *argument = [instMgr generateCustomType:type WithValue:value];
            [invocation setArgument:&argument atIndex:index];
        }
    }
}

#pragma mark - 返回值字符串化

// 把 NSInvocation 的返回值按类型转为字符串：void → "void"，nil → "null"，BOOL → "true"/"false"
+ (NSString *)stringFromReturnValueOfInvocation:(NSInvocation *)invocation {
    const char *returnType = invocation.methodSignature.methodReturnType;
    if (!returnType) {
        return @"void";
    }

    if (strcmp(returnType, @encode(void)) == 0) {
        return @"void";
    }
    if (strcmp(returnType, @encode(BOOL)) == 0) {
        BOOL value = NO;
        [invocation getReturnValue:&value];
        return value ? @"true" : @"false";
    }
    if (strcmp(returnType, @encode(char)) == 0) {
        char value = 0;
        [invocation getReturnValue:&value];
        return [NSString stringWithFormat:@"%c", value];
    }
    if (strcmp(returnType, @encode(int)) == 0) {
        int value = 0;
        [invocation getReturnValue:&value];
        return [NSString stringWithFormat:@"%d", value];
    }
    if (strcmp(returnType, @encode(short)) == 0) {
        short value = 0;
        [invocation getReturnValue:&value];
        return [NSString stringWithFormat:@"%hd", value];
    }
    if (strcmp(returnType, @encode(long)) == 0) {
        long value = 0;
        [invocation getReturnValue:&value];
        return [NSString stringWithFormat:@"%ld", value];
    }
    if (strcmp(returnType, @encode(long long)) == 0) {
        long long value = 0;
        [invocation getReturnValue:&value];
        return [NSString stringWithFormat:@"%lld", value];
    }
    if (strcmp(returnType, @encode(unsigned char)) == 0) {
        unsigned char value = 0;
        [invocation getReturnValue:&value];
        return [NSString stringWithFormat:@"%u", (unsigned int)value];
    }
    if (strcmp(returnType, @encode(unsigned int)) == 0) {
        unsigned int value = 0;
        [invocation getReturnValue:&value];
        return [NSString stringWithFormat:@"%u", value];
    }
    if (strcmp(returnType, @encode(unsigned short)) == 0) {
        unsigned short value = 0;
        [invocation getReturnValue:&value];
        return [NSString stringWithFormat:@"%hu", value];
    }
    if (strcmp(returnType, @encode(unsigned long)) == 0) {
        unsigned long value = 0;
        [invocation getReturnValue:&value];
        return [NSString stringWithFormat:@"%lu", value];
    }
    if (strcmp(returnType, @encode(unsigned long long)) == 0) {
        unsigned long long value = 0;
        [invocation getReturnValue:&value];
        return [NSString stringWithFormat:@"%llu", value];
    }
    if (strcmp(returnType, @encode(float)) == 0) {
        float value = 0.0f;
        [invocation getReturnValue:&value];
        return [NSString stringWithFormat:@"%f", value];
    }
    if (strcmp(returnType, @encode(double)) == 0) {
        double value = 0.0;
        [invocation getReturnValue:&value];
        return [NSString stringWithFormat:@"%f", value];
    }
    if (strcmp(returnType, @encode(char *)) == 0) {
        char *value = NULL;
        [invocation getReturnValue:&value];
        return (value != NULL) ? [NSString stringWithFormat:@"%s", value] : @"null";
    }
    if (strcmp(returnType, @encode(id)) == 0) {
        // 对象返回值：先读入 __unsafe_unretained 槽位再立即转强引用（ARC 下安全）
        __unsafe_unretained id value = nil;
        [invocation getReturnValue:&value];
        id strongValue = value;
        return (strongValue != nil) ? [NSString stringWithFormat:@"%@", strongValue] : @"null";
    }
    // 其余类型（结构体等）：返回类型编码描述
    return [NSString stringWithFormat:@"(unsupported return type: %s)", returnType];
}

#pragma mark - GET_API 方法枚举

+ (NSString *)apiListJSONOfClassNamed:(NSString *)className {
    NSMutableArray<NSDictionary *> *apiList = [NSMutableArray array];
    Class targetClass = (className.length > 0) ? NSClassFromString(className) : Nil;
    if (targetClass) {
        // 实例方法（类自身声明的方法）
        [XPReflect appendMethodListOfMethodOwner:targetClass className:className intoArray:apiList];
        // 类方法（元类上声明的方法）
        [XPReflect appendMethodListOfMethodOwner:object_getClass(targetClass)
                                        className:className
                                        intoArray:apiList];
    }
    NSData *data = [NSJSONSerialization dataWithJSONObject:apiList options:0 error:nil];
    return (data != nil) ? ([[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding] ?: @"[]") : @"[]";
}

// 枚举 methodOwner（类或元类）上声明的方法，拼装为协议要求的条目
+ (void)appendMethodListOfMethodOwner:(Class)methodOwner
                            className:(NSString *)className
                            intoArray:(NSMutableArray<NSDictionary *> *)apiList {
    unsigned int methodCount = 0;
    Method *methods = class_copyMethodList(methodOwner, &methodCount);
    if (!methods) {
        return;
    }
    for (unsigned int i = 0; i < methodCount; i++) {
        Method method = methods[i];
        unsigned int argumentCount = method_getNumberOfArguments(method);
        if (argumentCount < 3) {
            continue; // 去掉 self/_cmd 后无参数的方法不列举（对齐其他平台行为）
        }
        NSString *selectorName = NSStringFromSelector(method_getName(method));

        // 参数名取选择子片段：joinRoom:roomName:uid: → ["joinRoom","roomName","uid"]，缺省补 argN
        NSArray<NSString *> *selectorParts = [selectorName componentsSeparatedByString:@":"];
        NSMutableArray<NSString *> *paramNames = [NSMutableArray arrayWithCapacity:argumentCount - 2];
        NSMutableArray<NSString *> *paramTypes = [NSMutableArray arrayWithCapacity:argumentCount - 2];
        for (unsigned int a = 2; a < argumentCount; a++) {
            NSUInteger argumentIndex = a - 2;
            NSString *name = (argumentIndex < selectorParts.count) ? selectorParts[argumentIndex] : nil;
            [paramNames addObject:(name.length > 0)
                ? name
                : [NSString stringWithFormat:@"arg%lu", (unsigned long)argumentIndex]];
            char *typeEncoding = method_copyArgumentType(method, a);
            [paramTypes addObject:[XPReflect friendlyTypeNameFromTypeEncoding:(typeEncoding ?: "")]];
            if (typeEncoding) {
                free(typeEncoding);
            }
        }

        [apiList addObject:@{
            @"api": [NSString stringWithFormat:@"%@.%@", className, selectorName],
            @"param_name": paramNames,
            @"param_type": paramTypes,
        }];
    }
    free(methods);
}

// 把 runtime 类型编码转为可读类型名：@ → id、@"NSString" → NSString、i → int、{CGPoint=dd} → CGPoint
+ (NSString *)friendlyTypeNameFromTypeEncoding:(const char *)encoding {
    if (!encoding || encoding[0] == '\0') {
        return @"id";
    }
    NSString *type = [NSString stringWithUTF8String:encoding];
    if ([type hasPrefix:@"@"]) {
        if (type.length >= 3 && [type hasPrefix:@"@\""] && [type hasSuffix:@"\""]) {
            return [type substringWithRange:NSMakeRange(2, type.length - 3)];
        }
        return @"id";
    }
    static NSDictionary<NSString *, NSString *> *encodingMap = nil;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        encodingMap = @{
            @"i": @"int",
            @"s": @"short",
            @"l": @"long",
            @"q": @"long long",
            @"I": @"unsigned int",
            @"S": @"unsigned short",
            @"L": @"unsigned long",
            @"Q": @"unsigned long long",
            @"f": @"float",
            @"d": @"double",
            @"B": @"BOOL",
            @"c": @"char",
            @"C": @"unsigned char",
            @"*": @"char*",
            @"v": @"void",
            @"#": @"Class",
            @":": @"SEL",
        };
    });
    NSString *mapped = encodingMap[type];
    if (mapped) {
        return mapped;
    }
    if ([type hasPrefix:@"{"]) {
        // 结构体编码 {CGPoint=dd} → CGPoint
        NSRange equalRange = [type rangeOfString:@"="];
        if (equalRange.location != NSNotFound && equalRange.location > 1) {
            return [type substringWithRange:NSMakeRange(1, equalRange.location - 1)];
        }
    }
    return type;
}

#pragma mark - 工具方法

// 属性名转 setter 选择子：testProperty → setTestProperty:
+ (NSString *)setterNameOfPropertyName:(NSString *)propertyName {
    if (propertyName.length == 0) {
        return @"";
    }
    NSString *initial = [[propertyName substringToIndex:1] uppercaseString];
    NSString *remaining = [propertyName substringFromIndex:1];
    return [NSString stringWithFormat:@"set%@%@:", initial, remaining];
}

// ver 判断：兼容整数 2 与字符串 "2"（NSJSONSerialization 解出的数字是 NSNumber）
+ (BOOL)isVer2:(id)ver {
    if ([ver isKindOfClass:[NSString class]]) {
        return [(NSString *)ver integerValue] == 2;
    }
    if ([ver isKindOfClass:[NSNumber class]]) {
        return [(NSNumber *)ver intValue] == 2;
    }
    return NO;
}

// 数值参数统一取字符串（NSString/NSNumber 均可），null/缺失按 "0" 处理
+ (NSString *)numericStringFromValue:(id)value {
    if ([value isKindOfClass:[NSNumber class]]) {
        return [(NSNumber *)value stringValue];
    }
    if ([value isKindOfClass:[NSString class]]) {
        return (NSString *)value;
    }
    return @"0";
}

// 对象参数统一取字符串，null/缺失返回空串
+ (NSString *)stringFromValue:(id)value {
    if (!value || [value isKindOfClass:[NSNull class]]) {
        return @"";
    }
    if ([value isKindOfClass:[NSString class]]) {
        return value;
    }
    return [NSString stringWithFormat:@"%@", value];
}

// 依次从命令顶层（camel/snake 两键）与 api 字典内查找参数数组
- (NSArray *)arrayValueForKey:(NSString *)key
                     flatKey:(NSString *)flatKey
                      cmdDict:(NSDictionary *)cmdDict
                       apiDict:(NSDictionary *)apiDict {
    id value = cmdDict[key];
    if (![value isKindOfClass:[NSArray class]]) {
        value = cmdDict[flatKey];
    }
    if (![value isKindOfClass:[NSArray class]] && apiDict != nil) {
        value = apiDict[key];
    }
    if (![value isKindOfClass:[NSArray class]] && apiDict != nil) {
        value = apiDict[flatKey];
    }
    return [value isKindOfClass:[NSArray class]] ? value : nil;
}

@end
