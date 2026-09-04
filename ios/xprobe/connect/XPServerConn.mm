//
//  XPServerConn.mm
//  XplatApiAutoProbe —— iOS 统一测试 RPC 库
//
//  单个客户端连接的实现：CFStream + RunLoop 收发、
//  粘包/半包切帧、特殊文本帧（PING/PONG/GET_API）处理。
//

#import "XPServerConn.h"
#import "XPReflect.h"
#import "XPLog.h"

#import <sys/socket.h>
#import <netinet/in.h>
#import <arpa/inet.h>

// 帧头长度：4 字节大端无符号整数（不含头本身）
static const NSUInteger kXPFrameHeaderLength = 4;
// 单帧 payload 上限（防御异常长度头撑爆内存）。
// 32MB 为 PROTOCOL.md §2 规定的跨端统一值，各平台必须一致，不得单独调整。
static const NSUInteger kXPMaxFrameLength = 32 * 1024 * 1024;
// 单次读缓冲大小
static const NSUInteger kXPReadBufferSize = 4096;

// 特殊文本帧
static NSString * const kXPPingText = @"PING";
static NSString * const kXPPongText = @"PONG";
static NSString * const kXPGetApiPrefix = @"GET_API:";

#pragma mark - CF 上下文持有 Objective-C 对象（ARC 安全）

// 让 CFStreamClientContext 以 +1 方式持有 ObjC 对象（新版 SDK 回调参数为 void *）
static void *XPStreamContextRetain(void *info) {
    return (void *)CFBridgingRetain((__bridge id)(info));
}

// 与 XPStreamContextRetain 配对，释放持有的 ObjC 对象
static void XPStreamContextRelease(void *info) {
    if (info) {
        (void)CFBridgingRelease(info);
    }
}

#pragma mark - XPServerConn 私有声明（置于 C 回调之前，保证方法可见性）

@interface XPServerConn ()
{
    CFReadStreamRef _readStream;    // 读流（仅 runLoopThread 访问）
    CFWriteStreamRef _writeStream;  // 写流（仅 runLoopThread 访问）
}

@property (nonatomic, strong) NSMutableData *readBuffer;     // 读缓冲（仅 runLoopThread 访问）
@property (nonatomic, strong) NSMutableData *pendingWrites;  // 发送缓冲（跨线程，配合 sendLock 访问）
@property (nonatomic, strong) NSLock *sendLock;              // 保护 pendingWrites
@property (nonatomic, strong) NSThread *runLoopThread;       // 流所属 RunLoop 线程
@property (nonatomic, weak, nullable) id<XPServerConnDelegate> delegate;
@property (nonatomic, assign) BOOL closed;                   // 仅 runLoopThread 修改

// 读事件处理：有数据可读 / 流错误 / 对端关闭
- (void)onReadStreamEvent:(CFStreamEventType)eventType;

// 写事件处理：可继续写入 / 流错误
- (void)onWriteStreamEvent:(CFStreamEventType)eventType;

@end

#pragma mark - CFStream 事件回调（在 runLoopThread 的 RunLoop 上触发）

// 读事件回调：有数据可读 / 流错误 / 对端关闭
static void XPReadStreamCallback(CFReadStreamRef stream, CFStreamEventType eventType, void *info) {
    (void)stream; // 未使用：流引用由连接对象持有
    XPServerConn *conn = (__bridge XPServerConn *)(info);
    [conn onReadStreamEvent:eventType];
}

// 写事件回调：可继续写入 / 流错误
static void XPWriteStreamCallback(CFWriteStreamRef stream, CFStreamEventType eventType, void *info) {
    (void)stream; // 未使用：流引用由连接对象持有
    XPServerConn *conn = (__bridge XPServerConn *)(info);
    [conn onWriteStreamEvent:eventType];
}

@implementation XPServerConn

- (instancetype)initWithNativeHandle:(int)handle
                       runLoopThread:(NSThread *)runLoopThread
                             delegate:(id<XPServerConnDelegate>)delegate {
    self = [super init];
    if (self) {
        _nativeHandle = handle;
        _runLoopThread = runLoopThread;
        _delegate = delegate;
        _readBuffer = [NSMutableData data];
        _pendingWrites = [NSMutableData data];
        _sendLock = [[NSLock alloc] init];
        _closed = NO;
    }
    return self;
}

