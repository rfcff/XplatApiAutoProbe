//
//  DemoInvocation.mm
//

#import "DemoInvocation.h"

#import "../rtc/RtcManager.h"
#import "../../../ios/xprobe/XplatApiAutoProbe.h"

@implementation DemoInvocation

- (void)callMethod:(NSString *)apiName Params:(NSDictionary *)params {
    NSDictionary *p = params ?: @{};
    @try {
        [self dispatch:apiName params:p];
    } @catch (NSException *exception) {
        [[XPTestMgr sharedInstance] sendError:apiName
                                          msg:[NSString stringWithFormat:@"%@\n%@",
                                               exception.name, exception.reason ?: @""]];
    }
}

- (void)dispatch:(NSString *)api params:(NSDictionary *)params {
    RtcManager *rtc = [RtcManager sharedInstance];
    XPTestMgr *mgr = [XPTestMgr sharedInstance];

    if ([api isEqualToString:@"createEngine"]) {
        NSString *appId = [RtcManager safe:params[@"appId"] defaultValue:@"10034"];
        long long sceneId = [self longLong:params[@"sceneId"] defaultValue:0];
        long long cost = [rtc initializeWithAppId:appId sceneId:sceneId];
        [mgr sendReturn:api value:[NSString stringWithFormat:@"%lld", cost]];
    } else if ([api isEqualToString:@"destroyEngine"]) {
        [rtc deInitialize];
        [mgr sendReturn:api value:@"0"];
    } else if ([api isEqualToString:@"joinRoom"]) {
        NSString *room = [RtcManager safe:params[@"roomName"] defaultValue:@"82552971"];
        NSString *uid = [RtcManager safe:params[@"uid"] defaultValue:@"123456789"];
        NSString *token = params[@"token"];
        if ([token isKindOfClass:[NSNull class]]) {
            token = nil;
        }
        int ret = [rtc joinRoom:room uid:uid token:token];
        [mgr sendReturn:api value:[NSString stringWithFormat:@"%d", ret]];
    } else if ([api isEqualToString:@"leaveRoom"]) {
        int ret = [rtc leaveRoom];
        [mgr sendReturn:api value:[NSString stringWithFormat:@"%d", ret]];
    } else if ([api isEqualToString:@"addSubscribe"]) {
        NSString *room = [RtcManager safe:params[@"roomName"] defaultValue:rtc.roomName ?: @""];
        NSString *uid = [RtcManager safe:params[@"uid"] defaultValue:rtc.remoteUid];
        int ret = [rtc addSubscribe:room uid:uid];
        [mgr sendReturn:api value:[NSString stringWithFormat:@"%d", ret]];
    } else if ([api isEqualToString:@"removeSubscribe"]) {
        NSString *room = [RtcManager safe:params[@"roomName"] defaultValue:rtc.roomName ?: @""];
        NSString *uid = [RtcManager safe:params[@"uid"] defaultValue:rtc.remoteUid];
        int ret = [rtc removeSubscribe:room uid:uid];
        [mgr sendReturn:api value:[NSString stringWithFormat:@"%d", ret]];
    } else if ([api isEqualToString:@"startLocalPreview"]) {
        int ret = [rtc startLocalPreview];
        [mgr sendReturn:api value:[NSString stringWithFormat:@"%d", ret]];
    } else if ([api isEqualToString:@"stopLocalPreview"]) {
        int ret = [rtc stopLocalPreview];
        [mgr sendReturn:api value:[NSString stringWithFormat:@"%d", ret]];
    } else if ([api isEqualToString:@"setupRemoteVideo"]) {
        NSString *uid = [RtcManager safe:params[@"uid"] defaultValue:rtc.remoteUid];
        int ret = [rtc setupRemoteVideo:uid];
        [mgr sendReturn:api value:[NSString stringWithFormat:@"%d", ret]];
    } else if ([api isEqualToString:@"getState"]) {
        NSString *state = [NSString stringWithFormat:@"init=%@, room=%@, uid=%@, remoteUid=%@",
                           rtc.isInitialized ? @"YES" : @"NO",
                           rtc.roomName ?: @"",
                           rtc.uid ?: @"",
                           rtc.remoteUid];
        [mgr sendReturn:api value:state];
    } else if ([api isEqualToString:@"echo"]) {
        [mgr sendReturn:api value:[NSString stringWithFormat:@"echo api=%@ params=%@", api, params]];
    } else if ([api isEqualToString:@"scheduleCallback"]) {
        NSString *name = [RtcManager safe:params[@"name"] defaultValue:@"onProbeCallback"];
        NSString *info = [RtcManager safe:params[@"info"] defaultValue:@"ok"];
        long long delayMs = [self longLong:params[@"delayMs"] defaultValue:200];
        [mgr sendReturn:api value:@"0"];
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(delayMs * NSEC_PER_MSEC)),
                       dispatch_get_main_queue(), ^{
            [mgr sendCallback:name info:info];
        });
    } else {
        [mgr sendError:api msg:[NSString stringWithFormat:@"unknown api: %@", api]];
    }
}

- (long long)longLong:(id)value defaultValue:(long long)def {
    if (value == nil || value == [NSNull null]) {
        return def;
    }
    if ([value isKindOfClass:[NSNumber class]]) {
        return [(NSNumber *)value longLongValue];
    }
    return [[NSString stringWithFormat:@"%@", value] longLongValue] ?: def;
}

@end
