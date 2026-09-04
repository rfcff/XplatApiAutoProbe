//
//  XPTestMgr.mm
//  XplatApiAutoProbe —— iOS 统一测试 RPC 库
//
//  测试管理单例实现：
//  - dispatch_once 单例；
//  - 组合 XPConnMgr（网络层）与 XPCmdRunner（命令分发）；
//  - 回包统一序列化为 {"type":..., "key":..., "value":...} 帧发往最近活跃连接。
//

#import "XPTestMgr.h"
#import "XPConnMgr.h"
#import "XPCmdRunner.h"
#import "XPLog.h"

// 服务端默认监听端口（线协议约定 9000）
static const NSInteger kXPDefaultListenPort = 9000;

// 回包 JSON 字段名（线协议约定）
static NSString * const kXPKeyType  = @"type";
static NSString * const kXPKeyKey   = @"key";
static NSString * const kXPKeyValue = @"value";

// 回包类型（线协议约定）
static NSString * const kXPTypeReturn   = @"return";
static NSString * const kXPTypeError    = @"error";
static NSString * const kXPTypeCallback = @"callback";

@interface XPTestMgr () <XPConnMgrDelegate>
// 网络层连接管理器
@property (nonatomic, strong, nullable) XPConnMgr *connMgr;
// 命令分发器
@property (nonatomic, strong, nullable) XPCmdRunner *cmdRunner;
// 服务是否已启动
@property (nonatomic, assign, getter=isRunning) BOOL running;

@end

@implementation XPTestMgr

#pragma mark - 单例

+ (instancetype)sharedInstance {
    static XPTestMgr *instance = nil;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        instance = [[XPTestMgr alloc] initPrivate];
    });
    return instance;
}

- (instancetype)initPrivate {
    self = [super init];
    if (self) {
        _running = NO;
    }
    return self;
}

+ (NSInteger)defaultListenPort {
    return kXPDefaultListenPort;
}

#pragma mark - 启停

- (void)startTestWithInstMgr:(id<XPBaseInstMgr>)mgr {
    [self startTestWithPort:kXPDefaultListenPort InstMgr:mgr CustInvoc:nil];
}

- (void)startTestWithCustInvoc:(id<XPCustomInvocation>)inv {
    [self startTestWithPort:kXPDefaultListenPort InstMgr:nil CustInvoc:inv];
}

- (void)startTestWithInstMgr:(id<XPBaseInstMgr>)mgr CustInvoc:(id<XPCustomInvocation>)inv {
    [self startTestWithPort:kXPDefaultListenPort InstMgr:mgr CustInvoc:inv];
}

- (void)startTestWithPort:(NSInteger)port
                  InstMgr:(id<XPBaseInstMgr>)mgr
                 CustInvoc:(id<XPCustomInvocation>)inv {
    // 幂等：先停掉旧服务（每次启动完全覆盖上一次配置）
    [self stopTest];

    // port < 0 或 = 0 使用默认端口
    if (port <= 0) {
        port = kXPDefaultListenPort;
    }
    self.instMgr = mgr;
    self.custInvoc = inv;

    self.cmdRunner = [[XPCmdRunner alloc] initWithTestMgr:self];
    self.connMgr = [[XPConnMgr alloc] initWithDelegate:self];
    [self.connMgr openConn:port];
    self.running = YES;
    XPLog(XPLogLevelInfo, @"mgr", @"RPC 服务已启动（端口 %ld，反射管理器: %@，自定义调用: %@）",
          (long)port, (mgr != nil) ? @"已注册" : @"未注册", (inv != nil) ? @"已注册" : @"未注册");
}

- (void)stopTest {
    // 先停命令分发（丢弃排队命令），再关网络
    BOOL wasRunning = self.running;
    [self.cmdRunner stop];
    self.cmdRunner = nil;
    [self.connMgr closeConn];
    self.connMgr = nil;
    self.running = NO;
    if (wasRunning) {
        XPLog(XPLogLevelInfo, @"mgr", @"RPC 服务已停止");
    }
}

#pragma mark - XPConnMgrDelegate

- (void)connMgr:(XPConnMgr *)mgr didReceiveCommand:(NSString *)command {
    [self.cmdRunner onCommandFrame:command];
}

#pragma mark - 回包

- (void)sendReturn:(NSString *)key value:(NSString *)value {
    [self sendPayloadWithType:kXPTypeReturn
                           key:key
                         value:(value ?: @"null")]; // null 返回值统一为 "null"（线协议约定）
}

- (void)sendError:(NSString *)key msg:(NSString *)msg {
    [self sendPayloadWithType:kXPTypeError
                           key:key
                         value:(msg ?: @"")];
}

- (void)sendCallback:(NSString *)name info:(NSString *)info {
    [self sendPayloadWithType:kXPTypeCallback
                           key:name
                         value:(info ?: @"")];
}

- (void)sendCallbackJson:(NSString *)name json:(NSDictionary<NSString *, id> *)json {
    NSString *text = nil;
    if (json) {
        NSData *data = [NSJSONSerialization dataWithJSONObject:json options:0 error:nil];
        if (data) {
            text = [[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding];
        }
    }
    // 序列化失败或入参为 nil 时降级为空对象，保证回包始终是合法 JSON
    [self sendCallback:name info:(text ?: @"{}")];
}

- (void)sendCallback:(NSString *)name infoDict:(NSDictionary<NSString *, id> *)info {
    [self sendCallbackJson:name json:info];
}

// 统一回包出口：{"type":..., "key":..., "value":...} 序列化后发往最近活跃连接
- (void)sendPayloadWithType:(NSString *)type key:(NSString *)key value:(NSString *)value {
    XPLog(XPLogLevelVerbose, @"mgr", @"发送回包帧 type=%@ key=%@ value=%@", type, key, value);
    NSDictionary *payload = @{
        kXPKeyType:  type,
        kXPKeyKey:   (key ?: @""),
        kXPKeyValue: (value ?: @""),
    };
    NSData *data = [NSJSONSerialization dataWithJSONObject:payload options:0 error:nil];
    if (!data) {
        return;
    }
    NSString *message = [[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding];
    if (message) {
        [self.connMgr sendResult:message];
    }
}

@end
