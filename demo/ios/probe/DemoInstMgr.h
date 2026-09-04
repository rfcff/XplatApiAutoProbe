//
//  DemoInstMgr.h
//  XplatApiAutoProbe iOS demo — XPBaseInstMgr 最小实现
//

#import <Foundation/Foundation.h>

#import "../../../ios/xprobe/XPBaseInstMgr.h"

NS_ASSUME_NONNULL_BEGIN

@class DemoState;

@interface DemoInstMgr : NSObject <XPBaseInstMgr>

@property (nonatomic, strong) DemoState *state;

@end

NS_ASSUME_NONNULL_END
