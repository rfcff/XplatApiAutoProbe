# encoding=UTF-8
"""
框架配置中心。

所有与具体被测 App、内部服务相关的取值都集中在本文件，接入方按自己的环境填写。
仓库内不保存任何真实凭据、内部域名或个人邮箱——敏感项统一从环境变量读取。
"""

import json
import os
import threading

from utils import util1

# 本地调试可控制这些开关
useExistTask = False  # 使用存在的任务，重新跑  //todo
installFlag = True  # default True,download &install apk
uiautoFlag = True  # default True,install and use uiauto
hiidoFlag = True  # if true,get hiido report events
isDebugMode = True  # default False,本地调试时打开
pushConfigFile = True  # default True,本地调试时打开
lianMaiRetryTime = 2  # 连麦用例测试次数


def enum(**enums):
    return type('Enum', (), enums)


RESULT = enum(begin=0, success=1, installFailed=2, failed=3, timeout=4, crash=5, Cancel=6, )

TEST_SUIT = enum(P0_SUIT=0b0001, P1_SUIT=0b0010, BVT_SUIT=0b0100, BVT_P0_SUIT=0b0101, TICE_SUIT=0b1000, )

rootPath = "share"  # 存放运行日志的目录
configPath = "config"  # 存放配置文件的目录
resultPath = 'result'

# ==================================
# 被测应用（接入方按自己的 App 填写）
# ==================================
clientPackage = os.environ.get("XPROBE_CLIENT_PACKAGE", "<client-package>")
testPackage = os.environ.get("XPROBE_TEST_PACKAGE", "<client-package>.test")
mainActivity = os.environ.get("XPROBE_MAIN_ACTIVITY", "<client-package>.activity.MainActivity")

# Espresso / AndroidJUnitRunner 全限定名
testRunner = os.environ.get(
    "XPROBE_TEST_RUNNER",
    "<client-package>.test/android.support.test.runner.AndroidJUnitRunner")

# 被测 App 安装包：文件名匹配正则（从构建目录挑选 apk）与实际安装文件名
clientApkPattern = os.environ.get("XPROBE_CLIENT_APK_PATTERN", r"<client-app>\.apk")
clientApkName = os.environ.get("XPROBE_CLIENT_APK_NAME", "<client-app>.apk")
# App 在系统设置列表中的显示名（UI 自动化按文本点击时使用）
clientAppLabel = os.environ.get("XPROBE_CLIENT_APP_LABEL", "<client-app>")

# 被测 App 在设备上的日志目录
appLogPath = os.environ.get(
    "XPROBE_APP_LOG_PATH", "/sdcard/Android/data/<client-package>/cache/logs")

# monkey 测试用：包名 -> 该包在设备上的日志路径。
# 通过环境变量以 JSON 传入，例：
#   XPROBE_APP_LOG_PATH_MAP='{"com.example.app":"/sdcard/Android/data/com.example.app/cache/logs"}'
# 未配置的包不拉取应用日志。
appLogPathMap = json.loads(os.environ.get("XPROBE_APP_LOG_PATH_MAP", "{}"))

# ==================================
# 内部服务地址（接入方按自己的环境填写）
# ==================================
SERVER_ADDRESS = os.environ.get("XPROBE_SERVER_ADDRESS", "http://<task-server>/")
webDetail = os.environ.get("XPROBE_WEB_DETAIL", "http://<task-server>/task/detail?id=%s")
reportUrl = os.environ.get("XPROBE_REPORT_URL", "http://<report-server>/#/?appId=<app>&feedback=")
resultReportUrl = os.environ.get("XPROBE_RESULT_URL", "http://<result-server>/autoTest/apmAutotestResult/add")
appBuildUrl = os.environ.get("XPROBE_APP_BUILD_URL", "http://<repo-server>/<build-path>/")

# 日志/安装包共享目录（Windows UNC 路径与远端 IP）
sharePath = os.environ.get("XPROBE_SHARE_PATH", r"\\<share-host>\f")
remoteIP = os.environ.get("XPROBE_REMOTE_IP", "<share-host>")

# hiddo事件文件名
hiidoEventFolder = "hiidoevent"
missEventLog = "missingevent.txt"
execEventLog = "execevent.txt"
hiidoevent = threading.Event()

# if util1.isWindows():
#     localIP_share = util1.getLocalIP(0)
#     localIP_httpServer = localIP_share
# elif util1.isMac():
#     localIP_share = util1.get_Mac_ip()
#     localIP_httpServer = localIP_share
# else:
#     localIP_share = util1.getLocalIP("eth0")
#     localIP_httpServer = util1.getLocalIP("eth1")

httpServerPort = 8080

# ==================================
# email info
# ==================================
# 全部从环境变量读取，仓库内不保存凭据与收件人名单。
#   XPROBE_MAIL_HOST / XPROBE_MAIL_USER / XPROBE_MAIL_PASS / XPROBE_MAIL_FROM / XPROBE_MAIL_POSTFIX
#   XPROBE_MAIL_TO / XPROBE_MAIL_CC（多个地址用逗号分隔）
mail_host = os.environ.get("XPROBE_MAIL_HOST", "<mail-host>")
mail_user = os.environ.get("XPROBE_MAIL_USER", "<mail-user>")
mail_user1 = os.environ.get("XPROBE_MAIL_FROM", "<mail-user>@<mail-domain>")
mail_pass = os.environ.get("XPROBE_MAIL_PASS", "")
mail_postfix = os.environ.get("XPROBE_MAIL_POSTFIX", "<mail-domain>")


def _envList(key):
    raw = os.environ.get(key, "")
    return [item.strip() for item in raw.split(",") if item.strip()]


mailto = _envList("XPROBE_MAIL_TO")
mailcc = _envList("XPROBE_MAIL_CC")

# ==================================
# 装机后需额外授予系统权限的设备（按设备序列号区分，接入方按自己的设备填写）
# ==================================
uiautoVivoDevices = _envList("XPROBE_UIAUTO_VIVO_DEVICES")
uiautoXiaomiDevices = _envList("XPROBE_UIAUTO_XIAOMI_DEVICES")
