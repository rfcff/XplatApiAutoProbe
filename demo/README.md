# 测试工程（demo）

各平台的测试工程统一放在这里，按平台分目录。每个工程都是可直接构建运行的宿主程序，
启动后监听 `0.0.0.0:9000`，用 [client/](../client) 下的 Python 客户端或任意 TCP 客户端按
[PROTOCOL.md](../PROTOCOL.md) 联调。

三端 demo **测试面已对齐**（与 `demo/android` 一致）：

| 能力 | Android | iOS / macOS | PC |
|---|---|---|---|
| ver=2 RTC API（createEngine / joinRoom / subscribe / preview / getState …） | ✅ Thunder | ✅ 模拟器；`XPROBE_USE_THUNDER=1` 接真实 Thunder | ✅ Windows 默认 `third_party/thunderbolt`；`-DXPROBE_USE_THUNDER=OFF` 为模拟器 |
| echo / scheduleCallback 协议探针 | ✅ | ✅ | ✅ |
| ver=1 反射 + field | ✅ | ✅ DemoCalc / DemoState；Thunder 模式下含 Engine/Canvas | ❌ C++ 无反射（透传 legacyApi） |
| 手动 UI | Material | AppKit | 纯 Win32（仅 Windows） |
| Headless / CI | adb 起 App | `--headless`（模拟器） | `--headless` |

参考实现（真机 Thunder 接线）：

- iOS：本仓库 `demo/ios` 已支持可选真实 Thunder（见 [`ios/README.md`](ios/README.md)）；完整 UI 面参考 `ThunderBoltDemo-ios` 分支 `ThunderBoltDemo-ios_3.12.0_maint`
- PC：本仓库 `demo/pc` 默认链接 `third_party/thunderbolt`（3.5.10，见 [`pc/README.md`](pc/README.md)）；完整 MFC 面参考 `ThunderBoltDemo-pc`

Android 始终真 Thunder；iOS 默认模拟器；PC（Windows）默认真实 SDK（头文件 + lib + `release/<arch>` DLL 已入库）。

### 可选真实 Thunder（iOS）/ PC SDK 开关

| 平台 | 开关 | 说明 |
|---|---|---|
| [ios/](ios) | `XPROBE_USE_THUNDER=1 ./build.sh` + `XPROBE_THUNDER_LDFLAGS` | macOS 宿主；需头文件与可链接库，见 [ios/README.md](ios/README.md) |
| [pc/](pc) | 默认 `ON`（仅 Windows）；`-DXPROBE_USE_THUNDER=OFF` 关 SDK | 链接 `thunderbolt.lib`，runtime 来自 `third_party/thunderbolt/release/<arch>`，见 [pc/README.md](pc/README.md) |

完整 UI / API 面仍以 ThunderBoltDemo 参考仓为准；本仓库只接 Android 对齐的最小 RTC 面。

| 目录 | 平台 | 构建方式 | 产物 / 入口 |
|---|---|---|---|
| [pc/](pc) | Windows（Thunder）；macOS / Linux 仅模拟器+无 UI | CMake + 纯 Win32 UI | `pc/build/Debug/xprobe_demo.exe [--headless]` |
| [android/](android) | Android | Gradle（AGP 8.x） | `app` 模块，直接引用 `android/xprobe` |
| [ios/](ios) | iOS / macOS | clang（`ios/build.sh`；可选 Thunder） | `ios/build/xprobe_ios_demo [--headless]` |

## 快速开始

```bash
# PC（推荐 x64）
cmake -S demo/pc -B demo/pc/build -A x64
cmake --build demo/pc/build --config Debug
./demo/pc/build/Debug/xprobe_demo.exe --headless   # CI
./demo/pc/build/Debug/xprobe_demo.exe              # Win32 UI

# iOS（macOS 宿主）
./demo/ios/build.sh
./demo/ios/build/xprobe_ios_demo --headless
./demo/ios/build/xprobe_ios_demo         # AppKit UI

# Android：用 Android Studio 打开 demo/android 目录，或命令行
cd demo/android && ./gradlew :app:assembleDebug
```

> Android demo 若改用 `assembleRelease` 构建，服务**不会启动**——非 `debuggable` 构建被
> [发布门禁](../PROTOCOL.md#81-各端发布门禁现状) 默认拒绝。确需验证 Release 行为时，
> 在 `DemoApplication` 的 `start` 之前加一行 `AutoTestRpcServer.allowInReleaseBuild(true);`。

## 自动化回归

在仓库根目录执行，脚本会自动构建对应平台的宿主程序并跑完用例：

```bash
python3 client/test_client.py           # PC（headless）
python3 client/test_client_ios.py       # iOS macOS 宿主（headless）
python3 client/test_client_android.py   # Android（需 adb 设备）

# C++ Core 自身的单元/集成测试（位于 core/test）
cmake -S core -B core/build && cmake --build core/build && ctest --test-dir core/build
```

`test_client_android.py` 会自动：`assembleDebug` → `adb install` → 启动 `MainActivity` →
`adb forward` 本机端口（默认 19000）到设备 9000 → 跑协议用例。多设备时设置
`XPROBE_ANDROID_SERIAL`；跳过构建设 `XPROBE_ANDROID_SKIP_BUILD=1`。
demo APK 仅含 `armeabi-v7a` / `arm64-v8a`，x86 模拟器无法安装。

## 目录约定

- 平台库源码不放这里：`core/`、`android/`、`ios/` 是三端库实现，`demo/` 只放各自的宿主/测试工程。
- 构建产物一律输出到各工程自己的 `build/` 目录，不污染源码树。
