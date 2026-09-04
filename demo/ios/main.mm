//
//  main.mm
//  XplatApiAutoProbe iOS demo 入口
//
//  支持 GUI（AppKit）与 headless（--headless 或 XPROBE_HEADLESS=1）两种模式。
//

#import <Foundation/Foundation.h>

#import "../../ios/xprobe/XplatApiAutoProbe.h"
#import "probe/DemoInstMgr.h"
#import "rpc/DemoInvocation.h"

#if !defined(XPROBE_HEADLESS_ONLY)
#import <Cocoa/Cocoa.h>
#import "AppController.h"
#endif

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

static void StartProbeServices(void) {
    XPLogSetHandler(^(XPLogLevel level, NSString *tag, NSString *message) {
        NSLog(@"[XplatApiAutoProbe demo][%@][%@] %@",
              DemoLogLevelName(level), tag, message);
    });

    DemoInvocation *invocation = [[DemoInvocation alloc] init];
    DemoInstMgr *instMgr = [[DemoInstMgr alloc] init];
    [[XPTestMgr sharedInstance] startTestWithInstMgr:instMgr CustInvoc:invocation];

    NSLog(@"[XplatApiAutoProbe demo] 服务已启动，监听端口 %ld",
          (long)XPTestMgr.defaultListenPort);
}

static BOOL IsHeadless(int argc, const char *argv[]) {
    const char *env = getenv("XPROBE_HEADLESS");
    if (env != NULL && (strcmp(env, "1") == 0 || strcasecmp(env, "true") == 0)) {
        return YES;
    }
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--headless") == 0) {
            return YES;
        }
    }
    return NO;
}

int main(int argc, const char *argv[]) {
    @autoreleasepool {
        BOOL headless = IsHeadless(argc, argv);

#if defined(XPROBE_HEADLESS_ONLY)
        headless = YES;
#endif

        if (headless) {
            StartProbeServices();
            NSLog(@"[XplatApiAutoProbe demo] headless 模式，按 Ctrl+C 退出");
            [[NSRunLoop mainRunLoop] run];
            return 0;
        }

#if !defined(XPROBE_HEADLESS_ONLY)
        [NSApplication sharedApplication];
        AppController *appController = [[AppController alloc] init];
        [NSApp setDelegate:appController];
        return NSApplicationMain(argc, argv);
#else
        StartProbeServices();
        [[NSRunLoop mainRunLoop] run];
        return 0;
#endif
    }
}
