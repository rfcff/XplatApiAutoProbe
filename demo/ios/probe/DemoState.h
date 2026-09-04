//
//  DemoState.h
//  XplatApiAutoProbe iOS demo — field 命令与实例方法反射目标
//

#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

@interface DemoState : NSObject

@property (nonatomic, copy) NSString *uid;
@property (nonatomic, copy) NSString *channelId;

- (NSString *)describe;

@end

NS_ASSUME_NONNULL_END
