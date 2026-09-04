// =====================================================================
// test/TestCore.cpp
// xprobe 核心库单元/集成测试（基于 assert）：
//   1. JSON 解析 / 序列化 / 访问 / 错误处理
//   2. FrameCodec 分块 / 粘包 / 半包
//   3. MessageParser 单对象 / 数组 / ver2 / field / 参数长度不一致 / 解析失败
//   4. ThreadPool 任务执行
//   5. PING/PONG/GET_API 特殊帧与 AutoTestMgr 全链路（真实 TCP 回环）
//   6. 日志回调（注册收集/解析错误与连接建立的 level 与 tag/传 nullptr 恢复默认）
//   7. 跨端契约（单帧上限 32MiB / sendCallbackJson / 数组非法元素整帧拒绝）
// =====================================================================
#ifdef _WIN32
// 测试客户端同样需要 winsock
#ifndef FD_SETSIZE
#define FD_SETSIZE 4096
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

// 强制启用 assert：撤销构建类型（如 Release -DNDEBUG）对断言的禁用，
// 保证测试目标在任何配置下都真实执行校验
#undef NDEBUG
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "xprobe/AutoTestMgr.h"
#include "xprobe/json.h"
#include "xprobe/logger.h"
#include "xprobe/protocol.h"
#include "xprobe/ThreadPool.h"

// =====================================================================
// 测试辅助
// =====================================================================

static int gPassed = 0; // 已通过用例计数

// 输出通过日志并计数
static void pass(const char* name) {
    ++gPassed;
    std::printf("[PASS] %s\n", name);
}

// =====================================================================
// 1. JSON 测试
// =====================================================================

static void testJsonBasicTypes() {
    std::string err;

    // null
    xprobe::JsonValue v = xprobe::JsonValue::parse("null", &err);
    assert(err.empty() && v.isNull());
    assert(v.serialize() == "null");

    // true / false
    v = xprobe::JsonValue::parse("true", &err);
    assert(err.empty() && v.isBool() && v.asBool());
    v = xprobe::JsonValue::parse("false", &err);
    assert(err.empty() && v.isBool() && !v.asBool());
    assert(v.serialize() == "false");

    // 整数（含负数、0、int64 极值）
    v = xprobe::JsonValue::parse("42", &err);
    assert(err.empty() && v.isInt() && !v.isDouble() && v.asInt() == 42);
    assert(v.serialize() == "42");
    v = xprobe::JsonValue::parse("-7", &err);
    assert(v.isInt() && v.asInt() == -7);
    v = xprobe::JsonValue::parse("0", &err);
    assert(v.isInt() && v.asInt() == 0);
    v = xprobe::JsonValue::parse("9223372036854775807", &err);
    assert(v.isInt() && v.asInt() == 9223372036854775807LL);

    // 浮点
    v = xprobe::JsonValue::parse("3.25", &err);
    assert(v.isDouble() && !v.isInt() && v.asDouble() == 3.25);
    v = xprobe::JsonValue::parse("-0.5", &err);
    assert(v.isDouble() && v.asDouble() == -0.5);
    v = xprobe::JsonValue::parse("1e3", &err);
    assert(v.isDouble() && v.asDouble() == 1000.0);
    v = xprobe::JsonValue::parse("2.0", &err);
    assert(v.isDouble() && v.serialize() == "2");

    // 数字统一数值访问
    v = xprobe::JsonValue::parse("7", &err);
    assert(v.asDouble() == 7.0 && v.isNumber());

    pass("json 基本类型（null/bool/int/double）");
}

static void testJsonStringEscapes() {
    std::string err;

    // 常规转义
    xprobe::JsonValue v = xprobe::JsonValue::parse("\"a\\\"b\\\\c\\/d\\be\\ff\\ng\\rh\\ti\"", &err);
    assert(err.empty() && v.isString());
    assert(v.asString() == "a\"b\\c/d\be\ff\ng\rh\ti");

    // 序列化还原：紧凑格式
    assert(v.serialize() == "\"a\\\"b\\\\c/d\\be\\ff\\ng\\rh\\ti\"");

    // \uXXXX 基本多文种平面（中文“中文”）
    v = xprobe::JsonValue::parse("\"\\u4e2d\\u6587\"", &err);
    assert(err.empty() && v.asString() == "中文");

    // \uXXXX 转义拉丁字符
    v = xprobe::JsonValue::parse("\"\\u0041\"", &err);
    assert(v.asString() == "A");

    // 代理对（U+1F600）
    v = xprobe::JsonValue::parse("\"\\uD83D\\uDE00\"", &err);
    assert(err.empty() && v.asString() == "\xF0\x9F\x98\x80");

    // UTF-8 原文直接解析并往返
    v = xprobe::JsonValue::parse("\"你好世界\"", &err);
    assert(err.empty() && v.asString() == "你好世界");
    assert(v.serialize() == "\"你好世界\"");

    // 控制字符 \u0001
    v = xprobe::JsonValue::parse("\"\\u0001\"", &err);
    assert(err.empty() && v.asString().size() == 1 && v.asString()[0] == '\x01');
    assert(v.serialize() == "\"\\u0001\"");

    pass("json 字符串转义（\\\" \\\\ \\/ \\b \\f \\n \\r \\t \\uXXXX 代理对）");
}

