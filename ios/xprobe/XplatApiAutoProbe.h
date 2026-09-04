//
//  XplatApiAutoProbe.h
//  XplatApiAutoProbe —— iOS 统一测试 RPC 库
//
//  总头文件：集成方仅需 #import <XplatApiAutoProbe/XplatApiAutoProbe.h>
//
//  快速上手：
//    // 1. 实现 XPCustomInvocation（ver = 2 自定义调用）与/或 XPBaseInstMgr（反射调用）；
//    // 2. 启动服务（默认端口 9000）：
//    [[XPTestMgr sharedInstance] startTestWithInstMgr:yourInstMgr CustInvoc:yourInvocation];
//    // 3. SDK 异步回调中回发数据：
//    [[XPTestMgr sharedInstance] sendCallback:@"onSomeEvent" info:@"..."];
//

#import <Foundation/Foundation.h>

FOUNDATION_EXPORT double XplatApiAutoProbeVersionNumber;
FOUNDATION_EXPORT const unsigned char XplatApiAutoProbeVersionString[];

// 测试管理单例（对外统一入口）
#import "XPTestMgr.h"

// 统一日志（回调注入，业务/测试用例接管内部日志）
#import "XPLog.h"

// 反射执行器（一般经由 XPTestMgr 间接使用）
#import "XPReflect.h"

// 命令分发器（一般经由 XPTestMgr 间接使用）
#import "XPCmdRunner.h"

// 网络层
#import "XPConnMgr.h"
#import "XPServerConn.h"

// 业务方实现的协议
#import "XPBaseInstMgr.h"
#import "XPCustomInvocation.h"

// 参数类型名常量
#import "XPTypeDefine.h"
