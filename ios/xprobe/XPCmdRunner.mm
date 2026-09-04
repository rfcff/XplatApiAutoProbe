//
//  XPCmdRunner.mm
//  XplatApiAutoProbe —— iOS 统一测试 RPC 库
//
//  命令分发实现：
//  - JSON 解析（NSJSONSerialization）：单对象归一为数组，数组内命令顺序执行；
//  - threadMode == 1（数字 1 或字符串 "1"）→ 主队列 dispatch_async 执行；
//    0 / 缺省 → 串行后台队列执行；
//  - 同一帧内「连续相同线程模式」的命令合并为一批，保证批内顺序，
//    与其他平台的分组派发策略保持一致。
//

#import "XPCmdRunner.h"
#import "XPReflect.h"
#import "XPTestMgr.h"
#import "XPLog.h"

// threadMode 字段名（线协议约定）
static NSString * const kXPKeyThreadMode = @"threadMode";
// 解析错误回包使用的 key（线协议约定）
static NSString * const kXPParseErrorKey = @"parseError";
// 解析失败日志中命令帧文本的最大预览长度（防止超长帧刷屏）
static const NSUInteger kXPCmdPreviewLength = 200;

// 命令帧文本预览：超长截断加省略号
static NSString *XPCmdFramePreview(NSString *frame) {
    if (frame.length <= kXPCmdPreviewLength) {
        return frame ?: @"";
    }
    return [[frame substringToIndex:kXPCmdPreviewLength] stringByAppendingString:@"..."];
}

@interface XPCmdRunner ()
// 回指单例管理器（弱引用，避免循环持有）
@property (nonatomic, weak, nullable) XPTestMgr *testMgr;
// 反射执行器
@property (nonatomic, strong) XPReflect *reflect;
// 串行后台队列：保证同一帧内命令顺序执行
@property (nonatomic, strong) dispatch_queue_t workQueue;
// 停止标记：置位后丢弃尚未执行的排队命令
@property (nonatomic, assign) BOOL stopped;

@end

@implementation XPCmdRunner

- (instancetype)initWithTestMgr:(XPTestMgr *)testMgr {
    self = [super init];
    if (self) {
        _testMgr = testMgr;
        _reflect = [[XPReflect alloc] initWithTestMgr:testMgr];
        _workQueue = dispatch_queue_create("com.xprobe.rpc.cmd", DISPATCH_QUEUE_SERIAL);
        _stopped = NO;
    }
    return self;
}

- (void)stop {
    self.stopped = YES;
}

#pragma mark - 命令帧入口