static void testJsonComposite() {
    std::string err;

    // 数组（含嵌套）
    xprobe::JsonValue v = xprobe::JsonValue::parse("[1,[2,3],\"x\",null,true]", &err);
    assert(err.empty() && v.isArray() && v.size() == 5);
    assert(v[0].asInt() == 1);
    assert(v[1].size() == 2 && v[1][1].asInt() == 3);
    assert(v[2].asString() == "x");
    assert(v[3].isNull() && v[4].asBool());
    assert(v.serialize() == "[1,[2,3],\"x\",null,true]");

    // 空数组 / 空对象
    v = xprobe::JsonValue::parse("[]", &err);
    assert(v.isArray() && v.size() == 0 && v.serialize() == "[]");
    v = xprobe::JsonValue::parse("{}", &err);
    assert(v.isObject() && v.size() == 0 && v.serialize() == "{}");

    // 对象（含嵌套与混合类型）
    v = xprobe::JsonValue::parse("{\"api\":\"add\",\"n\":1,\"obj\":{\"k\":[1.5,false]}}", &err);
    assert(err.empty() && v.isObject() && v.size() == 3);
    assert(v.isMember("api") && !v.isMember("nope"));
    assert(v["api"].asString() == "add");
    assert(v["n"].asInt() == 1);
    assert(v["obj"]["k"][0].asDouble() == 1.5);
    assert(v["obj"]["k"][1].asBool() == false);

    // 对象键序列化按字典序（输出稳定）
    assert(v.serialize() == "{\"api\":\"add\",\"n\":1,\"obj\":{\"k\":[1.5,false]}}");

    // 缺键访问返回 null 占位（不崩溃）；非 const 下标具有“插入 null 成员”语义
    assert(v["missing"].isNull());
    assert(v.isMember("missing")); // 非 const 访问会创建成员
    const xprobe::JsonValue& cv = v;
    // const 访问不修改对象：未触碰的键返回 null 占位且 find 为空
    assert(cv["never_touched"].isNull());
    assert(cv.find("never_touched") == nullptr);
    assert(!v.isMember("never_touched"));

    pass("json 数组/对象/嵌套/键序");
}

static void testJsonBuildAndModify() {
    // 链式构建对象
    xprobe::JsonValue obj = xprobe::JsonValue::makeObject();
    obj["type"] = "return";
    obj["code"] = 200;
    obj["ratio"] = 0.5;
    obj["list"].append(1).append("two").append(true);
    assert(obj.serialize() == "{\"code\":200,\"list\":[1,\"two\",true],\"ratio\":0.5,\"type\":\"return\"}");

    // 重复键后者覆盖
    obj["code"] = 404;
    assert(obj["code"].asInt() == 404 && obj.size() == 4);

    // 删除键
    assert(obj.erase("ratio") && !obj.isMember("ratio") && !obj.erase("ratio"));

    // 越界访问不崩溃
    assert(obj["list"][9].isNull());

    // round-trip：parse(serialize(j)) == j
    std::string err;
    xprobe::JsonValue round = xprobe::JsonValue::parse(obj.serialize(), &err);
    assert(err.empty() && round == obj);

    // 相等比较
    assert(xprobe::JsonValue(1) == xprobe::JsonValue::parse("1"));
    assert(xprobe::JsonValue(1) != xprobe::JsonValue(1.0)); // int 与 double 类型不同
    assert(xprobe::JsonValue("a") != xprobe::JsonValue("b"));

    pass("json 构建/修改/round-trip/相等比较");
}

static void testJsonParseErrors() {
    std::string err;

    // 各种非法输入
    const char* badInputs[] = {
        "",             // 空串
        "{",            // 对象不完整
        "{\"a\":}",     // 缺值
        "{\"a\":1,}",   // 尾逗号
        "{'a':1}",      // 单引号
        "[1,2",         // 数组不完整
        "\"unterminated", // 字符串缺右引号
        "tru",          // 字面量不完整
        "nulx",         // 非法字面量
        "{\"a\":1} xx", // 尾部多余内容
        "01a",          // 非法数字
        "\"\\q\"",      // 非法转义
        "\"\\u12\"",    // \u 缺位数
        "+1",           // JSON 不允许前导加号
    };
    for (const char* s : badInputs) {
        err.clear();
        xprobe::JsonValue v = xprobe::JsonValue::parse(s, &err);
        assert(!err.empty() && v.isNull());
    }

    // 失败时 err 含位置信息
    xprobe::JsonValue::parse("{", &err);
    assert(err.find("offset") != std::string::npos);

    pass("json 解析错误用例（14 种非法输入）");
}

// =====================================================================
// 2. FrameCodec 测试
// =====================================================================

static void testFrameCodecEncode() {
    // 编码：4 字节大端长度头 + payload
    std::string frame = xprobe::FrameCodec::encode("PING");
    assert(frame.size() == 8);
    assert(static_cast<unsigned char>(frame[0]) == 0x00);
    assert(static_cast<unsigned char>(frame[1]) == 0x00);
    assert(static_cast<unsigned char>(frame[2]) == 0x00);
    assert(static_cast<unsigned char>(frame[3]) == 0x04);
    assert(frame.substr(4) == "PING");

    // 大 payload 长度头（0x0102 = 258）
    std::string big(258, 'x');
    frame = xprobe::FrameCodec::encode(big);
    assert(static_cast<unsigned char>(frame[0]) == 0x00 &&
           static_cast<unsigned char>(frame[1]) == 0x00 &&
           static_cast<unsigned char>(frame[2]) == 0x01 &&
           static_cast<unsigned char>(frame[3]) == 0x02);

    // 空 payload
    frame = xprobe::FrameCodec::encode("");
    assert(frame.size() == 4 && frame[3] == '\0');

    pass("FrameCodec 编码（长度头大端序）");
}

