//
//  RtcManager.mm
//

#import "RtcManager.h"

#import "../../../ios/xprobe/XplatApiAutoProbe.h"

#if defined(XPROBE_USE_THUNDER)
#import "ThunderEngine.h"
#endif

@interface RtcManager ()
#if defined(XPROBE_USE_THUNDER)
@property (nonatomic, strong, readwrite, nullable) ThunderEngine *engine;
#else
@property (nonatomic, assign) BOOL initialized;
#endif
@property (nonatomic, copy, nullable) NSString *roomName;
@property (nonatomic, copy, nullable) NSString *uid;
@property (nonatomic, copy) NSString *remoteUid;
#if !defined(XPROBE_HEADLESS_ONLY)
@property (nonatomic, weak) NSView *localContainer;
@property (nonatomic, weak) NSView *remoteContainer;
#endif
#if defined(XPROBE_USE_THUNDER)
@property (nonatomic, strong, nullable) NSView *localView;
@property (nonatomic, strong, nullable) NSView *remoteView;
#else
@property (nonatomic, strong, nullable) NSView *localPlaceholder;
@property (nonatomic, strong, nullable) NSView *remotePlaceholder;
#endif
@property (nonatomic, assign) BOOL localPreviewOn;
@end

#if defined(XPROBE_USE_THUNDER)
// ThunderEventDelegate 方法较多；实现与 Android RtcManager 对齐的子集，其余靠 respondsToSelector
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wprotocol"
@interface RtcManager (ThunderEvents) <ThunderEventDelegate>
@end
#pragma clang diagnostic pop
#endif

@implementation RtcManager

+ (instancetype)sharedInstance {
    static RtcManager *instance = nil;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        instance = [[RtcManager alloc] initPrivate];
    });
    return instance;
}

- (instancetype)initPrivate {
    self = [super init];
    if (self) {
        _remoteUid = @"";
    }
    return self;
}

#if !defined(XPROBE_HEADLESS_ONLY)
- (void)setVideoContainers:(NSView *)localContainer remoteContainer:(NSView *)remoteContainer {
    self.localContainer = localContainer;
    self.remoteContainer = remoteContainer;
}
#endif

- (BOOL)isInitialized {
#if defined(XPROBE_USE_THUNDER)
    return self.engine != nil;
#else
    return self.initialized;
#endif
}

#pragma mark - Lifecycle

- (long long)initializeWithAppId:(NSString *)appId sceneId:(long long)sceneId {
    if (self.isInitialized) {
        return 0;
    }
    long long begin = (long long)([[NSDate date] timeIntervalSince1970] * 1000.0);

#if defined(XPROBE_USE_THUNDER)
    __block ThunderEngine *created = nil;
    void (^createBlock)(void) = ^{
        created = [ThunderEngine createEngine:appId sceneId:(NSInteger)sceneId delegate:self];
        if (created) {
            [created setAudioConfig:THUNDER_AUDIO_CONFIG_MUSIC_STANDARD_PRO
                         commutMode:THUNDER_COMMUT_MODE_DEFAULT
                       scenarioMode:THUNDER_SCENARIO_MODE_DEFAULT];
        }
    };
    if ([NSThread isMainThread]) {
        createBlock();
    } else {
        dispatch_sync(dispatch_get_main_queue(), createBlock);
    }
    self.engine = created;
    if (!self.engine) {
        [self log:@"createEngine 失败"];
        return -1;
    }
#else
    usleep(50000);
    self.initialized = YES;
#endif

    long long cost = (long long)([[NSDate date] timeIntervalSince1970] * 1000.0) - begin;
    [self log:[NSString stringWithFormat:@"createEngine 耗时 %lldms, appId=%@, sceneId=%lld",
               cost, appId, sceneId]];
    return cost;
}

- (void)deInitialize {
    if (!self.isInitialized) {
        return;
    }
#if defined(XPROBE_USE_THUNDER)
    [self stopLocalPreview];
    [ThunderEngine destroyEngine];
    self.engine = nil;
    NSView *local = self.localView;
    NSView *remote = self.remoteView;
    self.localView = nil;
    self.remoteView = nil;
    void (^cleanup)(void) = ^{
        [local removeFromSuperview];
        [remote removeFromSuperview];
    };
    if ([NSThread isMainThread]) {
        cleanup();
    } else {
        dispatch_sync(dispatch_get_main_queue(), cleanup);
    }
#else
    [self stopLocalPreview];
    self.initialized = NO;
#if !defined(XPROBE_HEADLESS_ONLY)
    [self.localPlaceholder removeFromSuperview];
    [self.remotePlaceholder removeFromSuperview];
    self.localPlaceholder = nil;
    self.remotePlaceholder = nil;
#endif
#endif
    self.roomName = nil;
    self.uid = nil;
    self.remoteUid = @"";
    [self log:@"destroyEngine 完成"];
}

