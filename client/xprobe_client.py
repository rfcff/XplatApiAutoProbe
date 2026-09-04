#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""XplatApiAutoProbe 跨端一致性测试客户端（零第三方依赖，仅标准库）。

严格实现 PROTOCOL.md v1.0 定义的线协议：

- 传输层：TCP；
- 帧格式：4 字节大端长度头（不含头本身）+ UTF-8 JSON/文本 payload；
- 特殊文本帧（客户端 -> 服务端）：
    "PING"                -> 服务端回复 "PONG"
    "PONG"                -> 服务端忽略
    "GET_API:<className>" -> 服务端回 JSON 数组（C++ Core 无反射，返回 []）
- 命令帧：单个 JSON 对象或对象数组（连续相同 threadMode 组内顺序执行；
  组间在 C++/Android 上可能并发，跨组回包顺序不保证）；
- 返回帧（服务端 -> 客户端）：
    {"type": "return",   "key": <方法名>,  "value": <返回值文本>}
    {"type": "error",    "key": <parseError|方法名>, "value": <错误描述>}
    {"type": "callback", "key": <回调名>,  "value": <回调内容>}

线程模型：后台接收线程负责 recv 与按长度头切帧（正确处理粘包/半包），
并把消息分拣到内部队列；callback 帧可由 register_callback 订阅，
未订阅的 callback 按名字缓存在有界队列中（可用 wait_callback 取出）。

用法示例::

    with XProbeClient("127.0.0.1", 9000) as client:
        assert client.ping()
        frame = client.call("createEngine", {"appId": "123", "sceneId": 1})
        print(frame["type"], frame["key"], frame["value"])
