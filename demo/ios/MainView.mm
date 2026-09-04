//
//  MainView.mm
//

#if !defined(XPROBE_HEADLESS_ONLY)

#import "MainView.h"

#import "rtc/RtcManager.h"

static const NSInteger kMaxLogLines = 300;

@interface MainView ()
@property (nonatomic, strong) NSView *localVideoContainer;
@property (nonatomic, strong) NSView *remoteVideoContainer;
@property (nonatomic, strong) NSTextField *appIdField;
@property (nonatomic, strong) NSTextField *sceneIdField;
@property (nonatomic, strong) NSTextField *roomField;
@property (nonatomic, strong) NSTextField *uidField;
@property (nonatomic, strong) NSButton *engineInitBtn;
@property (nonatomic, strong) NSButton *destroyBtn;
@property (nonatomic, strong) NSButton *joinBtn;
@property (nonatomic, strong) NSButton *leaveBtn;
@property (nonatomic, strong) NSButton *cameraBtn;
@property (nonatomic, strong) NSButton *subAudioBtn;
@property (nonatomic, strong) NSButton *subVideoBtn;
@property (nonatomic, strong) NSScrollView *logScrollView;
@property (nonatomic, strong) NSTextView *logView;
@property (nonatomic, strong) NSMutableString *logBuilder;
@property (nonatomic, assign) BOOL cameraOn;
@property (nonatomic, assign) BOOL subAudioOn;
@property (nonatomic, assign) BOOL subVideoOn;
@end

@implementation MainView

- (instancetype)initWithFrame:(NSRect)frameRect {
    self = [super initWithFrame:frameRect];
    if (self) {
        _logBuilder = [NSMutableString string];
        [self buildUI];
        [self setupButtons];
        [[RtcManager sharedInstance] setVideoContainers:self.localVideoContainer
                                       remoteContainer:self.remoteVideoContainer];
        [[RtcManager sharedInstance] setUiLogger:^(NSString *msg) {
            [self appendLog:msg];
        }];
        [self appendLog:@"demo 已启动，RPC 服务监听 0.0.0.0:9000"];
    }
    return self;
}

- (void)buildUI {
    CGFloat pad = 16.0;
    CGFloat y = NSMaxY(self.bounds) - pad;

    // Title row
    NSTextField *title = [self label:@"Thunder Demo (macOS)" size:18 bold:YES];
    title.frame = NSMakeRect(pad, y - 24, 320, 24);
    [self addSubview:title];

    NSTextField *rpcChip = [self label:@"RPC :9000" size:12 bold:NO];
    rpcChip.frame = NSMakeRect(NSMaxX(self.bounds) - pad - 90, y - 22, 90, 20);
    rpcChip.alignment = NSTextAlignmentCenter;
    rpcChip.wantsLayer = YES;
    rpcChip.layer.backgroundColor = [[NSColor colorWithCalibratedWhite:0.85 alpha:1.0] CGColor];
    rpcChip.layer.cornerRadius = 10.0;
    [self addSubview:rpcChip];
    y -= 36;

    // Video placeholders
    CGFloat videoH = 180.0;
    CGFloat halfW = (NSWidth(self.bounds) - pad * 2 - 12) / 2.0;
    self.localVideoContainer = [self videoContainerWithLabel:@"本地预览"
                                                       frame:NSMakeRect(pad, y - videoH, halfW, videoH)];
    self.remoteVideoContainer = [self videoContainerWithLabel:@"远端视频"
                                                        frame:NSMakeRect(pad + halfW + 12, y - videoH, halfW, videoH)];
    [self addSubview:self.localVideoContainer];
    [self addSubview:self.remoteVideoContainer];
    y -= videoH + 12;

    // Input fields 2x2
    self.appIdField = [self inputField:@"10034" frame:NSMakeRect(pad, y - 28, halfW, 28)];
    self.sceneIdField = [self inputField:@"0" frame:NSMakeRect(pad + halfW + 12, y - 28, halfW, 28)];
    [self addLabel:@"appId" above:self.appIdField];
    [self addLabel:@"sceneId" above:self.sceneIdField];
    y -= 52;

    self.roomField = [self inputField:@"82552971" frame:NSMakeRect(pad, y - 28, halfW, 28)];
    self.uidField = [self inputField:@"123456789" frame:NSMakeRect(pad + halfW + 12, y - 28, halfW, 28)];
    [self addLabel:@"room" above:self.roomField];
    [self addLabel:@"uid" above:self.uidField];
    y -= 52;

    // Buttons
    self.engineInitBtn = [self button:@"Init" frame:NSMakeRect(pad, y - 32, halfW, 32)];
    self.destroyBtn = [self button:@"Destroy" frame:NSMakeRect(pad + halfW + 12, y - 32, halfW, 32)];
    y -= 40;

    self.joinBtn = [self button:@"Join" frame:NSMakeRect(pad, y - 32, halfW, 32)];
    self.leaveBtn = [self button:@"Leave" frame:NSMakeRect(pad + halfW + 12, y - 32, halfW, 32)];
    y -= 40;

    CGFloat thirdW = (NSWidth(self.bounds) - pad * 2 - 16) / 3.0;
    self.cameraBtn = [self button:@"Camera On" frame:NSMakeRect(pad, y - 32, thirdW, 32)];
    self.subAudioBtn = [self button:@"Sub Audio" frame:NSMakeRect(pad + thirdW + 8, y - 32, thirdW, 32)];
    self.subVideoBtn = [self button:@"Sub Video" frame:NSMakeRect(pad + (thirdW + 8) * 2, y - 32, thirdW, 32)];
    y -= 44;

    // Log view
    self.logScrollView = [[NSScrollView alloc] initWithFrame:NSMakeRect(pad, pad, NSWidth(self.bounds) - pad * 2, y - pad)];
    self.logScrollView.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    self.logScrollView.hasVerticalScroller = YES;
    self.logScrollView.borderType = NSBezelBorder;
    self.logView = [[NSTextView alloc] initWithFrame:self.logScrollView.bounds];
    self.logView.editable = NO;
    self.logView.font = [NSFont monospacedSystemFontOfSize:11 weight:NSFontWeightRegular];
    self.logView.textColor = [NSColor colorWithCalibratedRed:0.62 green:0.70 blue:0.78 alpha:1.0];
    self.logView.backgroundColor = [NSColor colorWithCalibratedRed:0.08 green:0.09 blue:0.11 alpha:1.0];
    self.logScrollView.documentView = self.logView;
    [self addSubview:self.logScrollView];
}

