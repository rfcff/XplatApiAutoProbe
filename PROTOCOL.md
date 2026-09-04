# XplatApiAutoProbe 线协议规范 (v1.0)

本规范是 XplatApiAutoProbe 各平台实现（C++ Core / Android / iOS）与测试客户端之间的统一契约，
三端帧格式与命令语义一致，测试脚本可跨端复用。各端实现差异见 **第 7 节**。

## 1. 传输层

- TCP，服务端默认监听端口 `9000`（可配置），绑定 `0.0.0.0`（即接受所有网络接口上的连接）。
- 服务端可接受多个客户端连接；`sendReturn/sendError/sendCallback` 默认发往最近活跃的连接。
- 传输层**无身份校验、无加密、无授权**，命令帧可反射调用本进程内任意类的任意方法。
  使用前提见 **第 8 节「安全约束」**。

## 2. 帧格式

每一帧的结构如下（大端序）：

```
+----------------+----------------------------+
| 4 字节长度头 N | N 字节 UTF-8 JSON / 文本   |
+----------------+----------------------------+
```

- 长度头为 4 字节 **big-endian** 无符号整数，不含头本身。
- 内容为 UTF-8 编码的 JSON 命令、或特殊文本（见第 3 节）。
- TCP 存在粘包/半包，收发双方必须按长度头切帧。
- 单帧 payload 上限 **32 MiB**（不含长度头），四端统一：
  C++ Core（`FrameCodec::kMaxFramePayload`）/ Android（`CmdDecoder.MAX_FRAME_LENGTH`）/
  iOS（`XPServerConn` 的 `kXPMaxFrameLength`）/ Python 客户端（`MAX_FRAME_PAYLOAD`）。
  超限视为协议错误：接收方断开该连接，客户端抛出协议错误。**不得单独调整某一端的取值。**

## 3. 特殊文本帧（非 JSON）

| 客户端发送 | 服务端行为 |
|---|---|
| `PING` | 回复 `PONG`（保活心跳） |
| `PONG` | 忽略 |
| `GET_API:<className>` | 通过反射枚举该类的公开方法，返回 JSON 数组：<br>`[{"api":"com.foo.Bar.add","param_name":["a","b"],"param_type":["int","int"]}, ...]`<br>不支持反射的平台（C++ Core）返回空数组 `[]` |

## 4. 命令帧（JSON）

一条命令帧可以是 **单个 JSON 对象** 或 **对象数组**。
实现方把数组中**连续且 `threadMode` 相同**的命令合并为一组，在同一执行器（后台线程池或主线程）内**按数组顺序依次执行**。
`threadMode` 发生变化时拆成新组；**组与组之间在 C++ Core / Android 上可能并发**，因此跨组的回包顺序**不保证**与命令顺序一致。
需要整帧有序时，请整帧使用同一 `threadMode`（iOS 因全程串行队列，整帧顺序更接近「全有序」，跨端脚本仍应按「同组有序」编写，不要依赖 iOS 的更强保证）。

**非法元素策略（严格）：**数组的每个元素都必须是 JSON 对象。出现非对象元素时，实现方必须
**整帧拒绝**——只回报一条 `{"type":"error","key":"parseError",...}`，本帧命令**全部不执行**；
不得跳过非法元素后继续执行其余命令。部分执行会留下半截副作用，使测试脚本的状态不可控。

> 已知残余差异：Android 的 `ProtocolUtil.msgToCommandList` 额外接受「对象文本字符串」元素，
> C++ Core 与 iOS 拒绝。跨平台测试脚本不应依赖此行为。

### 4.1 自定义调用（ver = 2）

业务方注册 `CustomInvocation` 实现后，由业务自行解析并分发：

```json
{
  "api": {
    "apiName": "createEngine",
    "params": { "appId": "123", "sceneId": 1 }
  },
  "threadMode": 1,
  "ver": 2
}
```

- `apiName`：方法名，直接传给 `CustomInvocation.callMethod(apiName, params)`。
- `params`：任意 JSON 对象（Map），原样透传。
- `ver`：整数 `2` 或字符串 `"2"` 均视为有效，三端一致（见 §7.3）。

### 4.2 反射方法调用（ver = 1 或缺省）

