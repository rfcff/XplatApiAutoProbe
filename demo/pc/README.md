# PC Demo（`demo/pc`）

与 [Android demo](../android) **测试面对齐**的桌面宿主：同一套 ver=2 RPC 命令、同一
`RtcManager` 公共操作与 SDK 风格回调，便于 Python 客户端跨端回归。

> 默认使用 **RtcManager 模拟器**（无需 Thunder 二进制）。Windows 上可通过
> `-DXPROBE_USE_THUNDER=ON` 接入真实 Thunder（运行时 `LoadLibrary`，参考
> `ThunderBoltDemo-pc_3.12.0_mb`）。

## 快速开始

```bash
cmake -S demo/pc -B demo/pc/build
cmake --build demo/pc/build

# 图形界面（Dear ImGui + GLFW）
./demo/pc/build/xprobe_demo

# CI / 无窗口：仅 RPC 服务 + Ctrl+C 退出
./demo/pc/build/xprobe_demo --headless
# 或
XPROBE_PC_HEADLESS=1 ./demo/pc/build/xprobe_demo
```

关闭 UI 编译（纯 headless 二进制，体积更小）：

```bash
cmake -S demo/pc -B demo/pc/build -DXPROBE_PC_UI=OFF
cmake --build demo/pc/build
```

## 可选：真实 Thunder SDK（Windows）

与 iOS demo 相同思路：编译期开关 + 运行时加载私有 SDK，默认 CI 仍走模拟器。

```bash
cmake -S demo/pc -B demo/pc/build-thunder -DXPROBE_USE_THUNDER=ON ^
  -DXPROBE_THUNDER_INCLUDE=C:\path\to\thunder\win\src\include
cmake --build demo/pc/build-thunder
```

未指定 `XPROBE_THUNDER_INCLUDE` 时，会尝试同级目录
`../thunder/thunder/win/src/include`（相对本仓库根）。

运行时将 `thunderbolt.dll`（或 `thunder.dll`）放到 PATH，或设置：

```bat
set XPROBE_THUNDER_DLL=C:\path\to\thunderbolt.dll
```

实现要点（对齐 Android / ThunderBolt PC）：

- `LoadLibrary` + `createEngine` → `initialize` / `destroyEngine`
- `joinRoom`（可选 `token`）/ `leaveRoom` / `addSubscribe` / `removeSubscribe`
- `setLocalVideoCanvas` + `enableLocalVideoCapture` + `startVideoPreview`
- `setRemoteVideoCanvas`；UI 模式下用 GLFW 窗口 HWND 作渲染目标
- `IThunderEventHandler` 回调转发到 `AutoTestMgr::sendCallback`

macOS / Linux 上开启 `XPROBE_USE_THUNDER` 会直接 CMake 失败（`IThunderEngine` 为 Win32 API）。

## RPC 命令表（ver=2，`DemoInvocation`）

与 Android `DemoInvocation` 一致：

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
| `setupRemoteVideo` | `{uid}` | 错误码 |
| `getState` | `{}` | `init=..., room=..., uid=..., remoteUid=...` |
| `echo` | 任意 | 回显 apiName 与 params |
| `scheduleCallback` | `{name, info, delayMs}` | 先 `"0"`，延迟后 `sendCallback` |
| 未知 | — | `error` 帧 |

`joinRoom` 成功后模拟器约 200ms 上报 `onJoinRoomSuccess` / `onConnectionStatus`，约
350ms 上报 `onUserJoined`（远端 uid `987654321`），便于订阅 UI 联调。

## C++ Core 与反射

C++ Core **没有** Java 式运行时反射：

- `GET_API` 始终返回空列表 `[]`。
- **ver=1 方法 / field 命令**仍透传到 `DemoInvocation::callMethod`（api 子对象 JSON
  串），并以 `legacyApi` 回显，供 [PROTOCOL.md](../../PROTOCOL.md) 协议回归。
- 不存在 Android 的 `DemoCalc` / `DemoState` 字段反射；请勿在 PC 端期望
  `DemoCalc.add` 或 field 读写生效。

## 目录结构

```
demo/pc/
├── main.cpp              # 日志回调、AutoTestMgr、UI/headless 分支
├── rpc/DemoInvocation.*  # ver=2 命令分发
├── rtc/RtcManager.*      # RTC 模拟器 / 可选 Thunder + sendCallback
├── ui/DemoUi.cpp         # ImGui 界面（XPROBE_PC_UI=ON）
└── CMakeLists.txt        # 链接 ../../core，FetchContent glfw/imgui
```

## 自动化测试

仓库根目录：

```bash
python3 client/test_client.py           # PC 协议一致性
python3 client/test_client_android.py   # Android 端到端
```

Headless CI 推荐：

```bash
./demo/pc/build/xprobe_demo --headless &
python3 client/test_client.py
```
