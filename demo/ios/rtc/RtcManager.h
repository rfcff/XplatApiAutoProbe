//
//  RtcManager.h
//  XplatApiAutoProbe iOS demo — RTC 封装
//
//  默认：模拟器（无 Thunder 二进制依赖，CI / headless 可用）
//  可选：编译时定义 XPROBE_USE_THUNDER，链接真实 Thunder SDK（见 build.sh / README）
//

#import <Foundation/Foundation.h>

#if !defined(XPROBE_HEADLESS_ONLY) || defined(XPROBE_USE_THUNDER)
#import <AppKit/AppKit.h>
#endif

#if defined(XPROBE_USE_THUNDER)
@class ThunderEngine;
#endif

NS_ASSUME_NONNULL_BEGIN

@interface RtcManager : NSObject

typedef void (^RtcUiLogBlock)(NSString *msg);

+ (instancetype)sharedInstance;

- (instancetype)init NS_UNAVAILABLE;
+ (instancetype)new NS_UNAVAILABLE;

@property (nonatomic, copy, nullable) RtcUiLogBlock uiLogger;

#if !defined(XPROBE_HEADLESS_ONLY)
- (void)setVideoContainers:(NSView *)localContainer remoteContainer:(NSView *)remoteContainer;
#endif

- (BOOL)isInitialized;
- (nullable NSString *)roomName;
- (nullable NSString *)uid;
- (NSString *)remoteUid;

#if defined(XPROBE_USE_THUNDER)
/** 真实引擎实例（未 createEngine 时为 nil）；供 ver=1 反射 */
@property (nonatomic, strong, readonly, nullable) ThunderEngine *engine;
/** 按需创建本地/远端渲染 NSView（挂到容器；headless 无容器时返回临时 view） */
- (nullable NSView *)ensureLocalView;
- (nullable NSView *)ensureRemoteView;
#endif

- (long long)initializeWithAppId:(NSString *)appId sceneId:(long long)sceneId;
- (void)deInitialize;

/** token 可空：模拟器忽略；真实 Thunder 传 nil/空串表示无鉴权进房 */
- (int)joinRoom:(NSString *)roomName uid:(NSString *)uid;
- (int)joinRoom:(NSString *)roomName uid:(NSString *)uid token:(nullable NSString *)token;
- (int)leaveRoom;

- (int)addSubscribe:(NSString *)channelId uid:(NSString *)uid;
- (int)removeSubscribe:(NSString *)channelId uid:(NSString *)uid;

- (int)setupRemoteVideo:(NSString *)uid;
- (int)startLocalPreview;
- (int)stopLocalPreview;

+ (NSString *)safe:(nullable NSString *)s defaultValue:(NSString *)def;

@end

NS_ASSUME_NONNULL_END
