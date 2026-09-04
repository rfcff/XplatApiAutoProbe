#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""XplatApiAutoProbe Android 库端到端回归测试（真机/模拟器 + Python 客户端）。

把 Android demo（demo/android）装到设备上，经 adb forward 把宿主端口映射到
设备上的探针服务（默认 9000），再以线协议客户端做端到端回归：

  - 基础链路：PING/PONG、GET_API 方法枚举；
  - ver=2 自定义调用（echo / getState / 主线程 threadMode / 未知 API）；
  - ver=1 反射调用（DemoCalc.add 静态方法）；
  - field 命令（改写 DemoState 静态字段并反射回读）；
  - 数组命令批量调用（同 threadMode 组内顺序执行）；
  - 异步 callback（scheduleCallback）；
  - 非法 JSON 的 parseError 回包。

运行方式（仓库根目录，需已连接 Android 设备且 adb 可用）：
    python3 client/test_client_android.py

环境变量（可选）：
    XPROBE_ANDROID_SERIAL      多设备时指定序列号
    XPROBE_ANDROID_HOST_PORT   本机转发端口，默认 19000（避免与本机 :9000 PC demo 冲突）
    XPROBE_ANDROID_SKIP_BUILD  设为 1 时跳过 gradle 构建，直接用已有 APK

测试启动前自动完成：
  1) 检查 adb 与在线设备；
  2) APK 不存在或源码有更新时执行 ./gradlew :app:assembleDebug；
  3) adb install -r、force-stop、启动 MainActivity；
  4) adb forward tcp:<hostPort> tcp:9000，并用 PING -> PONG 探测就绪。
