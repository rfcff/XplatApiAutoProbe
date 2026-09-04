#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""XplatApiAutoProbe iOS 库端到端回归测试（macOS 宿主 + Python 客户端）。

把 iOS 库（ios/xprobe/）与其测试工程（demo/ios，`--headless`）用 clang 编译为
macOS 宿主程序，再以线协议客户端做端到端回归。

  - 基础链路：PING/PONG、GET_API 方法枚举；
  - ver=2 自定义调用（后台线程与主线程两种 threadMode）；
  - ver=1 反射调用（类方法 / 经实例管理器的实例方法）；
  - field 命令（反射 setter 修改属性并回读验证）；
  - 数组命令批量调用（同 threadMode 组内顺序执行）；
  - 非法 JSON 的 parseError 回包；
  - 日志回调验证：demo 启动时注册 XPLogSetHandler，
    触发一次连接与一次 parseError 后，断言 handler 收到对应日志。

运行方式（在仓库任意目录均可）：
    python3 client/test_client_ios.py

测试启动前自动完成：
  1) pkill 清理残留的旧 demo 进程（xprobe_demo），释放 9000 端口；
  2) 宿主二进制不存在或源码有更新时先用 clang 构建；
  3) 后台拉起 demo 宿主（stderr 接管道捕获，用于日志回调验证），
     并用 PING -> PONG 探测端口就绪。
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
IOS_DIR = os.path.join(ROOT, "ios")                 # iOS 库源码
IOS_DEMO_DIR = os.path.join(ROOT, "demo", "ios")    # iOS 测试工程
DEMO_BIN = os.path.join(IOS_DEMO_DIR, "build", "xprobe_ios_demo")
HOST = "127.0.0.1"
PORT = 9000

_demo_proc = None

# demo 宿主 stderr 捕获（日志回调验证用；demo 注册的 handler 以
# [XplatApiAutoProbe demo][LEVEL][tag] message 格式输出内部日志）
_log_lines = []
_log_lock = threading.Lock()


# ======================================================================
# demo 宿主进程管理
# ======================================================================