"""

import json
import queue
import socket
import struct
import threading
import time
from collections import deque

__all__ = ["XProbeClient"]

# 默认单次等待超时（秒）
DEFAULT_TIMEOUT = 5.0
# 单帧 payload 最大长度，超出视为协议错误。
# 32MB 为 PROTOCOL.md §2 规定的跨端统一值，各平台必须一致，不得单独调整。
MAX_FRAME_PAYLOAD = 32 * 1024 * 1024
# 每个 callback 名字最多缓存的未消费帧数（未订阅时防无限膨胀）
MAX_CALLBACK_CACHE = 128


class XProbeClient(object):
    """XplatApiAutoProbe 线协议测试客户端。

    :param host:   服务端地址（默认 127.0.0.1）
    :param port:   服务端端口（默认 9000，与 PROTOCOL.md 一致）
    :param timeout: 各阻塞等待的默认超时（秒）
    """

    def __init__(self, host="127.0.0.1", port=9000, timeout=DEFAULT_TIMEOUT):
        self._host = host
        self._port = port
        self._timeout = timeout
        self._sock = None
        self._closed = True
        self._recv_error = None          # 接收线程遇到的致命错误（帧长超限等）
        self._recv_thread = None
        self._send_lock = threading.Lock()  # 保证多线程 sendall 时字节流不交织
        # ---- 入站消息分拣队列（接收线程生产，调用方线程消费） ----
        self._pong_queue = queue.Queue()   # "PONG" 文本帧
        self._resp_queue = queue.Queue()   # return / error 返回帧（dict）
        self._api_queue = queue.Queue()    # GET_API 的 JSON 数组结果（list）
        # ---- callback 帧：按名字缓存 + 订阅回调 ----
        self._callback_cond = threading.Condition()
        self._callback_cache = {}         # 名字 -> deque(value)，有界
        self._callback_handlers = {}      # 名字 -> [handler, ...]

    # ==================================================================
    # 连接管理
    # ==================================================================

    def connect(self, host=None, port=None, timeout=None):
        """建立 TCP 连接并启动后台接收线程；已连接时直接返回自身（幂等）。"""
        if self._sock is not None and not self._closed:
            return self
        if host is not None:
            self._host = host
        if port is not None:
            self._port = port
        timeout = self._timeout if timeout is None else timeout
        # 清理上次会话残留的队列数据（支持 close 之后重连）
        self._drain_queues()
        sock = socket.create_connection((self._host, self._port), timeout=timeout)
        # 接收线程采用阻塞读；close 时通过 shutdown 唤醒
        sock.settimeout(None)
        self._sock = sock
        self._closed = False
        self._recv_error = None
        self._recv_thread = threading.Thread(
            target=self._recv_loop, name="XProbeClientRecv", daemon=True)
        self._recv_thread.start()
        return self

    def close(self):
        """关闭连接并停止接收线程（幂等，可重复调用）。"""
        if self._closed and self._sock is None:
            return  # 尚未连接或已关闭
        self._closed = True
        sock, self._sock = self._sock, None
        if sock is not None:
            # 先 shutdown 唤醒阻塞在 recv 的接收线程，再关闭句柄
            try:
                sock.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
            try:
                sock.close()
            except OSError:
                pass
        thread, self._recv_thread = self._recv_thread, None
        if thread is not None and thread is not threading.current_thread():
            thread.join(timeout=2.0)

    def __enter__(self):
        # with 语法：未连接时按构造参数自动连接
        if self._sock is None or self._closed:
            self.connect()
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.close()
        return False

    # ==================================================================
    # 帧编解码与发送
    # ==================================================================

    @staticmethod
    def encode_frame(payload):
        """按协议编码一帧：struct.pack(">I", len(payload)) + UTF-8 payload。"""
        if isinstance(payload, (bytes, bytearray)):
            data = bytes(payload)
        else:
            data = payload.encode("utf-8")
        return struct.pack(">I", len(data)) + data

    def send_frame(self, payload):
        """发送单帧。payload 为特殊文本（如 "PING"）或 JSON 命令字符串。"""
        self.send_frames([payload])

    def send_frames(self, payloads):
        """把多个帧拼接后一次性写出（单次 sendall）。

        TCP 本身可能粘包，这里主动拼接成一段字节流写出，
        可用于验证服务端按长度头正确切帧的能力。
        """
        self._ensure_connected()
        blob = b"".join(self.encode_frame(p) for p in payloads)
        with self._send_lock:
            self._sock.sendall(blob)

    # ==================================================================
    # 特殊文本帧：PING/PONG、GET_API
    # ==================================================================

    def ping(self, timeout=None):
        """发送 PING 心跳帧并等待 PONG；在超时内收到 PONG 返回 True。"""
        self._ensure_connected()
        self.send_frame("PING")
        return self.wait_pong(timeout)

    def wait_pong(self, timeout=None):
        """等待下一个 PONG 帧（只收不发），用于粘包等自主动作场景。"""
        try:
            item = self._pong_queue.get(timeout=self._effective_timeout(timeout))
        except queue.Empty:
            return False
        return item == "PONG"

    def get_api(self, class_name, timeout=None):
        """发送 GET_API:<className>，返回服务端回传的方法枚举数组（list）。

        C++ Core 无运行时反射，返回空数组 []；Android/iOS 返回
        [{"api": ..., "param_name": [...], "param_type": [...]}, ...]。
        """
        self._ensure_connected()
        self.send_frame("GET_API:" + class_name)
        return self._get_from(self._api_queue, self._effective_timeout(timeout),
                              "GET_API 结果")

    # ==================================================================
    # 命令调用
    # ==================================================================

    def call(self, api_name, params=None, thread_mode=0, ver=2, timeout=None):
        """发起单条命令调用，阻塞等待下一个 return/error 返回帧（dict）。

        :param api_name:   方法名（如 "createEngine"）
        :param params:     参数对象（任意 JSON 对象，原样透传）
        :param thread_mode: 0/缺省=后台线程执行，1=主线程执行
        :param ver:        2=自定义调用（apiName+params 透传）；
                           非 2 按嵌套形式发送，由服务端把 api 子对象
                           序列化后透传给业务（兼容旧版桌面端实现）
        :return: {"type": "return"|"error", "key": ..., "value": ...}
        """
        command = self._build_command(api_name, params, thread_mode, ver)
        self.send_frame(self._dumps(command))
        return self.wait_response(timeout)

    def call_batch(self, commands, timeout=None):
        """发送对象数组命令帧，收集与命令数量相同的 return/error 返回帧。

        协议保证（与 PROTOCOL.md §4 对齐）：连续相同 threadMode 的命令在同一
        执行器内按数组顺序执行；threadMode 切换会拆组，组间在 C++ / Android
        上可能并发，跨组回包顺序不保证。需要整帧有序时请整帧使用同一
        threadMode。

        :param commands: 命令列表，每项支持三种写法：
            1) {"apiName": ..., "params": ..., "threadMode": ..., "ver": ...}（简写）
            2) 已含 "api" 键的完整命令对象（缺省补 threadMode=0 / ver=2）
            3) 已含 "field" 键的成员变量修改命令（原样使用）
        :return: 返回帧 dict 列表，长度与 commands 相同（按收到顺序收集，
                 不保证跨 threadMode 组与命令下标一一对应）
        """
        if not commands:
            return []
        normalized = [self._normalize_command(c) for c in commands]
        self.send_frame(self._dumps(normalized))
        timeout = self._effective_timeout(timeout)
        deadline = time.monotonic() + timeout
        frames = []
        for _ in normalized:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError(
                    "批量命令仅收到 %d/%d 个返回帧（超时 %.1f 秒）"
                    % (len(frames), len(normalized), timeout))
            frames.append(self._get_from(self._resp_queue, remaining, "批量命令返回帧"))
        return frames

    def set_field(self, class_name, name_type_values, timeout=None):
        """发送静态成员变量/属性修改命令（field 帧），等待 return/error 返回帧。

        :param class_name: 类全名（如 "com.foo.DemoConfig"）
        :param name_type_values: (名字, 类型, 值) 三元组列表；
            基础类型的值为字符串，自定义类型的值为数组（构造参数列表）
        :return: {"type": "return"|"error", "key": ..., "value": ...}
        """
        if not name_type_values:
            raise ValueError("name_type_values 不能为空")
        names, types, values = [], [], []
        for item in name_type_values:
            name, typ, value = item
            names.append(name)
            types.append(typ)
            values.append(value)
        command = {
            "field": class_name,
            "param_name": names,
            "param_type": types,
            "param_value": values,
        }
        self.send_frame(self._dumps(command))
        return self.wait_response(timeout)

    def wait_response(self, timeout=None):
        """阻塞等待下一个 return/error 返回帧（dict）。

        callback 帧不会进入该队列（由回调通道单独处理），
        因此异步回调不会干扰对返回帧的等待。超时抛 TimeoutError。
        """
        return self._get_from(self._resp_queue, self._effective_timeout(timeout),
                              "return/error 响应帧")

    # ==================================================================
    # callback 订阅
    # ==================================================================

    def register_callback(self, name, handler):
        """订阅名为 name 的 callback 帧。

        handler(frame) 在接收线程中被调用（请勿阻塞），frame 为完整
        返回帧 dict：{"type": "callback", "key": 名字, "value": 文本}。
        handler 抛出的异常会被吞掉，不影响收包线程。

        未订阅的 callback 不会丢失：按名字缓存在有界队列中，
        可随时用 wait_callback(name) 取出。
        """
        if not callable(handler):
            raise TypeError("handler 必须可调用")
        with self._callback_cond:
            self._callback_handlers.setdefault(name, []).append(handler)

    def wait_callback(self, name, timeout=None):
        """等待名为 name 的 callback（优先消费缓存），返回 value 字符串。

        超时未收到返回 None。同一帧既触发已注册的 handler，也会进入缓存，
        因此 handler 与 wait_callback 可并存使用。
        """
        deadline = time.monotonic() + self._effective_timeout(timeout)
        with self._callback_cond:
            while True:
                cached = self._callback_cache.get(name)
                if cached:
                    return cached.popleft()
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    return None
                self._callback_cond.wait(remaining)

    # ==================================================================
    # 内部实现
    # ==================================================================

    def _build_command(self, api_name, params, thread_mode, ver):
        """构造嵌套形式的命令对象：{"api": {...}, "threadMode": ..., "ver": ...}。"""
        api = {"apiName": api_name, "params": params if params is not None else {}}
        return {"api": api, "threadMode": thread_mode, "ver": ver}

    def _normalize_command(self, command):
        """把 call_batch 的单条输入归一化为协议命令对象。"""
        if not isinstance(command, dict):
            raise TypeError("命令必须是 dict: %r" % (command,))
        if "apiName" in command:
            return self._build_command(command["apiName"], command.get("params"),
                                       command.get("threadMode", 0),
                                       command.get("ver", 2))
        if "api" in command or "field" in command:
            merged = dict(command)
            merged.setdefault("threadMode", 0)
            if "api" in merged:
                merged.setdefault("ver", 2)
            return merged
        raise ValueError("命令缺少 apiName/api/field 字段: %r" % (command,))

    @staticmethod
    def _dumps(obj):
        """紧凑 JSON 序列化（UTF-8，保留非 ASCII 字符）。"""
        return json.dumps(obj, ensure_ascii=False, separators=(",", ":"))

    def _effective_timeout(self, timeout):
        """取有效超时：显式传入优先，否则用构造时的默认值。"""
        return self._timeout if timeout is None else timeout

    def _ensure_connected(self):
        """发送类操作前检查连接状态。"""
        if self._sock is None or self._closed:
            raise RuntimeError("尚未连接服务端，请先调用 connect()")

    @staticmethod
    def _get_from(q, timeout, what):
        """从队列取一条消息；超时抛 TimeoutError，连接断开抛 ConnectionError。"""
        try:
            item = q.get(timeout=timeout)
        except queue.Empty:
            raise TimeoutError("等待%s超时（%.1f 秒）" % (what, timeout))
        if item is None:  # 断线哨兵
            raise ConnectionError("连接已断开")
        return item

    def _drain_queues(self):
        """清空各队列与 callback 缓存（重连前调用），订阅关系保留。"""
        for q in (self._pong_queue, self._resp_queue, self._api_queue):
            while True:
                try:
                    q.get_nowait()
                except queue.Empty:
                    break
        with self._callback_cond:
            self._callback_cache.clear()

    # ---- 后台接收线程 ----

    def _recv_loop(self):
        """接收线程主循环：阻塞读 -> 累积缓冲 -> 按长度头切帧 -> 分拣。"""
        buf = b""
        while not self._closed:
            try:
                chunk = self._sock.recv(65536)
            except OSError:
                break  # 连接被关闭（close() 的 shutdown 或网络异常）
            if not chunk:
                break  # 服务端正常关闭连接
            buf += chunk
            # 循环切帧：一次 recv 可能携带多个完整帧（粘包）；
            # 凑不齐一帧时剩余字节留在缓冲（半包），等待后续数据
            while True:
                if len(buf) < 4:
                    break  # 长度头不完整
                (payload_len,) = struct.unpack(">I", buf[:4])
                if payload_len > MAX_FRAME_PAYLOAD:
                    self._recv_error = "帧长度超限: %d" % payload_len
                    break
                if len(buf) < 4 + payload_len:
                    break  # payload 不完整
                payload = buf[4:4 + payload_len].decode("utf-8", errors="replace")
                buf = buf[4 + payload_len:]
                self._dispatch_payload(payload)
            if self._recv_error:
                break
        self._on_disconnected()

    def _dispatch_payload(self, payload):
        """把一个完整帧的 payload 分拣到对应通道。"""
        if payload == "PONG":
            self._pong_queue.put("PONG")
            return
        try:
            msg = json.loads(payload)
        except ValueError:
            return  # 非 JSON 文本（协议外内容）：忽略
        if isinstance(msg, dict):
            if msg.get("type") == "callback":
                self._handle_callback(msg)
            else:
                # return / error 帧（以及带 type 的扩展对象帧）
                self._resp_queue.put(msg)
        elif isinstance(msg, list):
            # GET_API 的反射枚举结果
            self._api_queue.put(msg)
        # 其余顶层类型：忽略

    def _handle_callback(self, frame):
        """处理 callback 帧：写入按名字缓存，并触发已注册的 handler。"""
        name = frame.get("key")
        value = frame.get("value")
        with self._callback_cond:
            cache = self._callback_cache.setdefault(
                name, deque(maxlen=MAX_CALLBACK_CACHE))
            cache.append(value)
            handlers = list(self._callback_handlers.get(name, ()))
            self._callback_cond.notify_all()  # 唤醒 wait_callback 的等待方
        # 在锁外调用 handler，避免死锁；异常吞掉，保证收包线程健壮
        for handler in handlers:
            try:
                handler(frame)
            except Exception:
                pass

    def _on_disconnected(self):
        """接收线程退出（连接断开/协议错误）：置位并唤醒所有等待方。"""
        self._closed = True
        # 向各队列投入断线哨兵，让阻塞的 wait_* 立刻以 ConnectionError 结束
        for q in (self._pong_queue, self._resp_queue, self._api_queue):
            q.put(None)
        with self._callback_cond:
            self._callback_cond.notify_all()