- (void)setupButtons {
    RtcManager *rtc = [RtcManager sharedInstance];

    self.engineInitBtn.target = self;
    self.engineInitBtn.action = @selector(onInit:);
    self.destroyBtn.target = self;
    self.destroyBtn.action = @selector(onDestroy:);
    self.joinBtn.target = self;
    self.joinBtn.action = @selector(onJoin:);
    self.leaveBtn.target = self;
    self.leaveBtn.action = @selector(onLeave:);
    self.cameraBtn.target = self;
    self.cameraBtn.action = @selector(onCamera:);
    self.subAudioBtn.target = self;
    self.subAudioBtn.action = @selector(onSubAudio:);
    self.subVideoBtn.target = self;
    self.subVideoBtn.action = @selector(onSubVideo:);

    (void)rtc;
}

#pragma mark - Actions

- (void)onInit:(id)sender {
    (void)sender;
    RtcManager *rtc = [RtcManager sharedInstance];
    NSString *appId = [self text:self.appIdField defaultValue:@"10034"];
    long long sceneId = [[self text:self.sceneIdField defaultValue:@"0"] longLongValue];
    long long cost = [rtc initializeWithAppId:appId sceneId:sceneId];
    [self appendLog:[NSString stringWithFormat:@"createEngine appId=%@ sceneId=%lld cost=%lldms",
                     appId, sceneId, cost]];
}

- (void)onDestroy:(id)sender {
    (void)sender;
    [[RtcManager sharedInstance] deInitialize];
    [self resetToggles];
    [self appendLog:@"SDK 已销毁"];
}

- (void)onJoin:(id)sender {
    (void)sender;
    RtcManager *rtc = [RtcManager sharedInstance];
    NSString *room = [self text:self.roomField defaultValue:@"82552971"];
    NSString *uid = [self text:self.uidField defaultValue:@"123456789"];
    int ret = [rtc joinRoom:room uid:uid];
    [self appendLog:[NSString stringWithFormat:@"joinRoom(%@, %@) ret=%d", room, uid, ret]];
}

- (void)onLeave:(id)sender {
    (void)sender;
    int ret = [[RtcManager sharedInstance] leaveRoom];
    [self resetToggles];
    [self appendLog:[NSString stringWithFormat:@"leaveRoom ret=%d", ret]];
}

- (void)onCamera:(id)sender {
    (void)sender;
    RtcManager *rtc = [RtcManager sharedInstance];
    self.cameraOn = !self.cameraOn;
    int ret = self.cameraOn ? [rtc startLocalPreview] : [rtc stopLocalPreview];
    if (ret != 0) {
        self.cameraOn = NO;
    }
    [self refreshButtons];
}

- (void)onSubAudio:(id)sender {
    (void)sender;
    [self toggleSubscribe:YES];
}

- (void)onSubVideo:(id)sender {
    (void)sender;
    [self toggleSubscribe:NO];
}

- (void)toggleSubscribe:(BOOL)audio {
    RtcManager *rtc = [RtcManager sharedInstance];
    NSString *room = [self text:self.roomField defaultValue:rtc.roomName ?: @""];
    NSString *remoteUid = rtc.remoteUid;
    if (remoteUid.length == 0) {
        [self appendLog:@"尚未收到远端用户（onUserJoined）"];
        return;
    }
    BOOL on = audio ? self.subAudioOn : self.subVideoOn;
    int ret;
    if (on) {
        ret = [rtc removeSubscribe:room uid:remoteUid];
    } else {
        ret = [rtc addSubscribe:room uid:remoteUid];
        if (ret == 0 && !audio) {
            [rtc setupRemoteVideo:remoteUid];
        }
    }
    if (ret == 0) {
        if (audio) {
            self.subAudioOn = !self.subAudioOn;
        } else {
            self.subVideoOn = !self.subVideoOn;
        }
    } else {
        [self appendLog:[NSString stringWithFormat:@"订阅操作失败: %d", ret]];
    }
    [self refreshButtons];
}