def _kill_stale_demo():
    """清理残留的旧 demo 进程，避免端口 9000 被占用。"""
    for name in ("xprobe_ios_demo", "xprobe_demo"):
        try:
            subprocess.run(["pkill", "-f", name],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        except OSError:
            pass  # pkill 不存在时忽略
    time.sleep(0.3)  # 等待端口释放


def _demo_sources():
    """demo 宿主依赖的全部源码文件（用于增量构建判断）。"""
    demo_files = sorted(glob.glob(os.path.join(IOS_DEMO_DIR, "**", "*"), recursive=True))
    demo_files = [f for f in demo_files
                  if os.path.isfile(f)
                  and not f.startswith(os.path.join(IOS_DEMO_DIR, "build"))
                  and f.endswith((".m", ".mm", ".h", ".sh"))]
    return (demo_files +
            sorted(glob.glob(os.path.join(IOS_DIR, "xprobe", "*.mm"))) +
            sorted(glob.glob(os.path.join(IOS_DIR, "xprobe", "connect", "*.mm"))) +
            sorted(glob.glob(os.path.join(IOS_DIR, "xprobe", "*.h"))) +
            sorted(glob.glob(os.path.join(IOS_DIR, "xprobe", "connect", "*.h"))))


def _build_demo_if_needed():
    """宿主二进制不存在或源码有更新时执行 build.sh 构建。"""
    sources = _demo_sources()
    build_sh = os.path.join(IOS_DEMO_DIR, "build.sh")
    if os.path.exists(DEMO_BIN):
        binary_mtime = os.path.getmtime(DEMO_BIN)
        if os.path.getmtime(build_sh) <= binary_mtime and all(
                os.path.getmtime(s) <= binary_mtime for s in sources):
            return  # 二进制已是最新
    print("[test] 构建 macOS 宿主 %s ..." % DEMO_BIN)
    result = subprocess.run(["bash", build_sh], cwd=IOS_DEMO_DIR,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    if result.returncode != 0:
        raise RuntimeError("构建 demo 宿主失败:\n%s" % result.stdout)


def _recv_exact(sock, size):
    """精确读取 size 字节（处理半包）。"""
    data = b""
    while len(data) < size:
        chunk = sock.recv(size - len(data))
        if not chunk:
            raise ConnectionError("连接被服务端关闭")
        data += chunk
    return data


def _wait_server_ready(timeout=10.0):
    """轮询连接 demo 端口并发 PING 帧，收到 PONG 才视为服务就绪。"""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            sock = socket.create_connection((HOST, PORT), timeout=1.0)
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
        time.sleep(0.1)
    return False


# ======================================================================
# demo 宿主 stderr 日志捕获（日志回调验证用）
# ======================================================================

def _log_reader(stream):
    """后台线程：持续读取 demo 宿主的 stderr，收集日志行。"""
    for raw in iter(stream.readline, b""):
        with _log_lock:
            _log_lines.append(raw.decode("utf-8", errors="replace"))
    stream.close()


def _log_snapshot():
    """当前已收集的日志行数（作为增量断言的起点）。"""
    with _log_lock:
        return len(_log_lines)


def _wait_log(predicate, start=0, timeout=5.0):
    """轮询等待出现 predicate 命中的日志行；返回命中的行，超时返回 None。

    :param predicate: 行匹配函数（返回 True 表示命中）
    :param start:     从第 start 行开始搜索（增量断言时传快照值）
    :param timeout:   等待超时（秒）
    """
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        with _log_lock:
            for line in _log_lines[start:]:
                if predicate(line):
                    return line
        time.sleep(0.05)
    return None


def setUpModule():
    """所有测试开始前：清理旧进程 -> 按需构建 -> 拉起 demo 并等待就绪。"""
    global _demo_proc
    _kill_stale_demo()
    _build_demo_if_needed()
    _demo_proc = subprocess.Popen(
        [DEMO_BIN, "--headless"], cwd=IOS_DEMO_DIR,
        stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    # 后台线程持续收取 demo 的 stderr（demo 注册了 XPLogSetHandler，
    # SDK 内部日志以 [XplatApiAutoProbe demo][LEVEL][tag] message 格式输出）
    reader = threading.Thread(target=_log_reader, args=(_demo_proc.stderr,),
                              daemon=True)
    reader.start()
    if not _wait_server_ready():
        _demo_proc.terminate()
        raise RuntimeError(
            "demo 服务未就绪：端口 %d 未在超时内完成 PING/PONG 探测"
            "（请检查端口是否被其他进程占用）" % PORT)
    print("[test] iOS demo 宿主已就绪 (%s:%d)" % (HOST, PORT))


def tearDownModule():
    """所有测试结束后：终止 demo 进程。"""
    global _demo_proc
    if _demo_proc is not None:
        _demo_proc.terminate()
        try:
            _demo_proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            _demo_proc.kill()
        _demo_proc = None
    print("[test] iOS demo 宿主已停止")


# ======================================================================
# 端到端回归测试
# ======================================================================

class XProbeIOSRegressionTest(unittest.TestCase):
    """iOS 库（macOS 宿主）线协议与日志回调端到端回归。"""

    def setUp(self):
        # 每个用例独立连接，互不干扰
        self.client = XProbeClient(HOST, PORT, timeout=5.0)
        self.client.connect()

    def tearDown(self):
        self.client.close()

    # ---- 基础链路 ----

    def test_ping_pong(self):
        """PING 心跳帧应得到 PONG 回复（连续两次验证链路稳定）。"""
        self.assertTrue(self.client.ping())
        self.assertTrue(self.client.ping())

    def test_get_api_enumerates_methods(self):
        """GET_API 反射枚举：应返回 DemoCalc 的类方法元数据。"""
        result = self.client.get_api("DemoCalc")
        self.assertIsInstance(result, list)
        entries = {entry["api"]: entry for entry in result}
        self.assertIn("DemoCalc.addLeft:right:", entries)
        self.assertEqual(entries["DemoCalc.addLeft:right:"]["param_name"],
                         ["addLeft", "right"])
        self.assertEqual(len(entries["DemoCalc.addLeft:right:"]["param_type"]), 2)

    # ---- ver=2 自定义调用 ----

    def test_ver2_custom_invocation(self):
        """ver=2 调用：DemoInvocation 原样回显 apiName 与 params。"""
        frame = self.client.call("echo", {"text": "hi"}, thread_mode=0, ver=2)
        self.assertEqual(frame["type"], "return")
        self.assertEqual(frame["key"], "echo")
        self.assertIn("echo api=echo params=", frame["value"])
        self.assertIn("text = hi", frame["value"])

    def test_ver2_main_thread_mode(self):
        """ver=2 主线程模式（threadMode=1）：命令派发到主队列执行并正常回包。"""
        frame = self.client.call("echo", {"mode": "main"}, thread_mode=1, ver=2)
        self.assertEqual(frame["type"], "return")
        self.assertEqual(frame["key"], "echo")
        self.assertIn("mode = main", frame["value"])

    # ---- ver=1 反射调用 ----

    def test_ver1_reflect_class_method(self):
        """ver=1 反射类方法：DemoCalc.addLeft:right:(1, 2) 应返回 3。

        param_type 需与方法真实签名一致（NSInteger，8 字节；
        GET_API 枚举结果亦为 long long / NSInteger 风格）。
        """
        command = {"api": "DemoCalc.addLeft:right:",
                   "param_type": ["NSInteger", "NSInteger"],
                   "param_value": ["1", "2"],
                   "threadMode": 0, "ver": 1}
        self.client.send_frame(json.dumps(command))
        frame = self.client.wait_response()
        self.assertEqual(frame["type"], "return")
        self.assertEqual(frame["key"], "DemoCalc.addLeft:right:")
        self.assertEqual(frame["value"], "3")

    def test_ver1_reflect_instance_method(self):
        """ver=1 反射实例方法：DemoState.describe 经实例管理器取实例调用。"""
        command = {"api": "DemoState.describe", "threadMode": 0, "ver": 1}
        self.client.send_frame(json.dumps(command))
        frame = self.client.wait_response()
        self.assertEqual(frame["type"], "return")
        self.assertEqual(frame["key"], "DemoState.describe")
        self.assertIn("uid=", frame["value"])

    # ---- field 命令 ----

    def test_field_set_and_readback(self):
        """field 命令：反射 setter 修改 DemoState 属性，再用实例方法回读验证。"""
        frame = self.client.set_field("DemoState", [
            ("uid", "String", "234"),
            ("channelId", "String", "567"),
        ])
        self.assertEqual(frame["type"], "return")
        self.assertEqual(frame["key"], "setUid:")
        self.assertEqual(frame["value"], "void")
        frame = self.client.wait_response()  # 第二个 setter 的返回帧
        self.assertEqual(frame["type"], "return")
        self.assertEqual(frame["key"], "setChannelId:")
        # 回读：field 修改应已生效
        command = {"api": "DemoState.describe", "threadMode": 0, "ver": 1}
        self.client.send_frame(json.dumps(command))
        frame = self.client.wait_response()
        self.assertEqual(frame["type"], "return")
        self.assertEqual(frame["value"], "uid=234 channelId=567")

    # ---- 数组命令 ----

    def test_batch_commands_in_order(self):
        """数组命令帧：同帧内命令顺序执行并逐条返回，返回帧顺序与命令一致。"""
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
            self.assertIn("n = %d" % index, frame["value"])

    # ---- 错误处理 ----

    def test_invalid_json_gets_parse_error(self):
        """非法 JSON 帧：服务端应回 error 帧（key=parseError）。"""
        self.client.send_frame("{this is not a valid json")
        frame = self.client.wait_response()
        self.assertEqual(frame["type"], "error")
        self.assertEqual(frame["key"], "parseError")
        self.assertIn("json parse failed", frame["value"])

    # ---- 日志回调验证 ----

    def test_log_handler_receives_key_path_logs(self):
        """日志回调：demo 注册 handler 后，服务启动、连接建立、命令解析
        失败的内部日志均进入 handler 输出（带 demo 前缀与级别名）。"""
        # 本用例自身连接先完成一次 PING，确保其「接入」日志已输出
        self.assertTrue(self.client.ping())
        # 服务启动日志：handler 在启动服务前注册，应捕获到最早期的启动日志
        line = _wait_log(lambda s: ("[XplatApiAutoProbe demo][INFO][mgr]" in s
                                    and "RPC 服务已启动" in s))
        self.assertIsNotNone(line, "未捕获到服务启动日志（INFO/mgr）")
        # 增量快照：后续只断言本用例触发的日志
        start = _log_snapshot()
        # 触发一次连接（新客户端接入）
        probe = XProbeClient(HOST, PORT, timeout=5.0)
        probe.connect()
        self.assertTrue(probe.ping())
        # 触发一次 parseError（非法 JSON）
        probe.send_frame("{this is not a valid json")
        frame = probe.wait_response()
        self.assertEqual(frame["key"], "parseError")
        probe.close()
        # 断言 handler 收到「连接建立」日志（INFO/conn）
        line = _wait_log(lambda s: ("[XplatApiAutoProbe demo][INFO][conn]" in s
                                    and "客户端已接入" in s), start=start)
        self.assertIsNotNone(line, "未捕获到连接建立日志（INFO/conn）")
        # 断言 handler 收到「命令解析失败」日志（WARN/cmd）
        line = _wait_log(lambda s: ("[XplatApiAutoProbe demo][WARN][cmd]" in s
                                    and "命令帧 JSON 解析失败" in s), start=start)
        self.assertIsNotNone(line, "未捕获到命令解析失败日志（WARN/cmd）")


if __name__ == "__main__":
    unittest.main(verbosity=2)
