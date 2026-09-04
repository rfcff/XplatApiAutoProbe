// =====================================================================
// xprobe/protocol.cpp
// 协议层实现
// =====================================================================
#include "xprobe/protocol.h"

#include <cstdint>
#include <cstdlib>

#include "xprobe/logger.h"

namespace xprobe {

// =====================================================================
// FrameCodec
// =====================================================================

std::vector<std::string> FrameCodec::feed(const char* data, size_t len) {
    std::vector<std::string> frames;
    if (error_) {
        return frames; // 已发生协议错误，不再消费数据
    }
    if (data != nullptr && len > 0) {
        buf_.append(data, len);
    }

    // 循环凑帧：缓冲中至少要有完整长度头 + 整个 payload 才产出一帧
    while (true) {
        if (buf_.size() < 4) {
            break; // 长度头未收满（半包）
        }
        // 4 字节大端长度头（不含头本身）
        uint32_t payloadLen = (static_cast<uint32_t>(static_cast<unsigned char>(buf_[0])) << 24) |
                              (static_cast<uint32_t>(static_cast<unsigned char>(buf_[1])) << 16) |
                              (static_cast<uint32_t>(static_cast<unsigned char>(buf_[2])) << 8) |
                              (static_cast<uint32_t>(static_cast<unsigned char>(buf_[3])));
        if (payloadLen > kMaxFramePayload) {
            error_ = true; // 帧长超限：协议错误，由上层断开连接
            frames.clear();
            return frames;
        }
        if (buf_.size() < 4 + payloadLen) {
            break; // payload 未收满（半包）
        }
        // 凑齐一帧：拷贝 payload，然后从缓冲中移除（剩余数据属于下一帧，可能存在粘包）
        frames.emplace_back(buf_, 4, payloadLen);
        buf_.erase(0, 4 + payloadLen);
    }
    return frames;
}

std::vector<std::string> FrameCodec::feed(const std::string& data) {
    return feed(data.data(), data.size());
}

std::string FrameCodec::encode(const std::string& payload) {
    uint32_t len = static_cast<uint32_t>(payload.size());
    std::string frame;
    frame.reserve(4 + payload.size());
    // 大端序长度头
    frame.push_back(static_cast<char>((len >> 24) & 0xFF));
    frame.push_back(static_cast<char>((len >> 16) & 0xFF));
    frame.push_back(static_cast<char>((len >> 8) & 0xFF));
    frame.push_back(static_cast<char>(len & 0xFF));
    frame.append(payload);
    return frame;
}

void FrameCodec::reset() {
    buf_.clear();
    error_ = false;
}

bool FrameCodec::hasError() const { return error_; }

// =====================================================================
// MessageParser
// =====================================================================

std::vector<Command> MessageParser::parse(const std::string& payload) {
    // 包装实现：解析失败统一以 WARN/"parser" 打点后原样抛出（上层捕获后回报 error/parseError）
    try {
        return parseImpl(payload);
    } catch (const MessageParseException& e) {
        log(LogLevel::WARN, "parser", std::string("命令解析失败: ") + e.what());
        throw;
    }
}

std::vector<Command> MessageParser::parseImpl(const std::string& payload) {
    // 第一步：JSON 解析，失败抛异常（错误消息包含原文）
    std::string err;
    JsonValue root = JsonValue::parse(payload, &err);
    if (!err.empty()) {
        throw MessageParseException("json parse failed: " + err + ", 原文: " + payload);
    }

    std::vector<Command> commands;

    if (root.isArray()) {
        // 数组形式：按连续相同 threadMode 分组后派发（见 PROTOCOL.md §4）
        for (size_t i = 0; i < root.size(); ++i) {
            const JsonValue& item = root[i];
            if (!item.isObject()) {
                throw MessageParseException("数组中第 " + std::to_string(i) +
                                            " 个元素不是 JSON 对象, 原文: " + payload);
            }
            commands.push_back(parseOne(item, payload));
        }
        return commands;
    }

    if (root.isObject()) {
        // 单对象形式
        commands.push_back(parseOne(root, payload));
        return commands;
    }

    // 顶层既不是对象也不是数组
    throw MessageParseException("命令帧顶层必须是 JSON 对象或数组, 原文: " + payload);
}

Command MessageParser::parseOne(const JsonValue& obj, const std::string& payload) {
    Command cmd;

    // ---- 成员变量修改命令：存在 "field" 键 ----
    if (obj.isMember("field")) {
        const JsonValue& field = obj["field"];
        if (!field.isString()) {
            throw MessageParseException("field 命令的 field 字段必须是字符串, 原文: " + payload);
        }
        cmd.cmdType = CmdType::Field;
        cmd.apiName = field.asString();          // field 帧的“方法名”为类名
        cmd.commandName = field.asString();
        cmd.apiRaw = obj;                         // 整条命令对象透传（含参数信息）
        cmd.params = JsonValue::makeObject();     // 无 params 对象语义
        cmd.threadMode = static_cast<int>(obj["threadMode"].asInt(0));
        // field 帧同样支持扁平参数数组校验
        checkParamArrayLength(obj, obj, false, payload);
        return cmd;
    }

    // ---- 方法调用命令：必须存在 "api" ----
    if (!obj.isMember("api")) {
        throw MessageParseException("命令缺少 api 字段, 原文: " + payload);
    }
    const JsonValue& api = obj["api"];
    if (!api.isString() && !api.isObject()) {
        throw MessageParseException("api 字段必须是字符串或对象, 原文: " + payload);
    }

    cmd.cmdType = CmdType::Method;
    cmd.apiRaw = api;
    cmd.threadMode = static_cast<int>(obj["threadMode"].asInt(0));

    // ---- ver 字段：接受整数 2 或字符串 "2" ----
    const JsonValue* verVal = obj.find("ver");
    if (verVal != nullptr) {
        if (verVal->isString()) {
            // 字符串形式：按十进制整数解析（三端一致，见 PROTOCOL.md §7.3）
            cmd.ver = static_cast<int>(std::strtol(verVal->asString().c_str(), nullptr, 10));
        } else if (verVal->isNumber()) {
            cmd.ver = static_cast<int>(verVal->asInt());
        } else {
            throw MessageParseException("ver 字段必须是整数或字符串, 原文: " + payload);
        }
    }
    cmd.isVer2 = (cmd.ver == 2);

    if (api.isString()) {
        // 扁平形式：{"api": "com.foo.Bar.add", "param_name": [...], ...}
        cmd.apiName = api.asString();
        cmd.commandName = api.asString();
        cmd.params = JsonValue::makeObject();
        checkParamArrayLength(obj, obj, false, payload);
    } else {
        // 嵌套形式（iOS 风格 / ver2）：{"api": {"apiName": ..., "params": {...}}, ...}
        const JsonValue& name = api["apiName"];
        if (!name.isString() || name.asString().empty()) {
            throw MessageParseException("api.apiName 必须是非空字符串, 原文: " + payload);
        }
        cmd.apiName = name.asString();
        cmd.commandName = name.asString();

        if (cmd.isVer2) {
            // ver2：params 为任意 JSON 对象，原样透传；缺省为空对象
            const JsonValue& p = api["params"];
            cmd.params = p.isObject() ? p : JsonValue::makeObject();
        } else {
            // ver1 嵌套形式：校验 paramName/paramType/paramValue 长度一致
            cmd.params = JsonValue::makeObject();
            checkParamArrayLength(obj, api, true, payload);
        }
    }

    return cmd;
}

void MessageParser::checkParamArrayLength(const JsonValue& obj,
                                          const JsonValue& apiObj,
                                          bool nested,
                                          const std::string& payload) {
    // 依据嵌套与否选择键名（paramName/paramType/paramValue 或 param_name/param_type/param_value）
    const std::string& nameKey = nested ? "paramName" : "param_name";
    const std::string& typeKey = nested ? "paramType" : "param_type";
    const std::string& valueKey = nested ? "paramValue" : "param_value";

    const JsonValue& host = nested ? apiObj : obj;
    const JsonValue* names = host.find(nameKey);
    const JsonValue* types = host.find(typeKey);
    const JsonValue* values = host.find(valueKey);

    // 三个数组要么都出现，要么都缺省；出现则长度必须一致
    bool hasAny = (names != nullptr) || (types != nullptr) || (values != nullptr);
    if (!hasAny) {
        return;
    }

    auto lenOf = [](const JsonValue* v) -> long long {
        // 缺省视为长度 -1（与出现的数组必然不相等，触发报错）
        if (v == nullptr) {
            return -1;
        }
        if (v->isArray()) {
            return static_cast<long long>(v->size());
        }
        return -2; // 类型错误也触发报错
    };

    long long ln = lenOf(names);
    long long lt = lenOf(types);
    long long lv = lenOf(values);
    if (ln < 0 || lt < 0 || lv < 0 || ln != lt || lt != lv) {
        throw MessageParseException("参数数组长度不一致 (" + nameKey + "=" +
                                    std::to_string(ln < 0 ? 0 : ln) + ", " + typeKey + "=" +
                                    std::to_string(lt < 0 ? 0 : lt) + ", " + valueKey + "=" +
                                    std::to_string(lv < 0 ? 0 : lv) + "), 原文: " + payload);
    }
}

} // namespace xprobe