- (void)openOnCurrentRunLoop {
    NSAssert(self->_readStream == NULL, @"连接流已创建，不可重复打开");

    CFReadStreamRef readStream = NULL;
    CFWriteStreamRef writeStream = NULL;
    // 用原生 socket 句柄创建读写流对（引用计数 +1，由本对象负责 CFRelease）
    CFStreamCreatePairWithSocket(kCFAllocatorDefault, self.nativeHandle, &readStream, &writeStream);
    if (!readStream || !writeStream) {
        XPLog(XPLogLevelError, @"conn", @"创建读写流失败，关闭本连接");
        if (readStream) { CFRelease(readStream); }
        if (writeStream) { CFRelease(writeStream); }
        close(self.nativeHandle);
        [self close];
        return;
    }

    // 流关闭时由流负责关闭底层 socket，避免句柄泄漏
    CFReadStreamSetProperty(readStream, kCFStreamPropertyShouldCloseNativeSocket, kCFBooleanTrue);
    CFWriteStreamSetProperty(writeStream, kCFStreamPropertyShouldCloseNativeSocket, kCFBooleanTrue);

    // 注册流事件回调（context 持有 self）
    CFStreamClientContext context = {0, (__bridge void *)self, XPStreamContextRetain, XPStreamContextRelease, NULL};
    CFReadStreamSetClient(readStream,
                          kCFStreamEventHasBytesAvailable | kCFStreamEventErrorOccurred | kCFStreamEventEndEncountered,
                          XPReadStreamCallback, &context);
    CFWriteStreamSetClient(writeStream,
                           kCFStreamEventCanAcceptBytes | kCFStreamEventErrorOccurred,
                           XPWriteStreamCallback, &context);

    // 挂到当前 RunLoop（即 runLoopThread 的 RunLoop）并打开
    CFRunLoopRef runLoop = CFRunLoopGetCurrent();
    CFReadStreamScheduleWithRunLoop(readStream, runLoop, kCFRunLoopCommonModes);
    CFWriteStreamScheduleWithRunLoop(writeStream, runLoop, kCFRunLoopCommonModes);
    CFReadStreamOpen(readStream);
    CFWriteStreamOpen(writeStream);

    self->_readStream = readStream;
    self->_writeStream = writeStream;
}

- (void)close {
    if (self.closed) {
        return;
    }
    self.closed = YES;

    // 摘除 RunLoop、注销回调（释放 context 对 self 的持有）、关闭并释放流
    if (self->_readStream) {
        CFReadStreamUnscheduleFromRunLoop(self->_readStream, CFRunLoopGetCurrent(), kCFRunLoopCommonModes);
        CFReadStreamSetClient(self->_readStream, 0, NULL, NULL);
        CFReadStreamClose(self->_readStream);
        CFRelease(self->_readStream);
        self->_readStream = NULL;
    }
    if (self->_writeStream) {
        CFWriteStreamUnscheduleFromRunLoop(self->_writeStream, CFRunLoopGetCurrent(), kCFRunLoopCommonModes);
        CFWriteStreamSetClient(self->_writeStream, 0, NULL, NULL);
        CFWriteStreamClose(self->_writeStream);
        CFRelease(self->_writeStream);
        self->_writeStream = NULL;
    }

    // 丢弃尚未发出的数据
    [self.sendLock lock];
    [self.pendingWrites setLength:0];
    [self.sendLock unlock];

    [self.delegate serverConnDidDisconnect:self];
}

#pragma mark - 发送（任意线程可调）

- (void)sendData:(NSString *)data {
    if (self.closed) {
        return;
    }
    NSData *body = [data dataUsingEncoding:NSUTF8StringEncoding] ?: [NSData data];
    uint32_t header = htonl((uint32_t)body.length);
    NSMutableData *frame = [NSMutableData dataWithCapacity:kXPFrameHeaderLength + body.length];
    [frame appendBytes:&header length:kXPFrameHeaderLength];
    if (body.length > 0) {
        [frame appendData:body];
    }
    [self sendFrameData:frame];
}

// 帧数据入发送缓冲，并尽量投递到 RunLoop 线程冲刷
- (void)sendFrameData:(NSData *)frame {
    if (frame.length == 0 || self.closed) {
        return;
    }
    [self.sendLock lock];
    [self.pendingWrites appendData:frame];
    [self.sendLock unlock];

    if ([NSThread currentThread] == self.runLoopThread) {
        // 已在网络线程：直接冲刷
        [self flushPendingWrites];
    } else if (self.runLoopThread && !self.runLoopThread.finished) {
        // 其他线程：投递到网络线程冲刷，保证 CFWriteStream 只在单线程操作
        [self performSelector:@selector(flushPendingWrites)
                     onThread:self.runLoopThread
                   withObject:nil
                waitUntilDone:NO];
    }
}

// 尽量把发送缓冲写入网络（仅在 runLoopThread 上执行）；
// 内核缓冲写满时保留剩余数据，等待 CanAcceptBytes 事件继续
- (void)flushPendingWrites {
    [self.sendLock lock];
    while (self.pendingWrites.length > 0) {
        if (self->_writeStream == NULL || !CFWriteStreamCanAcceptBytes(self->_writeStream)) {
            break;
        }
        CFIndex written = CFWriteStreamWrite(self->_writeStream,
                                             (const UInt8 *)self.pendingWrites.bytes,
                                             self.pendingWrites.length);
        if (written < 0) {
            [self.sendLock unlock];
            XPLog(XPLogLevelWarn, @"conn", @"写流失败，关闭本连接");
            [self close]; // 写失败：连接已不可用
            return;
        }
        if (written > 0) {
            // 消费已写出部分（缓冲前移）
            [self.pendingWrites replaceBytesInRange:NSMakeRange(0, (NSUInteger)written)
                                         withBytes:NULL
                                             length:0];
        }
    }
    [self.sendLock unlock];
}