#pragma mark - Room

- (int)joinRoom:(NSString *)roomName uid:(NSString *)uid {
    return [self joinRoom:roomName uid:uid token:nil];
}

- (int)joinRoom:(NSString *)roomName uid:(NSString *)uid token:(NSString *)token {
    if (!self.isInitialized) {
        return -1;
    }
    self.roomName = roomName;
    self.uid = uid;

#if defined(XPROBE_USE_THUNDER)
    int ret = [self.engine joinRoom:token roomName:roomName uid:uid];
    [self log:[NSString stringWithFormat:@"joinRoom(%@, %@) ret=%d", roomName, uid, ret]];
    return ret;
#else
    [self log:[NSString stringWithFormat:@"joinRoom(%@, %@) ret=0", roomName, uid]];
    __weak typeof(self) weakSelf = self;
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(0.2 * NSEC_PER_SEC)),
                   dispatch_get_main_queue(), ^{
        RtcManager *strongSelf = weakSelf;
        if (!strongSelf || !strongSelf.isInitialized) {
            return;
        }
        NSString *msg = [NSString stringWithFormat:@"onJoinRoomSuccess: room=%@ uid=%@ elapsed=200",
                         roomName, uid];
        [strongSelf log:msg];
        [[XPTestMgr sharedInstance] sendCallback:@"onJoinRoomSuccess" info:msg];

        NSString *connMsg = @"onConnectionStatus: status=1";
        [strongSelf log:connMsg];
        [[XPTestMgr sharedInstance] sendCallback:@"onConnectionStatus" info:connMsg];

        strongSelf.remoteUid = @"999";
        NSString *joinedMsg = @"onUserJoined: uid=999 elapsed=250";
        [strongSelf log:joinedMsg];
        [[XPTestMgr sharedInstance] sendCallback:@"onUserJoined" info:joinedMsg];
    });
    return 0;
#endif
}

- (int)leaveRoom {
    if (!self.isInitialized) {
        return -1;
    }
    self.remoteUid = @"";
#if defined(XPROBE_USE_THUNDER)
    int ret = [self.engine leaveRoom];
    [self log:[NSString stringWithFormat:@"leaveRoom() ret=%d", ret]];
    return ret;
#else
    [self log:@"leaveRoom() ret=0"];
    NSString *msg = @"onLeaveRoom: status=0";
    [[XPTestMgr sharedInstance] sendCallback:@"onLeaveRoom" info:msg];
    return 0;
#endif
}

#pragma mark - Subscribe / Video

- (int)addSubscribe:(NSString *)channelId uid:(NSString *)uid {
    if (!self.isInitialized) {
        return -1;
    }
#if defined(XPROBE_USE_THUNDER)
    int ret = [self.engine addSubscribe:channelId uid:uid];
    [self log:[NSString stringWithFormat:@"addSubscribe(%@, %@) ret=%d", channelId, uid, ret]];
    return ret;
#else
    [self log:[NSString stringWithFormat:@"addSubscribe(%@, %@) ret=0", channelId, uid]];
    return 0;
#endif
}

- (int)removeSubscribe:(NSString *)channelId uid:(NSString *)uid {
    if (!self.isInitialized) {
        return -1;
    }
#if defined(XPROBE_USE_THUNDER)
    int ret = [self.engine removeSubscribe:channelId uid:uid];
    [self log:[NSString stringWithFormat:@"removeSubscribe(%@, %@) ret=%d", channelId, uid, ret]];
    return ret;
#else
    [self log:[NSString stringWithFormat:@"removeSubscribe(%@, %@) ret=0", channelId, uid]];
    return 0;
#endif
}

- (int)setupRemoteVideo:(NSString *)uid {
    if (!self.isInitialized) {
        return -1;
    }
#if defined(XPROBE_USE_THUNDER)
    __block int ret = -1;
    void (^block)(void) = ^{
        NSView *view = [self ensureRemoteView];
        if (!view) {
            ret = -1;
            return;
        }
        ThunderVideoCanvas *canvas = [[ThunderVideoCanvas alloc] init];
        canvas.view = view;
        canvas.renderMode = THUNDER_RENDER_MODE_ASPECT_FIT;
        canvas.uid = uid;
        ret = [self.engine setRemoteVideoCanvas:canvas];
    };
    if ([NSThread isMainThread]) {
        block();
    } else {
        dispatch_sync(dispatch_get_main_queue(), block);
    }
    [self log:[NSString stringWithFormat:@"setRemoteVideoCanvas(%@) ret=%d", uid, ret]];
    return ret;
#else
#if !defined(XPROBE_HEADLESS_ONLY)
    [self ensureRemotePlaceholder];
#endif
    [self log:[NSString stringWithFormat:@"setRemoteVideoCanvas(%@) ret=0", uid]];
    return 0;
#endif
}