扁平形式（Android 风格，C++ / iOS 亦须接受）：

```json
{
  "api": "com.foo.Bar.addSubscribe",
  "param_name": ["room", "uid"],
  "param_type": ["String", "String"],
  "param_value": ["134", "2345"],
  "threadMode": 0
}
```

嵌套形式（iOS 风格，两种形式实现方都必须支持）：

```json
{
  "api": {
    "apiName": "com.foo.Engine.joinRoom:roomName:uid:",
    "paramName": ["token", "roomName", "uid"],
    "paramType": ["NSString", "NSString", "NSString"],
    "paramValue": ["1", "1", "1"]
  },
  "threadMode": 1,
  "ver": 1
}
```

规则：

1. `api` 字符串取最后一个 `.` 前为类名、其后为方法名；不含 `.` 时使用
   `ObjectManager.getSDKPackageName()` 提供的默认类名
   （iOS 对应 `XPBaseInstMgr` 的可选方法 `sdkPackageName`）。
   未提供默认类名时，不含 `.` 的 `api` 命令按解析错误上报。
   注意：该返回值在三端都直接作为**类名**使用，整个 `api` 字符串作为方法名。
2. 解析顺序：先尝试类静态方法 → 再从 `ObjectManager.getObject(className)` 取缓存实例调用实例方法。
3. `param_name/paramType/paramValue` 三个数组长度必须一致，否则返回解析错误。
4. 参数类型支持基础类型（`int/long/float/double/boolean/BOOL/String/NSString` 等），
   自定义类型交由 `ObjectManager.generateCustomType(typeName, paramList)` 构造；
   自定义类型的 `param_value` 用数组传构造参数。
5. C++ Core 无运行时反射：`ver != 2` 的命令不走反射，而是把 `apiRaw` 序列化后透传给
   `CustomInvocation`——Method 帧的 `apiRaw` 是 `api` 子对象，Field 帧是整条命令对象
   （见 §7.2）。Android / iOS 在 `ver != 2` 时按上方规则走反射调用。**跨平台测试脚本
   不应依赖 C++ Core 的这一分支。**

### 4.3 修改静态成员变量 / 属性

```json
{
  "field": "com.foo.DemoConfig",
  "param_name": ["mUid", "mChannelId"],
  "param_type": ["String", "String"],
  "param_value": ["234", "567"]
}
```

- Android：反射修改类的静态字段。
- iOS：属性名按 `set<属性名首字母大写>:` 规则反射调用 setter（实例经 `BaseInstMgr` 获取）。
- C++ Core：无运行时反射；`field` 帧按 §7.2 把整条命令对象序列化后透传给 `CustomInvocation`。
  **跨平台测试脚本不应依赖 C++ Core 真正改写了成员变量。**

### 4.4 threadMode（线程模式）

| 值 | 含义 |
|---|---|
| `0` 或缺省 | BACKGROUND：由工作线程池处理；与相邻同为 BACKGROUND 的命令合并为一组，组内同一线程顺序执行 |
| `1` | MAIN：投递到平台主线程执行（Android 主线程 Handler / iOS 主队列 / Windows 主窗口消息 / 桌面端注入的 MainThreadPoster）；与相邻同为 MAIN 的命令合并为一组，组内顺序执行 |

分组与跨组并发规则见 §4 开篇。跨端脚本若依赖整帧回包顺序，应整帧使用同一 `threadMode`。

## 5. 返回帧（服务端 → 客户端，JSON）

统一格式：

```json
{"type": "return",   "key": "com.foo.Bar.addSubscribe", "value": "0"}
{"type": "error",    "key": "parseError",               "value": "json parse failed"}
{"type": "callback", "key": "onConnectionStatus",       "value": "status: 0"}
```

| type | 说明 |
|---|---|
| `return` | 方法返回值。`void` 方法返回字符串 `"void"`；`null` 返回 `"null"`；其余取 `toString()` |
| `error` | 解析错误（key=`parseError`）或执行异常（key=方法名），value 为堆栈/描述 |
| `callback` | 被测 SDK 的异步回调。value 为文本或 JSON 字符串（`sendCallbackJson` 产生的对象序列化结果） |

## 6. 各平台公开 API（统一语义）