static void testFrameCodecWholeAndSticky() {
    xprobe::FrameCodec codec;

    // 整帧一次喂入
    std::string f1 = xprobe::FrameCodec::encode("{\"ver\":2}");
    auto frames = codec.feed(f1);
    assert(frames.size() == 1 && frames[0] == "{\"ver\":2}");

    // 粘包：两帧一次喂入
    std::string f2 = xprobe::FrameCodec::encode("PING");
    std::string f3 = xprobe::FrameCodec::encode("hello");
    frames = codec.feed(f2 + f3);
    assert(frames.size() == 2 && frames[0] == "PING" && frames[1] == "hello");

    // 三帧粘包 + 末尾半包
    std::string f4 = xprobe::FrameCodec::encode("A");
    std::string f5 = xprobe::FrameCodec::encode("BB");
    std::string f6 = xprobe::FrameCodec::encode("CCC");
    codec.reset();
    frames = codec.feed(f4 + f5 + f6.substr(0, 3)); // CCC 只给前 2 字节 + ... 此处 3 字节含头
    assert(frames.size() == 2 && frames[0] == "A" && frames[1] == "BB");
    frames = codec.feed(f6.substr(3)); // 补齐 payload 剩余
    assert(frames.size() == 1 && frames[0] == "CCC");

    pass("FrameCodec 粘包/整帧/半包组合");
}

static void testFrameCodecByteByByte() {
    // 逐字节喂入：模拟最细粒度半包
    xprobe::FrameCodec codec;
    std::string payload = "{\"api\":{\"apiName\":\"createEngine\"},\"ver\":2}";
    std::string frame = xprobe::FrameCodec::encode(payload);

    std::vector<std::string> collected;
    for (char c : frame) {
        auto frames = codec.feed(std::string(1, c));
        collected.insert(collected.end(), frames.begin(), frames.end());
    }
    assert(collected.size() == 1 && collected[0] == payload);

    // 再来两帧逐字节混合喂入
    std::string fa = xprobe::FrameCodec::encode("first");
    std::string fb = xprobe::FrameCodec::encode("second");
    codec.reset();
    collected.clear();
    for (char c : fa + fb) {
        auto frames = codec.feed(std::string(1, c));
        collected.insert(collected.end(), frames.begin(), frames.end());
    }
    assert(collected.size() == 2 && collected[0] == "first" && collected[1] == "second");

    pass("FrameCodec 逐字节分块喂入");
}

// =====================================================================
// 3. MessageParser 测试
// =====================================================================

static void testMessageParserVer2() {
    // ver2 单对象
    std::vector<xprobe::Command> cmds = xprobe::MessageParser::parse(
        "{\"api\":{\"apiName\":\"createEngine\",\"params\":{\"appId\":\"123\",\"sceneId\":1}},"
        "\"threadMode\":1,\"ver\":2}");
    assert(cmds.size() == 1);
    const xprobe::Command& c = cmds[0];
    assert(c.cmdType == xprobe::CmdType::Method);
    assert(c.isVer2 && c.ver == 2);
    assert(c.apiName == "createEngine");
    assert(c.threadMode == 1);
    assert(c.params.isObject() && c.params["appId"].asString() == "123");
    assert(c.params["sceneId"].asInt() == 1);
    assert(c.commandName == "createEngine");
    // 原始 api 子对象保留
    assert(c.apiRaw.isObject() && c.apiRaw["apiName"].asString() == "createEngine");

    // ver 为字符串 "2"（三端一致，见 PROTOCOL.md §7.3）
    cmds = xprobe::MessageParser::parse(
        "{\"api\":{\"apiName\":\"joinRoom\",\"params\":{\"roomName\":\"r1\"}},\"ver\":\"2\"}");
    assert(cmds.size() == 1 && cmds[0].isVer2 && cmds[0].ver == 2);
    assert(cmds[0].apiName == "joinRoom");
    assert(cmds[0].params["roomName"].asString() == "r1");
    assert(cmds[0].threadMode == 0); // 缺省为后台

    // ver2 缺省 params
    cmds = xprobe::MessageParser::parse(
        "{\"api\":{\"apiName\":\"noParams\"},\"ver\":2}");
    assert(cmds[0].params.isObject() && cmds[0].params.size() == 0);

    pass("MessageParser ver2（整数与字符串 \"2\"）");
}

