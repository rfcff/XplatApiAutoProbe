#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""XplatApiAutoProbe 跨端一致性测试（unittest）—— PC demo。

对 demo/pc 宿主（与 Android demo 测试面对齐）做线协议一致性验证。

运行方式（在任意目录均可）：
    python3 client/test_client.py

测试启动前自动完成：
  1) pkill 清理残留的旧 demo 进程（xprobe_demo），释放 9000 端口；
  2) demo/pc/build/xprobe_demo 不存在时先执行 cmake 构建；
  3) 后台以 --headless 拉起 demo 服务，并用 PING -> PONG 探测端口就绪。
"""

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

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEMO_BIN = os.path.join(ROOT, "demo", "pc", "build", "xprobe_demo")
HOST = "127.0.0.1"
PORT = 9000

_demo_proc = None


def _kill_stale_demo():
    for name in ("xprobe_demo",):
        try:
            subprocess.run(["pkill", "-f", name],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        except OSError:
            pass
    time.sleep(0.3)


def _build_demo_if_needed():
    if os.path.exists(DEMO_BIN):
        return
    print("[test] 未找到 %s，开始 cmake 构建..." % DEMO_BIN)
    for cmd in (
        ["cmake", "-S", os.path.join(ROOT, "demo", "pc"),
         "-B", os.path.join(ROOT, "demo", "pc", "build")],
        ["cmake", "--build", os.path.join(ROOT, "demo", "pc", "build")],
    ):
        result = subprocess.run(cmd, cwd=ROOT, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, text=True)
        if result.returncode != 0:
            raise RuntimeError("构建 demo 失败: %s\n%s" % (" ".join(cmd), result.stdout))


def _recv_exact(sock, size):
    data = b""
    while len(data) < size:
        chunk = sock.recv(size - len(data))
        if not chunk:
            raise ConnectionError("连接被服务端关闭")
        data += chunk
    return data


def _wait_server_ready(timeout=10.0):
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


def setUpModule():
    global _demo_proc
    _kill_stale_demo()
    _build_demo_if_needed()
    _demo_proc = subprocess.Popen(
        [DEMO_BIN, "--headless"], cwd=ROOT,
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if not _wait_server_ready():
        _demo_proc.terminate()
        raise RuntimeError(
            "demo 服务未就绪：端口 %d 未在超时内完成 PING/PONG 探测"
            "（请检查端口是否被其他进程占用）" % PORT)
    print("[test] demo 服务已就绪 (%s:%d)" % (HOST, PORT))


def tearDownModule():
    global _demo_proc
    if _demo_proc is not None:
        _demo_proc.terminate()
        try:
            _demo_proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            _demo_proc.kill()
        _demo_proc = None
    print("[test] demo 服务已停止")


class XProbeClientConsistencyTest(unittest.TestCase):
    """XProbeClient 与 Android 对齐后的 PC demo 一致性测试。"""

    def setUp(self):
        self.client = XProbeClient(HOST, PORT, timeout=5.0)
        self.client.connect()

    def tearDown(self):
        self.client.close()

    def test_ping_pong(self):
        self.assertTrue(self.client.ping())
        self.assertTrue(self.client.ping())

    def test_get_api_returns_empty_list(self):
        """C++ Core 无运行时反射，GET_API 返回 []。"""
        result = self.client.get_api("com.foo.Engine")
        self.assertIsInstance(result, list)
        self.assertEqual(result, [])

    def test_ver2_create_engine(self):
        """createEngine 返回初始化耗时（毫秒数字符串），与 Android 对齐。"""
        frame = self.client.call("createEngine",
                                 {"appId": "123", "sceneId": 1},
                                 thread_mode=0, ver=2)
        self.assertEqual(frame["type"], "return")
        self.assertEqual(frame["key"], "createEngine")
        self.assertRegex(frame["value"], r"^\d+$")
        # 清理，避免影响后续用例状态
        self.client.call("destroyEngine", {})

    def test_ver2_echo_and_get_state(self):
        frame = self.client.call("echo", {"text": "hi"}, thread_mode=0, ver=2)
        self.assertEqual(frame["type"], "return")
        self.assertEqual(frame["key"], "echo")
        self.assertIn("echo api=echo", frame["value"])
        self.assertIn("hi", frame["value"])

        frame = self.client.call("getState", {}, ver=2)
        self.assertEqual(frame["type"], "return")
        self.assertIn("init=", frame["value"])

    def test_ver2_unknown_api_returns_error(self):
        frame = self.client.call("noSuchMethod", {})
        self.assertEqual(frame["type"], "error")
        self.assertEqual(frame["key"], "noSuchMethod")
        self.assertIn("unknown api", frame["value"])

    def test_ver1_legacy_passthrough(self):
        """ver=1：C++ Core 把 api 子对象序列化后透传，demo 回显 legacyApi。"""
        frame = self.client.call("legacyEcho", {"k": "v"},
                                 thread_mode=0, ver=1)
        self.assertEqual(frame["type"], "return")
        self.assertEqual(frame["key"], "legacyApi")
        self.assertIn("legacyEcho", frame["value"])
        self.assertIn("k", frame["value"])

    def test_call_batch(self):
        commands = [
            {"apiName": "echo", "params": {"n": 1}},
            {"apiName": "echo", "params": {"n": 2}},
            {"apiName": "getState"},
        ]
        frames = self.client.call_batch(commands)
        self.assertEqual(len(frames), 3)
        self.assertEqual([f["type"] for f in frames], ["return"] * 3)
        self.assertEqual([f["key"] for f in frames], ["echo", "echo", "getState"])
        self.assertIn("1", frames[0]["value"])
        self.assertIn("2", frames[1]["value"])
        self.assertIn("init=", frames[2]["value"])

    def test_invalid_json_gets_parse_error(self):
        self.client.send_frame("{this is not a valid json")
        frame = self.client.wait_response()
        self.assertEqual(frame["type"], "error")
        self.assertEqual(frame["key"], "parseError")
        self.assertIn("json parse failed", frame["value"])

    def test_sticky_frames_single_write(self):
        cmd1 = {"api": {"apiName": "echo", "params": {"level": 1}},
                "threadMode": 0, "ver": 2}
        cmd2 = {"api": {"apiName": "getState", "params": {}},
                "threadMode": 0, "ver": 2}
        self.client.send_frames(["PING", json.dumps(cmd1), json.dumps(cmd2)])
        self.assertTrue(self.client.wait_pong())
        frame1 = self.client.wait_response()
        frame2 = self.client.wait_response()
        by_key = {f["key"]: f for f in (frame1, frame2)}
        self.assertEqual(set(by_key), {"echo", "getState"})
        self.assertEqual(by_key["echo"]["type"], "return")
        self.assertIn("init=", by_key["getState"]["value"])

    def test_set_field_legacy_passthrough(self):
        """field 命令：C++ Core 透传整条命令，demo 回显 legacyApi。"""
        frame = self.client.set_field("com.foo.DemoConfig", [
            ("mUid", "String", "234"),
            ("mChannelId", "String", "567"),
        ])
        self.assertEqual(frame["type"], "return")
        self.assertEqual(frame["key"], "legacyApi")
        self.assertIn("com.foo.DemoConfig", frame["value"])
        self.assertIn("mUid", frame["value"])

    def test_async_callback_subscription(self):
        """scheduleCallback：与 Android 对齐的异步 callback 探针。"""
        received = []
        event = threading.Event()

        def on_probe(frame):
            received.append(frame)
            event.set()

        self.client.register_callback("onProbeCallback", on_probe)
        frame = self.client.call(
            "scheduleCallback",
            {"name": "onProbeCallback", "info": "probe-ok", "delayMs": 200},
            ver=2)
        self.assertEqual(frame["type"], "return")
        self.assertEqual(frame["key"], "scheduleCallback")
        self.assertEqual(frame["value"], "0")
        self.assertTrue(event.wait(5), "等待 onProbeCallback 超时")
        self.assertEqual(received[0]["type"], "callback")
        self.assertIn("probe-ok", received[0]["value"])

        # joinRoom 也会异步上报 onJoinRoomSuccess / onConnectionStatus
        join_event = threading.Event()
        self.client.register_callback(
            "onJoinRoomSuccess", lambda f: join_event.set())
        frame = self.client.call("createEngine", {"appId": "10034", "sceneId": 0})
        self.assertEqual(frame["type"], "return")
        frame = self.client.call("joinRoom", {"roomName": "room-42", "uid": "10086"})
        self.assertEqual(frame["type"], "return")
        self.assertEqual(frame["value"], "0")
        self.assertTrue(join_event.wait(5), "等待 onJoinRoomSuccess 超时")
        self.client.call("destroyEngine", {})


if __name__ == "__main__":
    unittest.main(verbosity=2)