"""

import glob
import json
import os
import socket
import struct
import subprocess
import sys
import threading
import time
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from xprobe_client import XProbeClient  # noqa: E402

# 仓库根目录（client/ 的上一级）
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ANDROID_DEMO_DIR = os.path.join(ROOT, "demo", "android")
ANDROID_LIB_DIR = os.path.join(ROOT, "android", "xprobe")
APK_PATH = os.path.join(
    ANDROID_DEMO_DIR, "app", "build", "outputs", "apk", "debug", "app-debug.apk")
PACKAGE = "com.xprobe.thunderdemo"
MAIN_ACTIVITY = PACKAGE + "/.MainActivity"
DEVICE_PORT = 9000
HOST = "127.0.0.1"
HOST_PORT = int(os.environ.get("XPROBE_ANDROID_HOST_PORT", "19000"))
SERIAL = os.environ.get("XPROBE_ANDROID_SERIAL", "").strip()
SKIP_BUILD = os.environ.get("XPROBE_ANDROID_SKIP_BUILD", "").strip() in ("1", "true", "yes")

DEMO_CALC = "com.xprobe.thunderdemo.probe.DemoCalc"
DEMO_STATE = "com.xprobe.thunderdemo.probe.DemoState"

_adb = None  # 解析到的 adb 可执行路径


# ======================================================================
# adb / 构建 / 安装
# ======================================================================

def _find_adb():
    """定位 adb：PATH -> ANDROID_HOME/ANDROID_SDK_ROOT -> macOS 默认 SDK 路径。"""
    from shutil import which
    found = which("adb")
    if found:
        return found
    for key in ("ANDROID_HOME", "ANDROID_SDK_ROOT"):
        root = os.environ.get(key)
        if root:
            candidate = os.path.join(root, "platform-tools", "adb")
            if os.path.isfile(candidate):
                return candidate
    mac_default = os.path.expanduser(
        "~/Library/Android/sdk/platform-tools/adb")
    if os.path.isfile(mac_default):
        return mac_default
    raise RuntimeError(
        "未找到 adb。请安装 Android SDK platform-tools，并保证 adb 在 PATH 中，"
        "或设置 ANDROID_HOME / ANDROID_SDK_ROOT。")


def _adb_cmd(*args):
    """构造带可选 -s <serial> 的 adb 命令列表。"""
    cmd = [_adb]
    if SERIAL:
        cmd.extend(["-s", SERIAL])
    cmd.extend(args)
    return cmd


def _adb_run(*args, check=True, timeout=120):
    """执行 adb 子命令，返回 CompletedProcess。"""
    result = subprocess.run(
        _adb_cmd(*args),
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        text=True, timeout=timeout)
    if check and result.returncode != 0:
        raise RuntimeError(
            "adb 命令失败 (%d): %s\n%s"
            % (result.returncode, " ".join(_adb_cmd(*args)), result.stdout))
    return result


def _pick_device():
    """确认至少有一台 device 状态的设备；多设备且未指定 SERIAL 时失败。"""
    result = _adb_run("devices", check=True)
    lines = [ln.strip() for ln in result.stdout.splitlines()[1:] if ln.strip()]
    devices = []
    for ln in lines:
        parts = ln.split()
        if len(parts) >= 2 and parts[1] == "device":
            devices.append(parts[0])
    if not devices:
        raise RuntimeError(
            "没有在线的 Android 设备（adb devices 为空或均为 offline）。\n"
            "请连接真机并开启 USB 调试，或启动模拟器后再跑本脚本。\n"
            "注意：demo APK 仅含 armeabi-v7a / arm64-v8a，x86 模拟器无法安装。")
    if SERIAL:
        if SERIAL not in devices:
            raise RuntimeError(
                "XPROBE_ANDROID_SERIAL=%s 不在在线设备列表中: %s"
                % (SERIAL, ", ".join(devices)))
        return SERIAL
    if len(devices) > 1:
        raise RuntimeError(
            "检测到多台设备 (%s)，请设置环境变量 XPROBE_ANDROID_SERIAL 指定一台。"
            % ", ".join(devices))
    return devices[0]


def _source_files():
    """影响 APK 内容的源码 / 构建脚本（用于增量构建判断）。"""
    patterns = [
        os.path.join(ANDROID_DEMO_DIR, "app", "src", "main", "**", "*.java"),
        os.path.join(ANDROID_DEMO_DIR, "app", "src", "main", "**", "*.xml"),
        os.path.join(ANDROID_DEMO_DIR, "app", "build.gradle"),
        os.path.join(ANDROID_DEMO_DIR, "build.gradle"),
        os.path.join(ANDROID_DEMO_DIR, "settings.gradle"),
        os.path.join(ANDROID_DEMO_DIR, "gradle.properties"),
        os.path.join(ANDROID_LIB_DIR, "src", "main", "**", "*.java"),
        os.path.join(ANDROID_LIB_DIR, "build.gradle"),
    ]
    files = []
    for pattern in patterns:
        files.extend(glob.glob(pattern, recursive=True))
    return [f for f in files if os.path.isfile(f)]


def _build_apk_if_needed():
    """APK 缺失或源码更新时执行 gradle assembleDebug。"""
    if SKIP_BUILD:
        if not os.path.isfile(APK_PATH):
            raise RuntimeError(
                "XPROBE_ANDROID_SKIP_BUILD=1 但未找到 APK: %s" % APK_PATH)
        print("[test] 跳过构建，使用已有 APK: %s" % APK_PATH)
        return
    sources = _source_files()
    if os.path.isfile(APK_PATH):
        apk_mtime = os.path.getmtime(APK_PATH)
        if sources and all(os.path.getmtime(s) <= apk_mtime for s in sources):
            print("[test] APK 已是最新: %s" % APK_PATH)
            return
    gradlew = os.path.join(ANDROID_DEMO_DIR, "gradlew")
    if not os.path.isfile(gradlew):
        raise RuntimeError("未找到 %s" % gradlew)
    print("[test] 构建 Android demo APK ...")
    cmd = [gradlew, ":app:assembleDebug", "--quiet"]
    result = subprocess.run(
        cmd, cwd=ANDROID_DEMO_DIR,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    if result.returncode != 0 or not os.path.isfile(APK_PATH):
        raise RuntimeError(
            "构建 APK 失败: %s\n%s" % (" ".join(cmd), result.stdout))
    print("[test] APK 已生成: %s" % APK_PATH)


def _install_and_launch(serial):
    """安装 APK、清理旧进程、启动 MainActivity（触发 DemoApplication 启探针）。"""
    print("[test] 安装 APK 到设备 %s ..." % serial)
    _adb_run("install", "-r", "-t", APK_PATH, timeout=180)
    _adb_run("shell", "am", "force-stop", PACKAGE, check=False)
    time.sleep(0.5)
    _adb_run("shell", "am", "start", "-n", MAIN_ACTIVITY)
    print("[test] 已启动 %s" % MAIN_ACTIVITY)


def _setup_port_forward():
    """本机 HOST_PORT -> 设备 DEVICE_PORT。"""
    _adb_run("forward", "--remove", "tcp:%d" % HOST_PORT, check=False)
    _adb_run("forward", "tcp:%d" % HOST_PORT, "tcp:%d" % DEVICE_PORT)
    print("[test] adb forward tcp:%d -> device:%d" % (HOST_PORT, DEVICE_PORT))


def _teardown_port_forward():
    _adb_run("forward", "--remove", "tcp:%d" % HOST_PORT, check=False)


def _recv_exact(sock, size):
    """精确读取 size 字节（处理半包）。"""
    data = b""
    while len(data) < size:
        chunk = sock.recv(size - len(data))
        if not chunk:
            raise ConnectionError("连接被服务端关闭")
        data += chunk
    return data


def _wait_server_ready(timeout=45.0):
    """轮询本机转发端口，PING 收到 PONG 才视为服务就绪。"""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            sock = socket.create_connection((HOST, HOST_PORT), timeout=1.0)
            try:
                payload = b"PING"
                sock.sendall(struct.pack(">I", len(payload)) + payload)
                (length,) = struct.unpack(">I", _recv_exact(sock, 4))
                if _recv_exact(sock, length) == b"PONG":
                    return True
            finally:
                sock.close()
        except OSError:
            pass
        time.sleep(0.3)
    return False


def setUpModule():
    """所有测试开始前：设备检查 -> 构建/安装 -> 端口转发 -> 就绪探测。"""
    global _adb, SERIAL
    _adb = _find_adb()
    serial = _pick_device()
    # 把选中的序列号写回，后续 _adb_cmd 统一带 -s
    if not SERIAL:
        globals()["SERIAL"] = serial
    _build_apk_if_needed()
    _install_and_launch(serial)
    _setup_port_forward()
    if not _wait_server_ready():
        _teardown_port_forward()
        raise RuntimeError(
            "Android demo 探针未就绪：本机 %s:%d 在超时内未完成 PING/PONG。\n"
            "请检查：应用是否崩溃（adb logcat）、设备 ABI 是否为 arm/arm64、"
            "DemoApplication 是否成功 start AutoTestRpcServer。"
            % (HOST, HOST_PORT))
    print("[test] Android demo 探针已就绪 (%s:%d -> device:%d)"
          % (HOST, HOST_PORT, DEVICE_PORT))


def tearDownModule():
    """所有测试结束后：撤掉端口转发，并 force-stop 被测 App。"""
    try:
        _adb_run("shell", "am", "force-stop", PACKAGE, check=False)
    except Exception:
        pass
    try:
        _teardown_port_forward()
    except Exception:
        pass
    print("[test] Android demo 已停止，端口转发已清理")


# ======================================================================
# 端到端回归测试
# ======================================================================

class XProbeAndroidRegressionTest(unittest.TestCase):
    """Android 库（设备上的 demo App）线协议端到端回归。"""

    def setUp(self):
        self.client = XProbeClient(HOST, HOST_PORT, timeout=8.0)
        self.client.connect()

    def tearDown(self):
        self.client.close()

    # ---- 基础链路 ----

    def test_ping_pong(self):
        """PING 心跳帧应得到 PONG 回复（连续两次验证链路稳定）。"""
        self.assertTrue(self.client.ping())
        self.assertTrue(self.client.ping())

    def test_get_api_enumerates_methods(self):
        """GET_API 反射枚举：应包含 DemoCalc.add（有参方法）。"""
        result = self.client.get_api(DEMO_CALC)
        self.assertIsInstance(result, list)
        apis = [entry.get("api", "") for entry in result]
        self.assertTrue(
            any(api.endswith(".add") for api in apis),
            "GET_API(%s) 未包含 add，实际: %s" % (DEMO_CALC, apis))
        add_entry = next(e for e in result if str(e.get("api", "")).endswith(".add"))
        self.assertEqual(len(add_entry.get("param_type", [])), 2)

    # ---- ver=2 自定义调用 ----

    def test_ver2_echo(self):
        """ver=2 echo：DemoInvocation 回显 apiName 与 params。"""
        frame = self.client.call("echo", {"text": "hi"}, thread_mode=0, ver=2)
        self.assertEqual(frame["type"], "return")
        self.assertEqual(frame["key"], "echo")
        self.assertIn("echo api=echo", frame["value"])
        self.assertIn("text", frame["value"])
        self.assertIn("hi", frame["value"])

    def test_ver2_main_thread_mode(self):
        """ver=2 主线程模式（threadMode=1）：命令派发到主线程并正常回包。"""
        frame = self.client.call("echo", {"mode": "main"}, thread_mode=1, ver=2)
        self.assertEqual(frame["type"], "return")
        self.assertEqual(frame["key"], "echo")
        self.assertIn("mode", frame["value"])
        self.assertIn("main", frame["value"])

    def test_ver2_get_state(self):
        """ver=2 getState：返回引擎状态文本（无需已初始化）。"""
        frame = self.client.call("getState", {}, thread_mode=0, ver=2)
        self.assertEqual(frame["type"], "return")
        self.assertEqual(frame["key"], "getState")
        self.assertIn("init=", frame["value"])

    def test_ver2_unknown_api_returns_error(self):
        """ver=2 调用未知方法：应得到 error 帧（key=方法名）。"""
        frame = self.client.call("noSuchMethod", {})
        self.assertEqual(frame["type"], "error")
        self.assertEqual(frame["key"], "noSuchMethod")
        self.assertIn("unknown api", frame["value"])

    # ---- ver=1 反射调用 ----

    def test_ver1_reflect_static_method(self):
        """ver=1 反射静态方法：DemoCalc.add(1, 2) 应返回 3。"""
        command = {
            "api": DEMO_CALC + ".add",
            "param_name": ["left", "right"],
            "param_type": ["int", "int"],
            "param_value": ["1", "2"],
            "threadMode": 0,
            "ver": 1,
        }
        self.client.send_frame(json.dumps(command))
        frame = self.client.wait_response()
        self.assertEqual(frame["type"], "return")
        self.assertEqual(frame["key"], DEMO_CALC + ".add")
        self.assertEqual(frame["value"], "3")

    # ---- field 命令 ----

    def test_field_set_and_readback(self):
        """field 命令：改写 DemoState 静态字段，再反射 describe 回读。"""
        frame = self.client.set_field(DEMO_STATE, [
            ("uid", "String", "234"),
            ("channelId", "String", "567"),
        ])
        self.assertEqual(frame["type"], "return")
        self.assertEqual(frame["key"], "uid")
        self.assertEqual(frame["value"], "void")
        frame = self.client.wait_response()  # 第二个字段的 return
        self.assertEqual(frame["type"], "return")
        self.assertEqual(frame["key"], "channelId")
        self.assertEqual(frame["value"], "void")

        # 无参方法：三个参数数组都省略（协议允许 value 缺失且 name/type 同时缺失）
        command = {
            "api": DEMO_STATE + ".describe",
            "threadMode": 0,
            "ver": 1,
        }
        self.client.send_frame(json.dumps(command))
        frame = self.client.wait_response()
        self.assertEqual(frame["type"], "return")
        self.assertEqual(frame["key"], DEMO_STATE + ".describe")
        self.assertEqual(frame["value"], "uid=234 channelId=567")

    # ---- 数组命令 ----

    def test_batch_commands_in_order(self):
        """数组命令帧：同 threadMode 组内顺序执行，返回帧顺序与命令一致。"""
        commands = [
            {"apiName": "echo", "params": {"n": 1}, "thread_mode": 0, "ver": 2},
            {"apiName": "echo", "params": {"n": 2}, "thread_mode": 0, "ver": 2},
            {"apiName": "echo", "params": {"n": 3}, "thread_mode": 0, "ver": 2},
        ]
        frames = self.client.call_batch(commands)
        self.assertEqual(len(frames), 3)
        self.assertEqual([f["type"] for f in frames], ["return"] * 3)
        self.assertEqual([f["key"] for f in frames], ["echo"] * 3)
        for index, frame in enumerate(frames, start=1):
            self.assertIn(str(index), frame["value"])

    # ---- 异步 callback ----

    def test_async_callback_subscription(self):
        """scheduleCallback：register_callback 订阅后收到异步 callback 帧。"""
        received = []
        event = threading.Event()

        def on_probe(frame):
            received.append(frame)
            event.set()

        self.client.register_callback("onProbeCallback", on_probe)
        frame = self.client.call(
            "scheduleCallback",
            {"name": "onProbeCallback", "info": "probe-ok", "delayMs": 200},
            thread_mode=0, ver=2)
        self.assertEqual(frame["type"], "return")
        self.assertEqual(frame["key"], "scheduleCallback")
        self.assertEqual(frame["value"], "0")
        self.assertTrue(event.wait(5), "等待 onProbeCallback 超时")
        self.assertEqual(len(received), 1)
        self.assertEqual(received[0]["type"], "callback")
        self.assertEqual(received[0]["key"], "onProbeCallback")
        self.assertIn("probe-ok", received[0]["value"])

    # ---- 错误处理 ----

    def test_invalid_json_gets_parse_error(self):
        """非法 JSON 帧：服务端应回 error 帧（key=parseError）。"""
        self.client.send_frame("{this is not a valid json")
        frame = self.client.wait_response()
        self.assertEqual(frame["type"], "error")
        self.assertEqual(frame["key"], "parseError")


if __name__ == "__main__":
    unittest.main(verbosity=2)