```java
// Android / C++ Core / iOS(XPTestMgr) 一致语义
start(CustomInvocation invocation)                        // 仅自定义调用
start(BaseObjectManager manager)                          // 仅反射调用
start(BaseObjectManager manager, CustomInvocation inv)    // 两者同时
stop()
sendReturn(String apiName, String result)
sendError(String key, String message)
sendCallback(String callbackName, String message)
sendCallbackJson(String callbackName, Map<String,Object> json)
```

各平台实际签名（语义一致，签名按平台命名习惯调整）：

| 协议语义 | Android | C++ Core | iOS (`XPTestMgr`) |
|---|---|---|---|
| `sendReturn` | `AutoTestRpcServer.sendReturn(String, String)` | `AutoTestMgr::sendReturn(key, value)` | `-sendReturn:value:` |
| `sendError` | `AutoTestRpcServer.sendError(String, String)` | `AutoTestMgr::sendError(key, msg)` | `-sendError:msg:` |
| `sendCallback` | `AutoTestRpcServer.sendCallback(String, String)` | `AutoTestMgr::sendCallback(key, value)` | `-sendCallback:info:` |
| `sendCallbackJson` | `AutoTestRpcServer.sendCallbackJson(String, Map)` | `AutoTestMgr::sendCallbackJson(key, JsonValue)` | `-sendCallbackJson:json:` |

- `sendCallbackJson` 的 `value` 为入参对象的 JSON 序列化字符串；三端在入参为 null / 序列化
  失败 / 非对象时均降级为 `{}`，保证回包始终是合法 JSON。
- iOS 的 `-sendCallback:infoDict:` 与 `-sendCallbackJson:json:` 行为等价，两者都继续保留；
  新代码应使用协议统一命名 `-sendCallbackJson:json:`。

## 7. 各端实现与行为差异

以下都是**当前实现**的事实。跨平台测试脚本不应依赖其中任何一端独有的行为。

### 7.1 传输层实现

| 端 | 实现 |
|---|---|
| C++ Core | `select()` 事件循环；POSIX 用 BSD socket，Windows 用 winsock2（`TcpServer`） |
| Android | `java.net.ServerSocket`：监听线程 `accept`，每个连接一条独立读线程（`AutoServer`） |
| iOS | `CFSocket` 监听 + `CFStreamCreatePairWithSocket` 读写流，专用 `NSThread` + RunLoop（`XPConnMgr` / `XPServerConn`） |

三端均零第三方依赖，帧格式完全一致（§2）。

### 7.2 `ver` 与自定义调用的分发

| 端 | `ver == 2` | `ver != 2` |
|---|---|---|
| C++ Core | `callMethod(apiName, params)` | `callMethod(apiRaw 的序列化串, 空对象)`（`apiRaw`：Method 帧为 `api` 子对象，Field 帧为整条命令对象） |
| Android | `callMethod(methodName, paramsMap)` | 反射调用（§4.2 规则 2） |
| iOS | `-callMethod:Params:` | 反射调用（§4.2 规则 2） |

C++ Core 的 `ver != 2` 分支把 `apiRaw` 序列化后塞进第一个参数、第二个参数传空对象，
因此该分支下 `CustomInvocation` 收到的是**文本**而非结构化参数。这是 C++ Core 独有行为；
需要跨端复用的调用请统一使用 `ver == 2`。

### 7.3 `ver` 取值判定

- 整数 `2` 与字符串 `"2"` 都判定为 `ver == 2`——**三端一致**。
- `ver` 缺失：C++ Core 记为 `0`、Android 记为 `1`、iOS 视为无值——三者都落在 `ver != 2`，行为一致。
- `ver` 存在但类型非法（既非数字也非字符串）：**三端不一致**。
  C++ Core 抛解析错误并拒绝该帧；Android 记错误日志后降级为 `1`；iOS 不报错，直接走 `ver != 2` 分支。
- `ver` 为 `1` 之外的其它合法数字（如 `3`）：Android 只在 `ver == 1` 时解析参数数组，
  其余值视同无参；C++ Core 与 iOS 只区分「是否为 2」，不按 `1` 单独分支。
  跨平台脚本应显式写 `ver`，不要依赖缺省值。

以上后两项是既有差异，未在本版统一；测试脚本不应依赖这些取值。

