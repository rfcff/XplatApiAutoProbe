//
//  AppController.mm
//

#if !defined(XPROBE_HEADLESS_ONLY)

#import "AppController.h"

#import "../../../ios/xprobe/XplatApiAutoProbe.h"
#import "MainView.h"
#import "probe/DemoInstMgr.h"
#import "rpc/DemoInvocation.h"

static NSString *DemoLogLevelName(XPLogLevel level) {
    switch (level) {
        case XPLogLevelVerbose: return @"VERBOSE";
        case XPLogLevelDebug:   return @"DEBUG";
        case XPLogLevelInfo:    return @"INFO";
        case XPLogLevelWarn:    return @"WARN";
        case XPLogLevelError:   return @"ERROR";
    }
    return @"UNKNOWN";
}

@implementation AppController

+ (void)startProbeServices {
    static DemoInvocation *invocation = nil;
    static DemoInstMgr *instMgr = nil;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        XPLogSetHandler(^(XPLogLevel level, NSString *tag, NSString *message) {
            NSLog(@"[XplatApiAutoProbe demo][%@][%@] %@",
                  DemoLogLevelName(level), tag, message);
        });
        invocation = [[DemoInvocation alloc] init];
        instMgr = [[DemoInstMgr alloc] init];
        [[XPTestMgr sharedInstance] startTestWithInstMgr:instMgr CustInvoc:invocation];
    });
}

- (void)applicationDidFinishLaunching:(NSNotification *)notification {
    (void)notification;
    [AppController startProbeServices];

    NSRect frame = NSMakeRect(0, 0, 720, 780);
    NSWindow *window = [[NSWindow alloc] initWithContentRect:frame
                                                   styleMask:(NSWindowStyleMaskTitled |
                                                              NSWindowStyleMaskClosable |
                                                              NSWindowStyleMaskMiniaturizable |
                                                              NSWindowStyleMaskResizable)
                                                     backing:NSBackingStoreBuffered
                                                       defer:NO];
    window.title = @"XplatApiAutoProbe iOS Demo";
    MainView *mainView = [[MainView alloc] initWithFrame:frame];
    window.contentView = mainView;
    [window center];
    [window makeKeyAndOrderFront:nil];
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)sender {
    (void)sender;
    return YES;
}

@end

#endif
