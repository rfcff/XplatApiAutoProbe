# autotest — 桌面端（PC）测试控制框架

> 仓库级架构图、与 `client/` / 三端探针的关系见根目录 [README.md「架构」](../README.md#架构)。

## 定位

本目录是**运行在开发机上的控制端**：它负责拉起被测 App、下发命令、等待回调、校验结果、汇总报告。
被测端（Android / PC / iOS）由各自进程内的**探针服务端**接收命令——本仓库对应实现为
`core/`（C++）/ `android/` / `ios/`，线协议见根目录 [PROTOCOL.md](../PROTOCOL.md)。

```
开发机（本目录，pytest 驱动）
   │
   ├── Android 被测端   adb forward + HTTP        ← 已实现
   ├── PC 被测端        直连 127.0.0.1:9000       ← 未实现
   └── iOS 被测端       iproxy / USB 隧道         ← 未实现
```

### 当前能力边界

**控制端框架是完整的，但被测端驱动目前只实现了 Android 一种。**

`multiTest/driver.py` 现用「`adb forward tcp:<host> tcp:<device>` + HTTP 明文接口」与设备通信，
命令由 Android 端的 Espresso 测试进程接收执行。PC / iOS 两端没有对应实现——
原 `sdkautotest` 仓库里也不存在，这不是本次迁移丢掉的。

要接 PC / iOS，扩展点在 `multiTest/driver.py` 的 `Driver` 类：它把「建立通道 / 发命令 / 收结果」
封装成了 `start()`、`step()`、`finish()` 三件事，替换底层传输即可，上层的用例编排与校验不用动。

## 目录结构

| 路径 | 职责 |
|---|---|
| `projectConfig.py` | **配置中心**。被测包名、内部服务地址、邮件等全部在这里，敏感项读环境变量 |
| `multiTest/driver.py` | 单设备驱动：拉起 App、发命令、收回调。接新被测端改这里 |
| `multiTest/multiCaseRun.py` | 用例执行编排：`step` / `verify` / `setUp` / `tearDown` 装饰器与结果记录 |
| `multiTest/multiCaseMgr.py` | 用例与设备的分配调度 |
| `multiTest/exception.py` | 异常类型定义 |
| `command/common.py` | 控制端与被测端约定的通用命令常量 |
| `command/sample/multiCommand.py` | 命令表示例——与某个具体被测端约定的命令清单，接入时替换为你自己的 |
| `utils/adbCommand.py` | adb 命令封装（装机、清日志、拉日志、端口转发） |
| `utils/deviceManage.py` | 设备池管理 |
| `utils/configFileManage.py` / `xmlUtil.py` | 账号与房间号等测试参数配置 |
| `utils/taskHelper1.py` | 内部任务系统对接 |
| `utils/util1.py` | 通用工具（日志、时间、网络） |
| `uiauto/` | UI 自动化（权限弹窗处理等） |
| `espressoControl.py` | 被测包安装与 Espresso 进程拉起 |
| `runCaseHelper.py` | 用例模板用到的 `setUp` / `getUsers` / `setupModule` |
| `tests/conftest.py` | pytest 钩子：报告定制、命令行参数 |
| `taskMgr.py` | IDE 调试入口 |

## 如何运行

### 1. 环境准备

原项目要求 Python 3.7 及以下（依赖 `reload(sys)`、`urllib2` 等 Python 2 兼容写法）。
若要在新版 Python 上运行，需先处理本文「已知问题」里列出的兼容项。

```bash
cd autotest
python3 -m venv .venv && source .venv/bin/activate   # Windows: .venv\Scripts\activate
pip install -r requirements.txt
```

被测 Android 设备通过 USB 连接并开启 USB 调试，`adb devices` 能识别即可。

### 2. 配置

所有与具体环境相关的取值集中在 `projectConfig.py`，**均可用环境变量覆盖**（见下一节「配置」表）。
最少需要设置被测包名与 Activity：

```bash
export XPROBE_CLIENT_PACKAGE=com.example.app
export XPROBE_TEST_PACKAGE=com.example.app.test
export XPROBE_MAIN_ACTIVITY=com.example.app/.MainActivity
export XPROBE_TEST_RUNNER=com.example.app.test/androidx.test.runner.AndroidJUnitRunner
```

### 3. 执行用例

本目录**未附带业务用例**（`tests/` 下目前只有 `conftest.py`）。
直接 `python taskMgr.py`（`CASE` 为空）会打印指引并以退出码 2 结束，**不会**去跑不存在的旧路径。

写好 pytest 用例后：

```bash
# 推荐：pytest 直接指定用例
pytest -s tests/p0/test_your_case.py::TestMultiCase::test_Foo \
  --devices=<did1>,<did2> \
  --appid=<appid> --uid1=<uid> --channel1=<channel> \
  --html=report.html

# 或：编辑 taskMgr.py 顶部的 CASE 常量指向真实用例节点后
python taskMgr.py
```

`tests/conftest.py` 支持的命令行参数：`--devices`、`--appid`、`--uid1/2/3`、`--channel1/2`。

## 配置

所有与具体环境相关的取值集中在 `projectConfig.py`，**均可用环境变量覆盖**：

| 环境变量 | 用途 |
|---|---|
| `XPROBE_CLIENT_PACKAGE` / `XPROBE_TEST_PACKAGE` | 被测 App 与测试包包名 |
| `XPROBE_MAIN_ACTIVITY` | 拉起用的 Activity 全限定名 |
| `XPROBE_TEST_RUNNER` | AndroidJUnitRunner 全限定名 |
| `XPROBE_APP_LOG_PATH` | 被测 App 在设备上的日志目录 |
| `XPROBE_APP_LOG_PATH_MAP` | monkey 测试用：包名 → 日志路径，JSON 格式 |
| `XPROBE_CLIENT_APK_PATTERN` / `XPROBE_CLIENT_APK_NAME` | 安装包匹配正则与文件名 |
| `XPROBE_CLIENT_APP_LABEL` | App 在系统设置中的显示名（UI 自动化按文本点击） |
| `XPROBE_SERVER_ADDRESS` / `XPROBE_WEB_DETAIL` | 内部任务系统地址 |
| `XPROBE_REPORT_URL` / `XPROBE_RESULT_URL` | 日志反馈页与结果上报接口 |
| `XPROBE_APP_BUILD_URL` | 构建产物下载地址 |
| `XPROBE_SHARE_PATH` / `XPROBE_REMOTE_IP` / `XPROBE_SHARE_USER` / `XPROBE_SHARE_PASS` | 共享目录与访问凭据 |
| `XPROBE_MAIL_*` | 邮件服务与收件人（`XPROBE_MAIL_TO` / `_CC` 用逗号分隔） |
| `XPROBE_UIAUTO_VIVO_DEVICES` / `XPROBE_UIAUTO_XIAOMI_DEVICES` | 需额外授予系统权限的设备序列号 |

## 接入 XplatApiAutoProbe

本仓库的 `client/xprobe_client.py` 已实现 XplatApiAutoProbe 的线协议
（TCP + 4 字节大端长度头 + JSON 帧，见仓库根 `PROTOCOL.md`），
三端（C++ Core / Android / iOS）服务端也都在本仓库内。

把本框架接到 XplatApiAutoProbe 上的路径：

1. `multiTest/driver.py` 的传输层从「HTTP + adb forward」换成 `client/xprobe_client.py` 的 TCP 客户端；
2. 通道建立方式按端区分：Android 仍走 `adb forward`，PC 直连 `127.0.0.1:<port>`，iOS 走 iproxy 或 USB 隧道；
3. `command/sample/multiCommand.py` 换成 XplatApiAutoProbe 的命令表（协议 §4 的 `api` / `param_name` 等字段）。

替换后三端可以共用同一套用例脚本，这也正是 XplatApiAutoProbe 的目标。

## 清洗说明

迁移时做了以下清洗，仓库内不含任何真实凭据、内部域名与个人邮箱：

- **作者信息**：删除 8 处 `author xxx` / `date xxx` 文件头文档串，以及 `uiauto/__init__.py` 的 `__author__` 变量。
- **凭据**：`projectConfig.py` 的邮件账号密码、共享目录账号密码，全部改为从环境变量读取，默认空值。
- **内部地址**：任务系统、报表平台、构建产物、日志共享目录的域名与内网 IP，改为占位符 `http://<task-server>/` 等。
- **个人信息**：收件人/抄送名单（十余个个人邮箱）移除，改为环境变量注入。
- **具体设备与包名**：硬编码的设备序列号、被测包名、其它 App 包名，改为可配置常量或配置化映射表，默认不生效。
- **命令表目录**：`command/thunderbolt/` 重命名为 `command/sample/`，表明它是范例而非本仓库的绑定依赖。

> ⚠️ **原 `sdkautotest` 仓库的 git 历史里仍保留着明文密码。**
> 迁移只保证本目录干净，历史不被改写。相关凭据应视为已泄露并尽快轮换。

## 已知问题

迁移时只做了搬运与清洗，**未做代码现代化**，以下问题是从原仓库带过来的：

- 依赖 Python 2 兼容写法：`reload(sys)`、`urllib2` 分支、`except:` 裸捕获。Python 3.12+ 会输出
  `SyntaxWarning: invalid escape sequence`（正则字符串未加 `r` 前缀），约 10 处。
- `requirements.txt` 锁定的版本较旧（pytest 5.4.3 等），在新版 Python 上未必能直接安装。
- `utils/util1.py`、`utils/taskHelper1.py` 中留有内部任务系统的对接代码，本仓库用不到，保留仅为框架完整。
- `uiauto/__init__.py` 含一段 1388 行的第三方 UI 自动化库代码副本，非本项目源码。
