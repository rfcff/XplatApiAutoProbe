# iOS / macOS 测试工程

XplatApiAutoProbe iOS 端 demo，功能与 [`demo/android`](../android) 对齐：

- **ver=2 RPC**（`DemoInvocation`）：`createEngine`、`destroyEngine`、`joinRoom`、`leaveRoom`、
  `addSubscribe`、`removeSubscribe`、`startLocalPreview`、`stopLocalPreview`、
  `setupRemoteVideo`、`getState`、`echo`、`scheduleCallback`
- **RtcManager**（默认模拟器）：无 Thunder 二进制依赖；`joinRoom` 后约 0.2s
  通过 `XPTestMgr sendCallback` 推送 `onJoinRoomSuccess`、`onConnectionStatus`，并模拟
  `onUserJoined`（远端 uid `999`）以便 UI 订阅按钮联调
- **RtcManager**（可选真实 Thunder）：`XPROBE_USE_THUNDER=1` 时走 `ThunderEngine`，API 名与
  Android 一致（`createEngine` / `setLocalVideoCanvas` + `startLocalVideoPreview` /
  `setRemoteVideoCanvas` 等），回调转发到 RPC；`joinRoom` 支持可选 `token`
- **ver=1 反射探针**（`probe/`）：`DemoCalc`、`DemoState`、`DemoInstMgr`；真实 Thunder 模式下
  `DemoInstMgr` 额外提供 `ThunderEngine` 实例与 `ThunderVideoCanvas` 构造（对齐 Android
  `DemoObjectManager`）
- **AppKit UI**（macOS）：控件与 Android `MainActivity` 一致（Init/Destroy/Join/Leave/Camera/Sub Audio/Sub Video）
- **RPC 服务**：`XPTestMgr` 监听 `0.0.0.0:9000`，`XPLogSetHandler` 日志格式
  `[XplatApiAutoProbe demo][LEVEL][tag] message`

## 构建与运行

```bash
./build.sh                              # macOS 宿主（模拟 RTC，含 AppKit UI）
./build/xprobe_ios_demo                 # GUI 模式
./build/xprobe_ios_demo --headless      # 无窗口，仅 RunLoop（CI / 自动化）
# 或
XPROBE_HEADLESS=1 ./build/xprobe_ios_demo
```

### 可选：真实 Thunder SDK

需本机已有 **Mac** Thunder 头文件与可链接库（参考 `ThunderBoltDemo-ios` 的
`pod 'thunder/thunderbolt_ai'`，或本地 `thunder/thunder/mac` 产物）：

```bash
XPROBE_USE_THUNDER=1 \
  XPROBE_THUNDER_INCLUDE=/path/to/thunder/mac/src/include \
  XPROBE_THUNDER_LDFLAGS='-F/path/to/Frameworks -framework Thunder' \
  ./build.sh
```

未设置 `XPROBE_THUNDER_INCLUDE` 时，会尝试自动探测同级目录
`../thunder/thunder/mac/src/include`（相对本仓库根）。未设置 `XPROBE_THUNDER_LDFLAGS`
时构建直接失败并打印指引。真实 Thunder **仅支持 `macosx`**（头文件依赖 `NSView`）。

iOS 交叉编译校验（headless-only，不链接 AppKit UI；不可与 `XPROBE_USE_THUNDER` 同开）：

```bash
./build.sh iphoneos
./build.sh iphonesimulator
```

产物：`build/xprobe_ios_demo`

## 目录结构

```
demo/ios/
  build.sh
  main.mm              # 入口：--headless / GUI
  AppController.mm     # NSApplicationDelegate
  MainView.mm          # AppKit 主界面
  rpc/DemoInvocation.* # ver=2 命令分发
  rtc/RtcManager.*     # RTC 封装（模拟 / 可选 Thunder）
  probe/               # ver=1 反射探针
```

## 依赖

- 库源码：`../../ios/xprobe/`
- macOS：`Foundation`、`AppKit`、`CoreGraphics`、`libc++`
- 默认无 Thunder；可选真实 SDK 见上文

## 自动化回归

```bash
./build.sh && ./build/xprobe_ios_demo --headless &
python3 client/test_client_ios.py
```

脚本会自动构建、以 headless 模式拉起 demo，并跑 10 例端到端用例（始终使用模拟 RTC）。

## 协议示例

| 发送帧 | 说明 |
|---|---|
| 文本 `PING` | 回复 `PONG` |
| 文本 `GET_API:DemoCalc` | 返回 `addLeft:right:` 等方法元数据 |
| `{"api":{"apiName":"echo","params":{"text":"hi"}},"ver":2}` | 回显 apiName 与 params |
| `{"api":"DemoCalc.addLeft:right:","param_type":["NSInteger","NSInteger"],"param_value":["1","2"],"ver":1}` | 反射类方法，回发 `"3"` |
| `{"field":"DemoState","param_name":["uid"],"param_type":["NSString"],"param_value":["234"]}` | 反射 setter |

完整协议见 [PROTOCOL.md](../../PROTOCOL.md)。