- (int)startLocalPreview {
    if (!self.isInitialized) {
        return -1;
    }
#if defined(XPROBE_USE_THUNDER)
    __block int ret = -1;
    void (^block)(void) = ^{
        NSView *view = [self ensureLocalView];
        if (!view) {
            ret = -1;
            return;
        }
        ThunderVideoCanvas *canvas = [[ThunderVideoCanvas alloc] init];
        canvas.view = view;
        canvas.renderMode = THUNDER_RENDER_MODE_ASPECT_FIT;
        canvas.uid = self.uid;
        ret = [self.engine setLocalVideoCanvas:canvas];
        if (ret == 0) {
            ret = [self.engine enableLocalVideoCapture:YES];
            if (ret == 0) {
                ret = [self.engine startLocalVideoPreview];
            }
        }
        if (ret == 0) {
            self.localPreviewOn = YES;
            view.hidden = NO;
        }
    };
    if ([NSThread isMainThread]) {
        block();
    } else {
        dispatch_sync(dispatch_get_main_queue(), block);
    }
    [self log:[NSString stringWithFormat:@"startLocalPreview() ret=%d", ret]];
    return ret;
#else
#if !defined(XPROBE_HEADLESS_ONLY)
    [self ensureLocalPlaceholder];
#endif
    self.localPreviewOn = YES;
    [self log:@"startLocalPreview() ret=0"];
    return 0;
#endif
}

- (int)stopLocalPreview {
    if (!self.isInitialized) {
        return -1;
    }
#if defined(XPROBE_USE_THUNDER)
    __block int ret = -1;
    void (^block)(void) = ^{
        ret = [self.engine stopLocalVideoPreview];
        self.localPreviewOn = NO;
        self.localView.hidden = YES;
    };
    if ([NSThread isMainThread]) {
        block();
    } else {
        dispatch_sync(dispatch_get_main_queue(), block);
    }
    [self log:[NSString stringWithFormat:@"stopLocalPreview() ret=%d", ret]];
    return ret;
#else
    self.localPreviewOn = NO;
#if !defined(XPROBE_HEADLESS_ONLY)
    if (self.localPlaceholder) {
        self.localPlaceholder.hidden = YES;
    }
#endif
    [self log:@"stopLocalPreview() ret=0"];
    return 0;
#endif
}

#if defined(XPROBE_USE_THUNDER)
#pragma mark - Video views

- (NSView *)ensureLocalView {
    if (self.localView) {
        return self.localView;
    }
#if !defined(XPROBE_HEADLESS_ONLY)
    NSView *container = self.localContainer;
#else
    NSView *container = nil;
#endif
    if (!container) {
        // headless / 无容器：仍分配 view 供 setLocalVideoCanvas，不挂 UI
        self.localView = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 320, 240)];
        self.localView.wantsLayer = YES;
        return self.localView;
    }
    NSView *view = [[NSView alloc] initWithFrame:container.bounds];
    view.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    view.wantsLayer = YES;
    [container addSubview:view];
    self.localView = view;
    return view;
}

- (NSView *)ensureRemoteView {
    if (self.remoteView) {
        return self.remoteView;
    }
#if !defined(XPROBE_HEADLESS_ONLY)
    NSView *container = self.remoteContainer;
#else
    NSView *container = nil;
#endif
    if (!container) {
        self.remoteView = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 320, 240)];
        self.remoteView.wantsLayer = YES;
        return self.remoteView;
    }
    NSView *view = [[NSView alloc] initWithFrame:container.bounds];
    view.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    view.wantsLayer = YES;
    [container addSubview:view];
    self.remoteView = view;
    return view;
}

#pragma mark - ThunderEventDelegate (Android-parity subset)

- (void)thunderEngine:(ThunderEngine *)engine
    onJoinRoomSuccess:(NSString *)room
              withUid:(NSString *)uid
              elapsed:(NSInteger)elapsed {
    NSString *msg = [NSString stringWithFormat:@"onJoinRoomSuccess: room=%@ uid=%@ elapsed=%ld",
                     room, uid, (long)elapsed];
    [self log:msg];
    [[XPTestMgr sharedInstance] sendCallback:@"onJoinRoomSuccess" info:msg];
}

- (void)thunderEngine:(ThunderEngine *)engine onLeaveRoomWithStats:(ThunderRtcRoomStats *)stats {
    (void)stats;
    NSString *msg = @"onLeaveRoom: status=0";
    [self log:msg];
    [[XPTestMgr sharedInstance] sendCallback:@"onLeaveRoom" info:msg];
}

