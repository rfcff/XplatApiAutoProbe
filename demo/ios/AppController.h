//
//  AppController.h
//  XplatApiAutoProbe iOS demo — 应用控制器
//

#if !defined(XPROBE_HEADLESS_ONLY)

#import <Cocoa/Cocoa.h>

NS_ASSUME_NONNULL_BEGIN

@interface AppController : NSObject <NSApplicationDelegate>

+ (void)startProbeServices;

@end

NS_ASSUME_NONNULL_END

#endif