static void testMessageParserVer1() {
    // ver1 扁平形式
    std::vector<xprobe::Command> cmds = xprobe::MessageParser::parse(
        "{\"api\":\"com.foo.Bar.addSubscribe\",\"param_name\":[\"room\",\"uid\"],"
        "\"param_type\":[\"String\",\"String\"],\"param_value\":[\"134\",\"2345\"],"
        "\"threadMode\":0}");
    assert(cmds.size() == 1);
    const xprobe::Command& c = cmds[0];
    assert(!c.isVer2 && c.ver == 0); // ver 缺省
    assert(c.cmdType == xprobe::CmdType::Method);
    assert(c.apiName == "com.foo.Bar.addSubscribe");
    assert(c.threadMode == 0);
    // 兼容模式：apiRaw 为字符串，执行时序列化透传
    assert(c.apiRaw.isString() && c.apiRaw.asString() == "com.foo.Bar.addSubscribe");

    // ver1 嵌套形式（iOS 风格）
    cmds = xprobe::MessageParser::parse(
        "{\"api\":{\"apiName\":\"com.foo.Engine.joinRoom:roomName:uid:\","
        "\"paramName\":[\"token\",\"roomName\",\"uid\"],"
        "\"paramType\":[\"NSString\",\"NSString\",\"NSString\"],"
        "\"paramValue\":[\"1\",\"1\",\"1\"]},\"threadMode\":1,\"ver\":1}");
    assert(cmds.size() == 1);
    const xprobe::Command& n = cmds[0];
    assert(!n.isVer2 && n.ver == 1);
    assert(n.apiName == "com.foo.Engine.joinRoom:roomName:uid:");
    assert(n.threadMode == 1);
    assert(n.apiRaw.isObject() && n.apiRaw["apiName"].asString() == "com.foo.Engine.joinRoom:roomName:uid:");
    // 嵌套参数数组三长一致：解析通过
    assert(n.apiRaw["paramName"].size() == 3);

    pass("MessageParser ver1 扁平/嵌套形式");
}

static void testMessageParserArray() {
    // 数组形式：顺序保持、混合 threadMode 与 ver
    std::vector<xprobe::Command> cmds = xprobe::MessageParser::parse(
        "[{\"api\":{\"apiName\":\"m1\",\"params\":{}},\"ver\":2},"
        "{\"api\":\"com.a.B.c\",\"threadMode\":1},"
        "{\"api\":{\"apiName\":\"m3\",\"params\":{\"x\":9}},\"threadMode\":1,\"ver\":2}]");
    assert(cmds.size() == 3);
    assert(cmds[0].isVer2 && cmds[0].apiName == "m1" && cmds[0].threadMode == 0);
    assert(!cmds[1].isVer2 && cmds[1].apiName == "com.a.B.c" && cmds[1].threadMode == 1);
    assert(cmds[2].isVer2 && cmds[2].apiName == "m3" && cmds[2].params["x"].asInt() == 9);

    // 空数组：合法（0 条命令）
    cmds = xprobe::MessageParser::parse("[]");
    assert(cmds.empty());

    pass("MessageParser 数组命令（顺序/混合模式）");
}

static void testMessageParserField() {
    // field 命令（修改静态成员变量）
    std::vector<xprobe::Command> cmds = xprobe::MessageParser::parse(
        "{\"field\":\"com.foo.DemoConfig\",\"param_name\":[\"mUid\",\"mChannelId\"],"
        "\"param_type\":[\"String\",\"String\"],\"param_value\":[\"234\",\"567\"]}");
    assert(cmds.size() == 1);
    const xprobe::Command& c = cmds[0];
    assert(c.cmdType == xprobe::CmdType::Field);
    assert(c.apiName == "com.foo.DemoConfig");
    assert(c.commandName == "com.foo.DemoConfig");
    assert(!c.isVer2);
    // 兼容模式：整条命令对象作为 apiRaw 透传
    assert(c.apiRaw.isObject() && c.apiRaw["field"].asString() == "com.foo.DemoConfig");
    assert(c.apiRaw["param_value"][0].asString() == "234");

    pass("MessageParser field 命令");
}

static void testMessageParserErrors() {
    bool thrown = false;

    // 参数长度不一致（扁平：3-3-2）
    try {
        xprobe::MessageParser::parse(
            "{\"api\":\"com.a.B.c\",\"param_name\":[\"a\",\"b\",\"c\"],"
            "\"param_type\":[\"int\",\"int\",\"int\"],\"param_value\":[\"1\",\"2\"]}");
        assert(false && "应当抛出异常");
    } catch (const xprobe::MessageParseException& e) {
        thrown = true;
        // 错误消息含原文
        assert(std::string(e.what()).find("com.a.B.c") != std::string::npos);
        assert(std::string(e.what()).find("长度不一致") != std::string::npos);
    }
    assert(thrown);

    // 参数长度不一致（嵌套：2-2-1）
    thrown = false;
    try {
        xprobe::MessageParser::parse(
            "{\"api\":{\"apiName\":\"m\",\"paramName\":[\"a\",\"b\"],"
            "\"paramType\":[\"int\",\"int\"],\"paramValue\":[\"1\"]}}");
        assert(false && "应当抛出异常");
    } catch (const xprobe::MessageParseException&) {
        thrown = true;
    }
    assert(thrown);

    // 缺少部分参数数组（param_name 有、param_type 无）
    thrown = false;
    try {
        xprobe::MessageParser::parse(
            "{\"api\":\"com.a.B.c\",\"param_name\":[\"a\"]}");
        assert(false && "应当抛出异常");
    } catch (const xprobe::MessageParseException&) {
        thrown = true;
    }
    assert(thrown);

    // JSON 语法错误：错误消息含原文
    thrown = false;
    try {
        xprobe::MessageParser::parse("{\"api\": bad json}");
        assert(false && "应当抛出异常");
    } catch (const xprobe::MessageParseException& e) {
        thrown = true;
        assert(std::string(e.what()).find("bad json") != std::string::npos);
        assert(std::string(e.what()).find("json parse failed") != std::string::npos);
    }
    assert(thrown);

    // 顶层非对象/数组
    for (const char* bad : {"42", "\"str\"", "true"}) {
        thrown = false;
        try {
            xprobe::MessageParser::parse(bad);
        } catch (const xprobe::MessageParseException& e) {
            thrown = true;
            assert(std::string(e.what()).find(bad) != std::string::npos);
        }
        assert(thrown);
    }

    // 数组内元素不是对象
    thrown = false;
    try {
        xprobe::MessageParser::parse("[{\"api\":\"a\"}, 42]");
    } catch (const xprobe::MessageParseException&) {
        thrown = true;
    }
    assert(thrown);

    // 缺少 api 字段
    thrown = false;
    try {
        xprobe::MessageParser::parse("{\"threadMode\":1}");
    } catch (const xprobe::MessageParseException&) {
        thrown = true;
    }
    assert(thrown);

    // api.apiName 缺失（嵌套形式）
    thrown = false;
    try {
        xprobe::MessageParser::parse("{\"api\":{\"paramName\":[]}}");
    } catch (const xprobe::MessageParseException&) {
        thrown = true;
    }
    assert(thrown);

    pass("MessageParser 错误用例（长度不一致/语法错误/结构错误）");
}