#pragma mark - 流事件处理（runLoopThread）

- (void)onReadStreamEvent:(CFStreamEventType)eventType {
    if (self.closed) {
        return;
    }
    if (eventType != kCFStreamEventHasBytesAvailable) {
        // 错误或对端关闭
        XPLog(XPLogLevelDebug, @"conn", @"读流结束或出错（事件 %lu），关闭本连接", (unsigned long)eventType);
        [self close];
        return;
    }

    // 尽量读完当前可读数据
    UInt8 buffer[kXPReadBufferSize];
    while (CFReadStreamHasBytesAvailable(self->_readStream)) {
        CFIndex bytesRead = CFReadStreamRead(self->_readStream, buffer, sizeof(buffer));
        if (bytesRead > 0) {
            [self.readBuffer appendBytes:buffer length:(NSUInteger)bytesRead];
        } else if (bytesRead < 0) {
            XPLog(XPLogLevelDebug, @"conn", @"读流出错，关闭本连接");
            [self close];
            return;
        } else {
            // 无数据（对端已关 Writing 半边时可能读到 0），等待 EndEncountered 事件
            break;
        }
    }

    // 任意数据到达即视为活跃连接
    [self.delegate serverConnDidReceiveData:self];

    [self processReadBuffer];
}

- (void)onWriteStreamEvent:(CFStreamEventType)eventType {
    if (self.closed) {
        return;
    }
    if (eventType == kCFStreamEventErrorOccurred) {
        [self close];
        return;
    }
    // kCFStreamEventCanAcceptBytes：继续冲刷发送缓冲
    [self flushPendingWrites];
}

#pragma mark - 切帧（正确处理 TCP 粘包/半包）

// 循环从读缓冲中按「4 字节大端长度头 + payload」切帧，直至数据不足一帧
- (void)processReadBuffer {
    while (self.readBuffer.length >= kXPFrameHeaderLength) {
        // 读帧头并转为主机序
        uint32_t networkLength = 0;
        [self.readBuffer getBytes:&networkLength length:kXPFrameHeaderLength];
        uint32_t bodyLength = ntohl(networkLength);

        if (bodyLength > kXPMaxFrameLength) {
            // 帧长度非法：判定为协议错误，断开连接
            XPLog(XPLogLevelError, @"conn", @"帧长度 %lu 超过上限 %lu，判定协议错误，断开连接",
                  (unsigned long)bodyLength, (unsigned long)kXPMaxFrameLength);
            [self close];
            return;
        }
        if (self.readBuffer.length < kXPFrameHeaderLength + (NSUInteger)bodyLength) {
            // 半包：剩余数据不足一帧，等待后续到达
            break;
        }

        // 取出 payload 并消费整个帧（缓冲前移）
        NSString *payload = nil;
        if (bodyLength > 0) {
            NSData *body = [self.readBuffer subdataWithRange:NSMakeRange(kXPFrameHeaderLength, bodyLength)];
            payload = [[NSString alloc] initWithData:body encoding:NSUTF8StringEncoding];
        }
        [self.readBuffer replaceBytesInRange:NSMakeRange(0, kXPFrameHeaderLength + (NSUInteger)bodyLength)
                                  withBytes:NULL
                                      length:0];

        if (payload.length > 0) {
            [self handlePayload:payload];
        }
        if (self.closed) {
            // 处理过程中连接可能被关闭（如协议错误）
            return;
        }
    }
}

// 分发单帧 payload：特殊文本帧就地处理，其余上抛为 JSON 命令
- (void)handlePayload:(NSString *)payload {
    // 心跳：回 PONG
    if ([payload isEqualToString:kXPPingText]) {
        XPLog(XPLogLevelVerbose, @"conn", @"收到 PING，回复 PONG");
        [self sendData:kXPPongText];
        return;
    }
    // 心跳：忽略
    if ([payload isEqualToString:kXPPongText]) {
        return;
    }
    // 枚举类方法列表
    if ([payload hasPrefix:kXPGetApiPrefix]) {
        NSString *className = [[payload substringFromIndex:kXPGetApiPrefix.length]
                stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
        XPLog(XPLogLevelDebug, @"conn", @"收到 GET_API，枚举类方法: %@", className);
        [self sendData:[XPReflect apiListJSONOfClassNamed:className]];
        return;
    }
    // JSON 命令帧
    [self.delegate serverConn:self didReceiveCommand:payload];
}

@end
