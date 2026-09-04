# PC Demo（`demo/pc`）

与 [Android demo](../android) **测试面对齐**的桌面宿主：同一套 ver=2 RPC、同一
`RtcManager` 公共操作与 SDK 风格回调，便于 Python 客户端跨端回归。

Windows 默认 `XPROBE_USE_THUNDER=ON`，使用本仓库 [`third_party/thunderbolt`](third_party/thunderbolt)
（Thunderbolt **3.5.10** 头文件 + import lib + `release/<arch>` 运行时）。

## 快速开始

```bash
# 推荐 x64（与 third_party/thunderbolt/release/x64 对齐）
cmake -S demo/pc -B demo/pc/build -A x64
cmake --build demo/pc/build --config Debug

# 图形界面（纯 Win32，无第三方 UI 框架；可双击，无黑色控制台）
./demo/pc/build/Debug/xprobe_demo.exe

# CI / 无窗口（仅 RPC :9000）
./demo/pc/build/Debug/xprobe_demo.exe --headless
```

非 Windows 请加 `-DXPROBE_PC_UI=OFF`；此时若仍开 Thunder 会 CMake 失败（Thunder 仅 Windows）。

关闭真实 SDK、改用模拟器：

```bash
cmake -S demo/pc -B demo/pc/build -DXPROBE_USE_THUNDER=OFF
```

## Thunderbolt SDK（Windows）

路径约定见 [`third_party/thunderbolt/README.md`](third_party/thunderbolt/README.md)。

```
demo/pc/third_party/thunderbolt/
  include/                                      # 3.5.10 头文件
  lib/<variant>/release/{x86,x64}/thunderbolt.lib
  release/{x86,x64}/                            # 运行时 DLL / vnmodel
```

| CMake 变量 | 默认 |
|---|---|
| `XPROBE_THUNDER_INCLUDE` | `third_party/thunderbolt/include` |
| `XPROBE_THUNDER_LIB` | `third_party/thunderbolt/lib/thunderboltyy/release/<arch>/thunderbolt.lib` |
| `XPROBE_THUNDER_VARIANT` | `thunderboltyy`（可改 `thunderbolt`） |
| `XPROBE_THUNDER_BIN` | `third_party/thunderbolt/release/<arch>` |

构建后会把 `XPROBE_THUNDER_BIN` 下的 `*.dll` / `*.vnmodel` 拷到 exe 同目录；
x64 的 `VideoSdk_x64.dll` 会额外拷成 `VideoSdk.dll` 以兼容加载名。

实现：链接 `thunderbolt.lib`，调用 `createEngine()`（不再 `LoadLibrary`）。

**注意：** 头文件必须与 DLL 同为 **3.5.10** 虚表。勿换成仓库较新分支的
`IThunderEngine.h`，否则 Init / `getVideoDeviceMgr` / 枚举摄像头会直接崩溃。

### UI 操作提示

1. **Init SDK** → 仅初始化引擎（**不**自动枚举摄像头）  
2. **刷新** → 调用 `enumVideoDevices` 填入下拉框  
3. 下拉切换摄像头（预览中会停采后按新设备重启）  
4. **Camera ON** → 对选中 index 调用 `startVideoDeviceCapture` + 本地预览  
5. 关窗时先关 UI，再在消息循环退出后 `destroyEngine`（避免界面长时间卡住）

## RPC 命令表（ver=2，`DemoInvocation`）

| apiName | params | 返回值 |
|---|---|---|
| `createEngine` | `{appId, sceneId}` | 初始化耗时（ms 字符串） |
| `destroyEngine` | `{}` | `"0"` |
| `joinRoom` | `{roomName, uid, token?}` | 错误码 |
| `leaveRoom` | `{}` | 错误码 |
| `addSubscribe` | `{roomName, uid}` | 错误码 |
| `removeSubscribe` | `{roomName, uid}` | 错误码 |
| `startLocalPreview` | `{}` | 错误码 |
| `stopLocalPreview` | `{}` | 错误码 |
| `enumCameras` / `enumVideoDevices` | `{}` | JSON 数组 `[{index,name},…]` |
| `selectCamera` / `startVideoDeviceCapture` | `{index}` | 错误码 |
| `setupRemoteVideo` | `{uid}` | 错误码 |
| `getState` | `{}` | `init=..., room=..., uid=..., remoteUid=...` |
| `echo` | 任意 | 回显 apiName 与 params |
| `scheduleCallback` | `{name, info, delayMs}` | 先 `"0"`，延迟后 `sendCallback` |
| 未知 | — | `error` 帧 |

`XPROBE_USE_THUNDER=OFF` 时为模拟器：`joinRoom` 后约 200ms 上报
`onJoinRoomSuccess` / `onConnectionStatus`，约 350ms 上报 `onUserJoined`
（远端 uid `987654321`）。

## C++ Core 与反射

C++ Core **没有** Java 式运行时反射：

- `GET_API` 始终返回空列表 `[]`。
- **ver=1 方法 / field 命令**仍透传到 `DemoInvocation`，并以 `legacyApi` 回显，供
  [PROTOCOL.md](../../PROTOCOL.md) 协议回归。
- 不存在 Android 的 `DemoCalc` / `DemoState` 字段反射。

## 目录结构

```
demo/pc/
├── main.cpp
├── rpc/DemoInvocation.*
├── rtc/RtcManager.*
├── ui/DemoUi.*                 # 纯 Win32 UI
├── third_party/thunderbolt/    # 3.5.10 SDK（见该目录 README）
└── CMakeLists.txt
```

Windows 下 `core/include/third_party/pthread` 提供 vendoring 的 `pthread.h`
（经 `xprobe` PUBLIC include）。当前仅头文件。

## 自动化测试

仓库根目录：

```bash
python3 client/test_client.py           # PC 协议一致性
python3 client/test_client_android.py   # Android 端到端
```

Headless CI：

```bash
cmake -S demo/pc -B demo/pc/build -A x64
cmake --build demo/pc/build --config Debug
./demo/pc/build/Debug/xprobe_demo.exe --headless &
python3 client/test_client.py
```