// =====================================================================
// 4. ThreadPool 测试
// =====================================================================

static void testThreadPool() {
    xprobe::ThreadPool pool(8);
    std::mutex mtx;
    std::condition_variable cv;
    int done = 0;
    const int kTotal = 200;

    // 提交 200 个并发任务（超过线程数，验证队列排队执行）
    for (int i = 0; i < kTotal; ++i) {
        pool.submit([&mtx, &cv, &done]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            std::lock_guard<std::mutex> lock(mtx);
            ++done;
            if (done == kTotal) {
                cv.notify_one();
            }
        });
    }

    // 等待全部任务完成
    {
        std::unique_lock<std::mutex> lock(mtx);
        bool finished = cv.wait_for(lock, std::chrono::seconds(10), [&done] { return done == kTotal; });
        assert(finished);
    }

    // shutdown 后可安全重复调用
    pool.shutdown();
    pool.shutdown();

    // shutdown 后提交被忽略（不死锁不崩溃）
    pool.submit([]() {});
    assert(pool.isStopped());

    pass("ThreadPool（8 线程 200 任务全部完成/shutdown 幂等）");
}

// =====================================================================
// 5. 集成测试：真实 TCP 回环（PING/PONG/GET_API/命令全链路）
// =====================================================================

// 测试用客户端套接字封装
struct TestClient {
#ifdef _WIN32
    SOCKET fd = INVALID_SOCKET;
#else
    int fd = -1;
#endif
    xprobe::FrameCodec codec;        // 客户端侧切帧
    std::vector<std::string> pending; // 已解码但尚未被消费的帧（一次 recv 可能含多帧）

    explicit TestClient(uint16_t port) {
        fd = ::socket(AF_INET, SOCK_STREAM, 0);
#ifdef _WIN32
        assert(fd != INVALID_SOCKET);
#else
        assert(fd >= 0);
#endif
        sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        addr.sin_addr.s_addr = htonl(0x7F000001); // 127.0.0.1
        int rc = ::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
        assert(rc == 0);
    }

    ~TestClient() {
#ifdef _WIN32
        closesocket(fd);
#else
        ::close(fd);
#endif
    }

    // 发送一帧
    void sendFrame(const std::string& payload) {
        std::string frame = xprobe::FrameCodec::encode(payload);
        size_t sent = 0;
        while (sent < frame.size()) {
            int n = static_cast<int>(::send(fd, frame.data() + sent, frame.size() - sent, 0));
            assert(n > 0);
            sent += static_cast<size_t>(n);
        }
    }

    // 带超时读取一帧；超时返回 false（一次收到的多余帧缓存于 pending 供后续读取）
    bool recvFrame(std::string* out, int timeoutMs) {
        // 先消费已缓存的帧
        if (!pending.empty()) {
            *out = pending.front();
            pending.erase(pending.begin());
            return true;
        }
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        char buf[4096];
        while (true) {
            auto remain = std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - std::chrono::steady_clock::now());
            if (remain.count() <= 0) {
                return false; // 超时
            }
            // select 等待可读
            fd_set rs;
            FD_ZERO(&rs);
            FD_SET(fd, &rs);
            timeval tv;
            tv.tv_sec = remain.count() / 1000;
            tv.tv_usec = (remain.count() % 1000) * 1000;
            int rc = ::select(static_cast<int>(fd) + 1, &rs, nullptr, nullptr, &tv);
            if (rc <= 0) {
                continue; // 超时或中断：下轮检查 deadline
            }
            int n = static_cast<int>(::recv(fd, buf, sizeof(buf), 0));
            if (n <= 0) {
                return false; // 连接关闭
            }
            // 切帧：可能一次产出多帧，全部缓存后逐帧消费
            std::vector<std::string> frames = codec.feed(buf, static_cast<size_t>(n));
            pending.insert(pending.end(), frames.begin(), frames.end());
            if (!pending.empty()) {
                *out = pending.front();
                pending.erase(pending.begin());
                return true;
            }
            // 半包：继续收
        }
    }
};

