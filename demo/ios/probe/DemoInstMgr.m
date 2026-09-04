//
//  DemoInstMgr.m
//

#import "DemoInstMgr.h"

#import "DemoState.h"
#import "../rtc/RtcManager.h"

#if defined(XPROBE_USE_THUNDER)
#import "ThunderEngine.h"
#endif

@interface DemoInstMgr ()
@property (nonatomic, strong, nullable) id lastCustomArg;
@end

@implementation DemoInstMgr

- (id)getInstOfClass:(NSString *)className {
    if ([className isEqualToString:@"DemoState"] || [className hasSuffix:@".DemoState"]) {
        if (!self.state) {
            self.state = [[DemoState alloc] init];
        }
        return self.state;
    }
#if defined(XPROBE_USE_THUNDER)
    if ([className isEqualToString:@"RtcManager"] || [className hasSuffix:@".RtcManager"]) {
        return [RtcManager sharedInstance];
    }
    if ([className isEqualToString:@"ThunderEngine"] || [className hasSuffix:@"ThunderEngine"]) {
        return [RtcManager sharedInstance].engine;
    }
#endif
    return nil;
}

- (BOOL)isInstInitialize:(NSString *)className {
#if defined(XPROBE_USE_THUNDER)
    if ([className isEqualToString:@"ThunderEngine"] || [className hasSuffix:@"ThunderEngine"]) {
        return [RtcManager sharedInstance].isInitialized;
    }
#endif
    (void)className;
    return YES;
}

#if defined(XPROBE_USE_THUNDER)
- (void *)generateCustomType:(NSString *)typeName WithValue:(id)value {
    // ThunderVideoCanvas: param_value = [canvasType, uid]；0=本地 1=远端（与 Android DemoObjectManager 一致）
    if ([typeName hasSuffix:@"ThunderVideoCanvas"] || [typeName isEqualToString:@"ThunderVideoCanvas"]) {
        NSArray *params = nil;
        if ([value isKindOfClass:[NSArray class]]) {
            params = value;
        } else if ([value isKindOfClass:[NSString class]]) {
            params = [(NSString *)value componentsSeparatedByString:@","];
        }
        int canvasType = params.count > 0 ? [params[0] intValue] : 0;
        NSString *uid = params.count > 1 ? [NSString stringWithFormat:@"%@", params[1]] : @"";
        RtcManager *rtc = [RtcManager sharedInstance];
        NSView *view = (canvasType == 0) ? [rtc ensureLocalView] : [rtc ensureRemoteView];
        ThunderVideoCanvas *canvas = [[ThunderVideoCanvas alloc] init];
        canvas.view = view;
        canvas.renderMode = THUNDER_RENDER_MODE_ASPECT_FIT;
        canvas.uid = uid;
        self.lastCustomArg = canvas;
        return (__bridge void *)canvas;
    }
    return NULL;
}

- (NSString *)sdkPackageName {
    return @"ThunderEngine";
}
#endif

@end