## 8. 安全约束（使用前提）

本协议的定位是**开发期自动化探针**，不是一个可对外暴露的服务。以下三条是当前实现
**有意保留**的既有行为，修改会破坏现有调试流程，因此不在协议层收紧；代价是下面这个
后果必须由集成方承担：

> **成功建立 TCP 连接的任何一端，都可以在宿主进程内执行任意可达的代码。**

| 现状 | 后果 |
|---|---|
| 监听 `0.0.0.0`（C++ / Android / iOS 三端一致） | 同一局域网内**任何设备**都能连上，不限于本机或 adb 转发 |
| 连接建立无握手、无身份校验、无加密 | 无需凭据；流量明文，可被中间人读取与篡改 |
| 反射调用（§4.2）无类名白名单 | `api` 可指向任何类，包括 `java.lang.Runtime.exec` / `NSClassFromString` 动态构造 |

因此：

1. **禁止在生产包（对外发布版本）中启用**，禁止在不可信网络（公网、公共 Wi-Fi、共享办公网）中使用。
2. 若必须在非隔离网络下使用，应在宿主进程外自行补齐访问控制（防火墙 / 只绑定 loopback / 端口转发）。

### 8.1 各端发布门禁现状

| 端 | 门禁 | 说明 |
|---|---|---|
| Android | **运行时默认拒绝** | 非 `debuggable` 构建调用任意 `start` 重载都会被拒绝并记错误日志，服务不启动。<br>确为受控内测场景时，须在 `start` **之前**调用 `AutoTestRpcServer.allowInReleaseBuild(true)` 显式放行。<br>该门禁同时覆盖 `start()` 与 `AutoTestService.onCreate()`——Service 默认 START_STICKY，<br>进程被杀后系统会重建 Service，只校验 `start()` 会被这条路径绕过。 |
| C++ Core | **无** | 无统一的 debuggable 判定，由集成方在调用 `AutoTestMgr::start()` 前自行把关。 |
| iOS | **无** | 同上，由集成方在调用 `-startTestWithPort:...` 前自行把关（例如用 `#if DEBUG` 包住启动代码）。 |

C++ / iOS 暂不加自动门禁，是因为两端都没有跨构建类型稳定可靠的 debuggable 判定
（iOS 的 `#if DEBUG` 只对源码编译生效，对已发布的 pod 二进制无意义），
加一个看似有用、实际可被绕过的门禁反而会给出错误的安全预期。

## 9. 变更记录

### v1.0 澄清（协议文本与实现的对齐，非破坏性）

| 项 | 原状况 | 现在 |
|---|---|---|
| 单帧 payload 上限 | C++ 64 MiB / Android 32 MiB / iOS 16 MiB / Python 64 MiB 四值并存 | 统一 32 MiB（§2） |
| 数组帧非法元素 | C++ / Android 整帧拒绝，iOS 跳过该元素继续执行 | 统一整帧拒绝（§4） |
| `sendCallbackJson` | 仅 Android 提供 | C++ Core 与 iOS 补齐（§6） |
| 不含 `.` 的 `api` | iOS 直接抛异常，无默认类名兜底 | iOS 走 `XPBaseInstMgr` 的 `sdkPackageName`（§4.2 规则 1） |
| Release 包发布门禁 | 全端无门禁，Release 包照常监听 | Android 加运行时 `FLAG_DEBUGGABLE` 门禁（可显式放行），并覆盖 Service 重建路径（§8.1） |
| 监听地址 / 反射范围 | 无安全说明 | **保持既有行为不变**（仍为 `0.0.0.0` + 反射无白名单），风险与前提写入 §8 |
| 第 7 节表述 | 以「旧版实现」为参照描述兼容性与传输层 | 改为只陈述**当前实现**的差异（§7.1 传输层 / §7.2 `ver` 分发 / §7.3 `ver` 取值） |
| 同帧命令顺序 | 写「按顺序、在同一线程依次执行」 | 收敛为「连续相同 `threadMode` 组内顺序；组间 C++/Android 可能并发」（§4 / §4.4） |
| `field` 平台范围 | 写「Android/PC 反射改字段」 | 明确仅 Android / iOS 反射；C++ Core 透传给 `CustomInvocation`（§4.3） |