// 记录调用事件的 invocation（用于全链路验证）
struct RecordingInvocation : public xprobe::ICustomInvocation {
    struct Call {
        std::string apiName;
        std::string paramsJson;
    };

    std::mutex mtx;
    std::condition_variable cv;
    std::vector<Call> calls;

    void callMethod(const std::string& apiName, const xprobe::JsonValue& params) override {
        std::lock_guard<std::mutex> lock(mtx);
        calls.push_back(Call{apiName, params.serialize()});
        cv.notify_all();
    }

    // 等待累计调用数达到 want（带超时）
    bool waitFor(size_t want, int timeoutMs) {
        std::unique_lock<std::mutex> lock(mtx);
        return cv.wait_for(lock, std::chrono::milliseconds(timeoutMs),
                           [this, want] { return calls.size() >= want; });
    }

    size_t count() {
        std::lock_guard<std::mutex> lock(mtx);
        return calls.size();
    }

    // 清空调用记录（测试分阶段隔离）
    void clearCalls() {
        std::lock_guard<std::mutex> lock(mtx);
        calls.clear();
    }
};

static void testIntegration() {
    // 使用独立端口，避免与 demo 的 9000 冲突
    const uint16_t kPort = 19765;

    xprobe::AutoTestMgr mgr;
    RecordingInvocation inv;

    mgr.start(&inv, kPort);
    std::this_thread::sleep_for(std::chrono::milliseconds(100)); // 等待监听就绪

    {
        TestClient client(kPort);

        // ---- PING -> PONG ----
        client.sendFrame("PING");
        std::string resp;
        assert(client.recvFrame(&resp, 2000) && resp == "PONG");

        // ---- PING 粘包 x2 -> PONG x2 ----
        std::string twoPings = xprobe::FrameCodec::encode("PING") + xprobe::FrameCodec::encode("PING");
        size_t sent = 0;
        while (sent < twoPings.size()) {
            int n = static_cast<int>(::send(client.fd, twoPings.data() + sent, twoPings.size() - sent, 0));
            assert(n > 0);
            sent += static_cast<size_t>(n);
        }
        assert(client.recvFrame(&resp, 2000) && resp == "PONG");
        assert(client.recvFrame(&resp, 2000) && resp == "PONG");

        // ---- PONG -> 忽略（500ms 内无任何响应） ----
        client.sendFrame("PONG");
        assert(!client.recvFrame(&resp, 500));

        // ---- GET_API -> [] ----
        client.sendFrame("GET_API:com.foo.Bar");
        assert(client.recvFrame(&resp, 2000) && resp == "[]");

        // ---- ver2 命令全链路：解析->线程池->invocation->sendReturn ----
        std::string cmd = "{\"api\":{\"apiName\":\"createEngine\",\"params\":{\"appId\":\"app-1\",\"sceneId\":7}},"
                          "\"threadMode\":0,\"ver\":2}";
        client.sendFrame(cmd);
        assert(inv.waitFor(1, 3000));
        {
            std::lock_guard<std::mutex> lock(inv.mtx);
            assert(inv.calls[0].apiName == "createEngine");
            assert(inv.calls[0].paramsJson == "{\"appId\":\"app-1\",\"sceneId\":7}");
        }
        // demo 风格：invocation 不回发结果时由测试侧手动 sendReturn 验证回程链路
        mgr.sendReturn("createEngine", "engine_1");
        assert(client.recvFrame(&resp, 2000));
        {
            std::string err;
            xprobe::JsonValue j = xprobe::JsonValue::parse(resp, &err);
            assert(err.empty());
            assert(j["type"].asString() == "return");
            assert(j["key"].asString() == "createEngine");
            assert(j["value"].asString() == "engine_1");
        }

        // ---- 数组命令（同一帧多条）作为一个任务顺序执行 ----
        inv.clearCalls();
        std::string arrCmd = "[{\"api\":{\"apiName\":\"a1\",\"params\":{}},\"ver\":2},"
                             "{\"api\":{\"apiName\":\"a2\",\"params\":{}},\"ver\":2},"
                             "{\"api\":{\"apiName\":\"a3\",\"params\":{}},\"ver\":2}]";
        client.sendFrame(arrCmd);
        assert(inv.waitFor(3, 3000));
        {
            std::lock_guard<std::mutex> lock(inv.mtx);
            assert(inv.calls[0].apiName == "a1"); // 顺序保持
            assert(inv.calls[1].apiName == "a2");
            assert(inv.calls[2].apiName == "a3");
        }

        // ---- 兼容模式：ver!=2 透传 api 子对象序列化串 ----
        inv.clearCalls();
        client.sendFrame("{\"api\":\"com.foo.Bar.addSubscribe\",\"param_name\":[\"room\"],"
                         "\"param_type\":[\"String\"],\"param_value\":[\"134\"]}");
        assert(inv.waitFor(1, 3000));
        {
            std::lock_guard<std::mutex> lock(inv.mtx);
            // api 为字符串时，序列化结果是带引号的 JSON 字符串
            assert(inv.calls[0].apiName == "\"com.foo.Bar.addSubscribe\"");
            assert(inv.calls[0].paramsJson == "{}");
        }

        // ---- 解析失败：回报 error/parseError（含原文） ----
        client.sendFrame("{\"api\": bad}");
        assert(client.recvFrame(&resp, 2000));
        {
            std::string err;
            xprobe::JsonValue j = xprobe::JsonValue::parse(resp, &err);
            assert(err.empty());
            assert(j["type"].asString() == "error");
            assert(j["key"].asString() == "parseError");
            assert(j["value"].asString().find("bad") != std::string::npos); // 错误消息含原文
        }

        // ---- sendCallback ----
        mgr.sendCallback("onConnectionStatus", "status: 0");
        assert(client.recvFrame(&resp, 2000));
        {
            std::string err;
            xprobe::JsonValue j = xprobe::JsonValue::parse(resp, &err);
            assert(j["type"].asString() == "callback");
            assert(j["key"].asString() == "onConnectionStatus");
            assert(j["value"].asString() == "status: 0");
        }
    }

    // ---- 多客户端：sendReturn 发往最近活跃连接 ----
    {
        TestClient c1(kPort);
        TestClient c2(kPort);
        std::this_thread::sleep_for(std::chrono::milliseconds(150));

        // c1 先活跃，c2 后活跃：回包应到 c2
        c1.sendFrame("PING");
        std::string resp;
        assert(c1.recvFrame(&resp, 2000) && resp == "PONG");
        c2.sendFrame("PING");
        assert(c2.recvFrame(&resp, 2000) && resp == "PONG");

        mgr.sendReturn("latest", "c2");
        assert(c2.recvFrame(&resp, 2000));
        {
            std::string err;
            xprobe::JsonValue j = xprobe::JsonValue::parse(resp, &err);
            assert(j["key"].asString() == "latest" && j["value"].asString() == "c2");
        }
        // c1 不应收到
        assert(!c1.recvFrame(&resp, 300));
    }

    // 停止服务（析构前显式调用，验证可重复 stop）
    mgr.stop();
    mgr.stop();

    pass("集成测试（PING/PONG/GET_API/ver2/数组/兼容模式/解析错误/多客户端/stop）");
}

