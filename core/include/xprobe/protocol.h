// =====================================================================
// xprobe/protocol.h
// 协议层：帧编解码（FrameCodec）、命令数据结构（Command）、
//         命令帧解析器（MessageParser）
// 依据 PROTOCOL.md v1.0：
//   - 帧格式：4 字节大端长度头 + payload
//   - 命令帧：单个 JSON 对象或对象数组
//   - ver==2 自定义调用；ver!=2 透传 api 子对象序列化（C++ Core 特有分支，见 PROTOCOL.md §7.2）
// =====================================================================
#ifndef XPROBE_PROTOCOL_H
#define XPROBE_PROTOCOL_H

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include "xprobe/json.h"

namespace xprobe {

// =====================================================================
// FrameCodec：帧编解码状态机
// 发送侧：encode() 为 payload 加 4 字节大端长度头；
// 接收侧：feed() 可接收任意分块字节流（应对 TCP 粘包/半包），
//         每凑齐一个完整帧即产出一个 payload。
// =====================================================================
class FrameCodec {
public:
    // 最大帧长度（含 payload，不含长度头），超出视为协议错误。
    // 32MB 为 PROTOCOL.md §2 规定的跨端统一值，各平台必须一致，不得单独调整。
    static constexpr uint32_t kMaxFramePayload = 32u * 1024u * 1024u;

    // 喂入一段原始字节（可任意分块），返回本次凑齐的全部完整帧 payload
    std::vector<std::string> feed(const char* data, size_t len);
    std::vector<std::string> feed(const std::string& data);

    // 编码一帧：返回 4 字节大端长度头 + payload
    static std::string encode(const std::string& payload);

    // 清空解码状态
    void reset();

    // 解码过程中是否发生协议错误（帧长超限），需要断开连接
    bool hasError() const;

private:
    std::string buf_;    // 接收缓冲（长度头未凑齐或 payload 未收满时暂存）
    bool error_ = false; // 协议错误标记
};

// =====================================================================
// Command：单条命令的结构化描述
// =====================================================================
enum class CmdType {
    Method, // 方法调用（api / api.apiName）
    Field,  // 静态成员变量 / 属性修改（field）
};

struct Command {
    CmdType cmdType = CmdType::Method; // 命令类型
    std::string apiName;               // 方法名：
                                       //   ver2  -> api.apiName（如 "createEngine"）
                                       //   扁平  -> api 字符串原样（如 "com.foo.Bar.add"）
                                       //   嵌套  -> api.apiName（iOS 风格）
                                       //   field -> field 字符串（类名）
    JsonValue params;                  // ver2 时为 api.params 对象；其余为空对象
    int threadMode = 0;                // 0/缺省=后台线程池，1=主线程
    int ver = 0;                       // 原始 ver 数值（整数或字符串解析结果，缺省 0）
    bool isVer2 = false;               // ver 是否有效为 2（整数 2 或字符串 "2"）
    JsonValue apiRaw;                  // 原始 api 子对象（METHOD），field 帧时为整条命令对象；
                                       // 执行时若 ver!=2 将其序列化后透传（C++ Core 特有分支）
    std::string commandName;           // 执行异常回报 sendError 使用的 key
};

// =====================================================================
// MessageParser：命令帧解析
// 解析单个 JSON 对象或对象数组，产出 Command 列表（保持原始顺序）；
// 解析失败抛出 MessageParseException，错误消息包含原文。
// =====================================================================
class MessageParseException : public std::runtime_error {
public:
    explicit MessageParseException(const std::string& msg)
        : std::runtime_error(msg) {}
};

class MessageParser {
public:
    // 解析命令帧 payload（不含长度头）；
    // 顶层必须是对象或对象数组，且每个元素必须可解析为合法命令。
    static std::vector<Command> parse(const std::string& payload);

private:
    // 实际解析实现（parse 在此基础上对解析失败统一打点日志）
    static std::vector<Command> parseImpl(const std::string& payload);

    // 解析单个命令对象
    static Command parseOne(const JsonValue& obj, const std::string& payload);

    // 校验参数名/类型/值三个数组长度一致（扁平：param_name/param_type/param_value；
    // 嵌套：paramName/paramType/paramValue），不一致抛异常
    static void checkParamArrayLength(const JsonValue& obj,
                                      const JsonValue& apiObj,
                                      bool nested,
                                      const std::string& payload);
};

} // namespace xprobe

#endif // XPROBE_PROTOCOL_H