- (void)onCommandFrame:(NSString *)frame {
    // 解析 JSON：一条帧可为单 JSON 对象或对象数组
    id jsonObject = [NSJSONSerialization JSONObjectWithData:[frame dataUsingEncoding:NSUTF8StringEncoding]
                                                   options:0
                                                     error:nil];
    if (!jsonObject) {
        XPLog(XPLogLevelWarn, @"cmd", @"命令帧 JSON 解析失败（parseError）: %@", XPCmdFramePreview(frame));
        [self.testMgr sendError:kXPParseErrorKey msg:@"json parse failed"];
        return;
    }

    NSArray<NSDictionary *> *commands = nil;
    if ([jsonObject isKindOfClass:[NSDictionary class]]) {
        // 单对象归一为数组
        commands = @[jsonObject];
    } else if ([jsonObject isKindOfClass:[NSArray class]]) {
        // 严格模式（PROTOCOL.md §4）：数组中任一元素不是 JSON 对象即整帧拒绝——
        // 只回一条 parseError，本帧命令全部不执行。与 C++ Core / Android 行为一致，
        // 目的是避免「部分执行」留下半截副作用，使测试脚本的状态不可控。
        NSArray *items = (NSArray *)jsonObject;
        NSMutableArray<NSDictionary *> *validCommands =
            [NSMutableArray arrayWithCapacity:items.count];
        NSUInteger itemIndex = 0;
        for (id item in items) {
            if (![item isKindOfClass:[NSDictionary class]]) {
                XPLog(XPLogLevelWarn, @"cmd", @"命令数组第 %lu 个元素不是 JSON 对象，整帧拒绝: %@",
                      (unsigned long)itemIndex, item);
                [self.testMgr sendError:kXPParseErrorKey
                                    msg:[NSString stringWithFormat:
                                             @"命令数组第 %lu 个元素不是 JSON 对象",
                                             (unsigned long)itemIndex]];
                return;
            }
            [validCommands addObject:item];
            itemIndex++;
        }
        commands = validCommands;
    } else {
        XPLog(XPLogLevelWarn, @"cmd", @"命令帧既不是 JSON 对象也不是数组（parseError）: %@",
              XPCmdFramePreview(frame));
        [self.testMgr sendError:kXPParseErrorKey
                           msg:[NSString stringWithFormat:@"命令帧既不是 JSON 对象也不是数组: %@", frame]];
        return;
    }

    if (commands.count == 0) {
        return;
    }

    if (commands.count == 1 && [self isMainThreadMode:commands[0]]) {
        // 单命令且主线程模式：投递主队列执行
        NSDictionary *command = commands[0];
        __weak XPCmdRunner *weakSelf = self;
        dispatch_async(dispatch_get_main_queue(), ^{
            @autoreleasepool {
                XPCmdRunner *strongSelf = weakSelf;
                if (strongSelf && !strongSelf.stopped) {
                    [strongSelf executeCommandSafely:command];
                }
            }
        });
        return;
    }

    // 其余情况：投后台串行队列，按分组顺序执行
    dispatch_async(self.workQueue, ^{
        @autoreleasepool {
            [self executeCommandList:commands];
        }
    });
}

#pragma mark - 分组执行

// 在当前线程上按「连续相同线程模式」分组顺序执行命令列表
- (void)executeCommandList:(NSArray<NSDictionary *> *)commands {
    NSUInteger index = 0;
    while (index < commands.count && !self.stopped) {
        // 找出从 index 开始连续同线程模式的一段
        BOOL mainThreadMode = [self isMainThreadMode:commands[index]];
        NSUInteger end = index;
        while (end < commands.count && [self isMainThreadMode:commands[end]] == mainThreadMode) {
            end++;
        }
        NSArray<NSDictionary *> *group = [commands subarrayWithRange:NSMakeRange(index, end - index)];

        if (mainThreadMode) {
            // 主线程模式：同步切到主队列执行，保持整帧顺序
            if ([NSThread isMainThread]) {
                [self runGroup:group];
            } else {
                dispatch_sync(dispatch_get_main_queue(), ^{
                    [self runGroup:group];
                });
            }
        } else {
            // 后台模式：在当前串行队列线程执行
            [self runGroup:group];
        }
        index = end;
    }
}

// 顺序执行一组命令；单条命令异常只上报不断帧
- (void)runGroup:(NSArray<NSDictionary *> *)group {
    for (NSDictionary *command in group) {
        if (self.stopped) {
            return;
        }
        [self executeCommandSafely:command];
    }
}

// 单条命令兜底执行：反射层内部已捕获执行异常，这里再兜住解析层的意外异常
- (void)executeCommandSafely:(NSDictionary *)command {
    XPLog(XPLogLevelDebug, @"cmd", @"派发命令: %@", command);
    @try {
        [self.reflect executeCommand:command];
    } @catch (NSException *exception) {
        XPLog(XPLogLevelError, @"cmd", @"命令派发异常: %@ %@", exception.name, exception.reason);
        [self.testMgr sendError:exception.name msg:exception.reason];
    }
}

#pragma mark - 工具

// threadMode 解析：兼容数字 1 与字符串 "1"（NSJSONSerialization 解出的数字是 NSNumber）
- (BOOL)isMainThreadMode:(NSDictionary *)command {
    id mode = command[kXPKeyThreadMode];
    if ([mode isKindOfClass:[NSString class]]) {
        return [(NSString *)mode integerValue] == 1;
    }
    if ([mode isKindOfClass:[NSNumber class]]) {
        return [(NSNumber *)mode intValue] == 1;
    }
    // 0 / 缺省 / 非法值：后台执行
    return NO;
}

@end
