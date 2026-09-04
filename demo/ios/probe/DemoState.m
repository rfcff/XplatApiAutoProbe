//
//  DemoState.m
//

#import "DemoState.h"

@implementation DemoState

- (NSString *)describe {
    return [NSString stringWithFormat:@"uid=%@ channelId=%@",
            self.uid ?: @"", self.channelId ?: @""];
}

@end