- (void)thunderEngine:(ThunderEngine *)engine onUserJoined:(NSString *)uid elapsed:(NSInteger)elapsed {
    self.remoteUid = uid ?: @"";
    NSString *msg = [NSString stringWithFormat:@"onUserJoined: uid=%@ elapsed=%ld", uid, (long)elapsed];
    [self log:msg];
    [[XPTestMgr sharedInstance] sendCallback:@"onUserJoined" info:msg];
}

- (void)thunderEngine:(ThunderEngine *)engine onConnectionStatus:(ThunderConnectionStatus)status {
    NSString *msg = [NSString stringWithFormat:@"onConnectionStatus: status=%ld", (long)status];
    [self log:msg];
    [[XPTestMgr sharedInstance] sendCallback:@"onConnectionStatus" info:msg];
}

- (void)thunderEngine:(ThunderEngine *)engine
    onRemoteVideoPlay:(NSString *)uid
                 size:(CGSize)size
              elapsed:(NSInteger)elapsed {
    NSString *msg = [NSString stringWithFormat:@"onRemoteVideoPlay: uid=%@ %.0fx%.0f elapsed=%ld",
                     uid, size.width, size.height, (long)elapsed];
    [self log:msg];
    [[XPTestMgr sharedInstance] sendCallback:@"onRemoteVideoPlay" info:msg];
}

- (void)thunderEngine:(ThunderEngine *)engine onRemoteAudioPlay:(NSString *)uid elapsed:(NSInteger)elapsed {
    NSString *msg = [NSString stringWithFormat:@"onRemoteAudioPlay: uid=%@ elapsed=%ld", uid, (long)elapsed];
    [self log:msg];
    [[XPTestMgr sharedInstance] sendCallback:@"onRemoteAudioPlay" info:msg];
}

- (void)thunderEngine:(ThunderEngine *)engine onRemoteAudioStopped:(BOOL)stopped byUid:(NSString *)uid {
    NSString *msg = [NSString stringWithFormat:@"onRemoteAudioStopped: uid=%@ stopped=%@",
                     uid, stopped ? @"YES" : @"NO"];
    [self log:msg];
    [[XPTestMgr sharedInstance] sendCallback:@"onRemoteAudioStopped" info:msg];
}

- (void)thunderEngine:(ThunderEngine *)engine onRemoteVideoStopped:(BOOL)stopped byUid:(NSString *)uid {
    NSString *msg = [NSString stringWithFormat:@"onRemoteVideoStopped: uid=%@ stopped=%@",
                     uid, stopped ? @"YES" : @"NO"];
    [self log:msg];
    [[XPTestMgr sharedInstance] sendCallback:@"onRemoteVideoStopped" info:msg];
}

- (void)thunderEngine:(ThunderEngine *)engine sdkAuthResult:(ThunderRtcSdkAuthResult)sdkAuthResult {
    NSString *msg = [NSString stringWithFormat:@"sdkAuthResult: auth=%ld", (long)sdkAuthResult];
    [self log:msg];
    [[XPTestMgr sharedInstance] sendCallback:@"sdkAuthResult" info:msg];
}

#else
#if !defined(XPROBE_HEADLESS_ONLY)
- (void)ensureLocalPlaceholder {
    if (!self.localContainer || self.localPlaceholder) {
        return;
    }
    NSView *view = [[NSView alloc] initWithFrame:self.localContainer.bounds];
    view.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    view.wantsLayer = YES;
    view.layer.backgroundColor = [[NSColor colorWithCalibratedRed:0.2 green:0.5 blue:0.9 alpha:1.0] CGColor];
    [self.localContainer addSubview:view];
    self.localPlaceholder = view;
}

- (void)ensureRemotePlaceholder {
    if (!self.remoteContainer || self.remotePlaceholder) {
        return;
    }
    NSView *view = [[NSView alloc] initWithFrame:self.remoteContainer.bounds];
    view.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    view.wantsLayer = YES;
    view.layer.backgroundColor = [[NSColor colorWithCalibratedRed:0.9 green:0.4 blue:0.2 alpha:1.0] CGColor];
    [self.remoteContainer addSubview:view];
    self.remotePlaceholder = view;
}
#endif
#endif

#pragma mark - Utils

- (void)log:(NSString *)msg {
    RtcUiLogBlock logger = self.uiLogger;
    if (logger) {
        dispatch_async(dispatch_get_main_queue(), ^{
            RtcUiLogBlock current = self.uiLogger;
            if (current) {
                current(msg);
            }
        });
    }
}

+ (NSString *)safe:(NSString *)s defaultValue:(NSString *)def {
    return (s == nil || s.length == 0) ? def : s;
}

@end