// =====================================================================
// 6. 日志回调测试
// =====================================================================

static void testLoggerCallback() {
    // 级别常量命名空间别名（简化引用）
    namespace LogLevel = xprobe::LogLevel;

    // 级别常量对齐规范（0..4）
    assert(LogLevel::VERBOSE == 0 && LogLevel::DEBUG == 1 && LogLevel::INFO == 2 &&
           LogLevel::WARN == 3 && LogLevel::ERROR == 4);

    // ---- 收集式回调：验证 log() 直接透传 level/tag/msg ----
    struct LogItem {
        int level;
        std::string tag;
        std::string msg;
    };
    std::mutex mtx;
    std::vector<LogItem> items;
    auto collector = [&mtx, &items](int level, const std::string& tag, const std::string& msg) {
        std::lock_guard<std::mutex> lock(mtx);
        items.push_back(LogItem{level, tag, msg});
    };

    xprobe::setLogCallback(collector);
    assert(xprobe::getLogCallback()); // 注册后可查询到

    xprobe::log(LogLevel::WARN, "test", "直接打点");
    {
        std::lock_guard<std::mutex> lock(mtx);
        assert(items.size() == 1);
        assert(items[0].level == LogLevel::WARN);
        assert(items[0].tag == "test");
        assert(items[0].msg == "直接打点");
    }

    // ---- 触发一条解析错误：parser 模块以 WARN/tag="parser" 打点 ----
    items.clear();
    try {
        xprobe::MessageParser::parse("{\"api\": bad json}");
        assert(false && "应当抛出异常");
    } catch (const xprobe::MessageParseException&) {
    }
    {
        std::lock_guard<std::mutex> lock(mtx);
        assert(items.size() == 1);
        assert(items[0].level == LogLevel::WARN && items[0].tag == "parser");
        assert(items[0].msg.find("命令解析失败") != std::string::npos);
    }

    // ---- 触发连接建立：tcp 模块以 INFO/tag="tcp" 打点（含服务启动日志） ----
    items.clear();
    {
        const uint16_t kPort = 19766;
        xprobe::AutoTestMgr mgr;
        RecordingInvocation inv;
        mgr.start(&inv, kPort);
        std::this_thread::sleep_for(std::chrono::milliseconds(100)); // 等待监听就绪

        {
            TestClient client(kPort);
            client.sendFrame("PING");
            std::string resp;
            assert(client.recvFrame(&resp, 2000) && resp == "PONG");
        }
        mgr.stop();

        std::lock_guard<std::mutex> lock(mtx);
        bool sawMgrStart = false, sawTcpStart = false, sawConn = false;
        for (const auto& it : items) {
            if (it.level == LogLevel::INFO && it.tag == "mgr" &&
                it.msg.find("启动") != std::string::npos) {
                sawMgrStart = true;
            }
            if (it.level == LogLevel::INFO && it.tag == "tcp" &&
                it.msg.find("启动") != std::string::npos) {
                sawTcpStart = true;
            }
            if (it.level == LogLevel::INFO && it.tag == "tcp" &&
                it.msg.find("连接建立") != std::string::npos) {
                sawConn = true;
            }
        }
        assert(sawMgrStart && sawTcpStart && sawConn);
    }

    // ---- 传 nullptr 恢复默认输出：不崩溃，且回调查询为空 ----
    xprobe::setLogCallback(nullptr);
    assert(!xprobe::getLogCallback());
    xprobe::log(LogLevel::INFO, "test", "默认输出路径（stdout）");
    xprobe::log(LogLevel::ERROR, "test", "默认输出路径（stderr）");

    pass("日志回调（直接打点/解析错误/连接建立/恢复默认）");
}

