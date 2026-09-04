//
//  MainView.h
//  XplatApiAutoProbe iOS demo — AppKit 主界面（对齐 Android MainActivity）
//

#if !defined(XPROBE_HEADLESS_ONLY)

#import <Cocoa/Cocoa.h>

NS_ASSUME_NONNULL_BEGIN

@interface MainView : NSView

- (void)appendLog:(NSString *)msg;

@end

NS_ASSUME_NONNULL_END

#endif