- (void)resetToggles {
    self.cameraOn = NO;
    self.subAudioOn = NO;
    self.subVideoOn = NO;
    [self refreshButtons];
}

- (void)refreshButtons {
    self.cameraBtn.title = self.cameraOn ? @"Camera Off" : @"Camera On";
    self.subAudioBtn.title = self.subAudioOn ? @"Unsub Audio" : @"Sub Audio";
    self.subVideoBtn.title = self.subVideoOn ? @"Unsub Video" : @"Sub Video";
}

#pragma mark - Log

- (void)appendLog:(NSString *)msg {
    dispatch_async(dispatch_get_main_queue(), ^{
        NSDateFormatter *fmt = [[NSDateFormatter alloc] init];
        fmt.dateFormat = @"HH:mm:ss";
        [self.logBuilder appendFormat:@"[%@] %@\n", [fmt stringFromDate:[NSDate date]], msg];
        NSArray *lines = [self.logBuilder componentsSeparatedByString:@"\n"];
        if ((NSInteger)lines.count > kMaxLogLines) {
            NSInteger over = (NSInteger)lines.count - kMaxLogLines;
            NSRange range = NSMakeRange(0, 0);
            for (NSInteger i = 0; i < over; i++) {
                NSRange found = [self.logBuilder rangeOfString:@"\n"];
                if (found.location == NSNotFound) {
                    break;
                }
                range.length = found.location + 1;
            }
            [self.logBuilder deleteCharactersInRange:range];
        }
        self.logView.string = self.logBuilder;
        [self.logView scrollRangeToVisible:NSMakeRange(self.logView.string.length, 0)];
    });
}

#pragma mark - UI helpers

- (NSTextField *)label:(NSString *)text size:(CGFloat)size bold:(BOOL)bold {
    NSTextField *field = [[NSTextField alloc] initWithFrame:NSZeroRect];
    field.stringValue = text;
    field.bezeled = NO;
    field.editable = NO;
    field.drawsBackground = NO;
    field.font = bold ? [NSFont boldSystemFontOfSize:size] : [NSFont systemFontOfSize:size];
    return field;
}

- (void)addLabel:(NSString *)text above:(NSView *)view {
    NSTextField *label = [self label:text size:11 bold:NO];
    label.frame = NSMakeRect(NSMinX(view.frame), NSMaxY(view.frame) + 2, NSWidth(view.frame), 16);
    [self addSubview:label];
}

- (NSTextField *)inputField:(NSString *)value frame:(NSRect)frame {
    NSTextField *field = [[NSTextField alloc] initWithFrame:frame];
    field.stringValue = value;
    field.font = [NSFont systemFontOfSize:13];
    return field;
}

- (NSButton *)button:(NSString *)title frame:(NSRect)frame {
    NSButton *btn = [[NSButton alloc] initWithFrame:frame];
    btn.title = title;
    btn.bezelStyle = NSBezelStyleRounded;
    return btn;
}

- (NSView *)videoContainerWithLabel:(NSString *)label frame:(NSRect)frame {
    NSView *container = [[NSView alloc] initWithFrame:frame];
    container.wantsLayer = YES;
    container.layer.backgroundColor = [[NSColor colorWithCalibratedRed:0.06 green:0.08 blue:0.09 alpha:1.0] CGColor];
    container.layer.cornerRadius = 8.0;

    NSTextField *center = [self label:label size:12 bold:NO];
    center.textColor = [NSColor colorWithCalibratedRed:0.35 green:0.40 blue:0.45 alpha:1.0];
    center.alignment = NSTextAlignmentCenter;
    center.frame = NSMakeRect(0, NSHeight(frame) / 2 - 8, NSWidth(frame), 16);
    center.autoresizingMask = NSViewWidthSizable | NSViewMinYMargin | NSViewMaxYMargin;
    [container addSubview:center];

    NSTextField *badge = [self label:label size:11 bold:NO];
    badge.textColor = [NSColor whiteColor];
    badge.wantsLayer = YES;
    badge.layer.backgroundColor = [[NSColor colorWithCalibratedWhite:0 alpha:0.5] CGColor];
    badge.layer.cornerRadius = 4.0;
    badge.frame = NSMakeRect(6, NSHeight(frame) - 22, 72, 18);
    badge.autoresizingMask = NSViewMinYMargin;
    [container addSubview:badge];
    return container;
}

- (NSString *)text:(NSTextField *)field defaultValue:(NSString *)def {
    NSString *s = field.stringValue;
    s = [s stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
    return s.length > 0 ? s : def;
}

@end

#endif