// =====================================================================
// 7. 跨端契约：PROTOCOL.md v1.0 §2 / §4 / §6 在 C++ 侧的落地
//    这三项是「测试脚本零改动跨端复用」的前提，任一项漂移都会让同一份脚本
//    在不同平台表现不一致，因此固化为回归测试。
// =====================================================================

// 触发 sendCallbackJson 的 invocation（§6 三端统一语义）
struct CallbackJsonInvocation : public xprobe::ICustomInvocation {
    xprobe::AutoTestMgr* mgr = nullptr;

    void callMethod(const std::string& apiName, const xprobe::JsonValue& params) override {
        (void)params;
        if (mgr == nullptr) {
            return;
        }
        if (apiName == "emitCallbackJson") {
            xprobe::JsonValue obj = xprobe::JsonValue::makeObject();
            obj["status"] = 1;
            obj["msg"] = "ok";
            mgr->sendCallbackJson("onStatus", obj);
        } else if (apiName == "emitBadCallbackJson") {
            // 入参非对象：按 §6 降级为空对象，回包仍是合法 JSON
            mgr->sendCallbackJson("onStatus", xprobe::JsonValue("not-an-object"));
        }
    }
};

static void testCrossPlatformContract() {
    // ---- §2 单帧 payload 上限：四端统一 32 MiB，不得单独调整 ----
    assert(xprobe::FrameCodec::kMaxFramePayload == 32u * 1024u * 1024u);

    // ---- §6 sendCallbackJson：对象入参序列化为 callback 帧的 value ----
    {
        const uint16_t kPort = 19766;
        xprobe::AutoTestMgr mgr;
        CallbackJsonInvocation inv;
        inv.mgr = &mgr;

        mgr.start(&inv, kPort);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        {
            TestClient client(kPort);

            client.sendFrame("{\"api\":{\"apiName\":\"emitCallbackJson\",\"params\":{}},\"ver\":2}");
            std::string resp;
            assert(client.recvFrame(&resp, 3000));
            std::string err;
            xprobe::JsonValue j = xprobe::JsonValue::parse(resp, &err);
            assert(err.empty());
            assert(j["type"].asString() == "callback");
            assert(j["key"].asString() == "onStatus");
            // value 必须可二次解析为 JSON 对象（客户端依赖这一点）
            std::string valueErr;
            xprobe::JsonValue value = xprobe::JsonValue::parse(j["value"].asString(), &valueErr);
            assert(valueErr.empty());
            assert(value.isObject());
            assert(value["status"].asInt() == 1);
            assert(value["msg"].asString() == "ok");

            // §6 边界：非对象入参降级为 {}
            client.sendFrame("{\"api\":{\"apiName\":\"emitBadCallbackJson\",\"params\":{}},\"ver\":2}");
            assert(client.recvFrame(&resp, 3000));
            std::string badErr;
            xprobe::JsonValue bad = xprobe::JsonValue::parse(resp, &badErr);
            assert(badErr.empty());
            assert(bad["type"].asString() == "callback");
            assert(bad["value"].asString() == "{}");
        }
        mgr.stop();
    }

    // ---- §4 数组帧非法元素：整帧拒绝（合法命令也不得执行） ----
    {
        const uint16_t kPort = 19767;
        xprobe::AutoTestMgr mgr;
        RecordingInvocation inv;

        mgr.start(&inv, kPort);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        {
            TestClient client(kPort);
            // 第二条是字符串而非对象：整帧拒绝，第一条合法命令也不执行
            client.sendFrame("[{\"api\":{\"apiName\":\"willNotRun\",\"params\":{}},\"ver\":2},\"oops\"]");
            std::string resp;
            assert(client.recvFrame(&resp, 3000));
            std::string err;
            xprobe::JsonValue j = xprobe::JsonValue::parse(resp, &err);
            assert(err.empty());
            assert(j["type"].asString() == "error");
            assert(j["key"].asString() == "parseError");

            // 等待一段时间，确认没有任何命令被执行（而非「跳过非法元素继续执行」）
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            assert(inv.count() == 0);
        }
        mgr.stop();
    }

    pass("跨端契约（单帧上限 32MiB / sendCallbackJson / 数组非法元素整帧拒绝）");
}

// =====================================================================
// main
// =====================================================================

int main() {
#ifdef _WIN32
    // 测试客户端需要 Winsock
    WSADATA wsaData;
    int wsaRc = WSAStartup(MAKEWORD(2, 2), &wsaData);
    assert(wsaRc == 0);
#endif

    std::printf("==== xprobe 核心库测试 ====\n");

    testJsonBasicTypes();
    testJsonStringEscapes();
    testJsonComposite();
    testJsonBuildAndModify();
    testJsonParseErrors();
    testFrameCodecEncode();
    testFrameCodecWholeAndSticky();
    testFrameCodecByteByByte();
    testMessageParserVer2();
    testMessageParserVer1();
    testMessageParserArray();
    testMessageParserField();
    testMessageParserErrors();
    testThreadPool();
    testIntegration();
    testLoggerCallback();
    testCrossPlatformContract();

    std::printf("==== 全部 %d 组用例通过 ====\n", gPassed);

#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}
