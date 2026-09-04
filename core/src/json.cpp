// =====================================================================
// xprobe/json.cpp
// 轻量 JSON 库实现：递归下降解析器 + 紧凑序列化
// =====================================================================
#include "xprobe/json.h"

#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace xprobe {

// 递归解析的最大嵌套深度（防止恶意输入导致栈溢出）
static const int kMaxParseDepth = 200;

// =====================================================================
// 构造
// =====================================================================

JsonValue::JsonValue() = default;
JsonValue::JsonValue(std::nullptr_t) : type_(Type::Null) {}

JsonValue::JsonValue(bool b) : type_(Type::Bool) { ensureStorage().boolVal = b; }

JsonValue::JsonValue(int i) : type_(Type::Int) { ensureStorage().intVal = static_cast<int64_t>(i); }
JsonValue::JsonValue(unsigned int i) : type_(Type::Int) { ensureStorage().intVal = static_cast<int64_t>(i); }
JsonValue::JsonValue(long i) : type_(Type::Int) { ensureStorage().intVal = static_cast<int64_t>(i); }
JsonValue::JsonValue(unsigned long i) : type_(Type::Int) { ensureStorage().intVal = static_cast<int64_t>(i); }
JsonValue::JsonValue(long long i) : type_(Type::Int) { ensureStorage().intVal = static_cast<int64_t>(i); }

JsonValue::JsonValue(unsigned long long i) {
    // 无符号 64 位超出 int64 表示范围时降级为 double，避免溢出截断
    if (i <= static_cast<unsigned long long>(9223372036854775807ULL)) {
        type_ = Type::Int;
        ensureStorage().intVal = static_cast<int64_t>(i);
    } else {
        type_ = Type::Double;
        ensureStorage().doubleVal = static_cast<double>(i);
    }
}

JsonValue::JsonValue(double d) : type_(Type::Double) { ensureStorage().doubleVal = d; }

JsonValue::JsonValue(const char* s) : type_(Type::String) { ensureStorage().stringVal = s ? s : ""; }
JsonValue::JsonValue(const std::string& s) : type_(Type::String) { ensureStorage().stringVal = s; }
JsonValue::JsonValue(std::string&& s) : type_(Type::String) { ensureStorage().stringVal = std::move(s); }

JsonValue JsonValue::makeArray() {
    JsonValue v;
    v.makeType(Type::Array);
    return v;
}

JsonValue JsonValue::makeObject() {
    JsonValue v;
    v.makeType(Type::Object);
    return v;
}

// 深拷贝构造：连同嵌套数组 / 对象整体复制
JsonValue::JsonValue(const JsonValue& other) : type_(other.type_) {
    if (other.storage_) {
        storage_ = std::make_unique<Storage>(*other.storage_);
    }
}

// 深拷贝赋值（自赋值安全）
JsonValue& JsonValue::operator=(const JsonValue& other) {
    if (this != &other) {
        type_ = other.type_;
        if (other.storage_) {
            storage_ = std::make_unique<Storage>(*other.storage_);
        } else {
            storage_.reset();
        }
    }
    return *this;
}

// 移动构造：接管存储体，并把源对象置为 Null。
// 必须显式重置 type_——若源对象保留原类型而 storage_ 已被移空，
// 后续对源对象调用 asInt / asBool / serialize 会解引用空 storage_ 而崩溃。
JsonValue::JsonValue(JsonValue&& other) noexcept
    : type_(other.type_), storage_(std::move(other.storage_)) {
    other.type_ = Type::Null;
}

// 移动赋值（自赋值安全）：同移动构造，源对象置为 Null
JsonValue& JsonValue::operator=(JsonValue&& other) noexcept {
    if (this != &other) {
        type_ = other.type_;
        storage_ = std::move(other.storage_);
        other.type_ = Type::Null;
    }
    return *this;
}

// =====================================================================
// 内部工具
// =====================================================================

JsonValue::Storage& JsonValue::ensureStorage() {
    if (!storage_) {
        storage_ = std::make_unique<Storage>();
    }
    return *storage_;
}

// 切换类型：如果目标类型需要存储体则惰性创建，并清空旧内容
void JsonValue::makeType(Type t) {
    type_ = t;
    switch (t) {
        case Type::Array:
            ensureStorage().arrayVal.clear();
            ensureStorage().objectVal.clear();
            break;
        case Type::Object:
            ensureStorage().arrayVal.clear();
            ensureStorage().objectVal.clear();
            break;
        default:
            break;
    }
}

// =====================================================================
// 类型判断
// =====================================================================

JsonValue::Type JsonValue::type() const { return type_; }
bool JsonValue::isNull() const { return type_ == Type::Null; }
bool JsonValue::isBool() const { return type_ == Type::Bool; }
bool JsonValue::isInt() const { return type_ == Type::Int; }
bool JsonValue::isDouble() const { return type_ == Type::Double; }
bool JsonValue::isNumber() const { return type_ == Type::Int || type_ == Type::Double; }
bool JsonValue::isString() const { return type_ == Type::String; }
bool JsonValue::isArray() const { return type_ == Type::Array; }
bool JsonValue::isObject() const { return type_ == Type::Object; }

// =====================================================================
// 数组访问
// =====================================================================

size_t JsonValue::size() const {
    switch (type_) {
        case Type::Array:
            return storage_ ? storage_->arrayVal.size() : 0;
        case Type::Object:
            return storage_ ? storage_->objectVal.size() : 0;
        default:
            return 0;
    }
}

JsonValue& JsonValue::append(JsonValue v) {
    // 非数组类型调用 append 时，自身重置为数组（便于链式构建）
    if (type_ != Type::Array) {
        makeType(Type::Array);
    }
    ensureStorage().arrayVal.push_back(std::move(v));
    return *this;
}

// 静态 null 占位：越界 / 缺键访问时返回，避免悬空引用
static JsonValue& nullPlaceholder() {
    static JsonValue instance;
    return instance;
}

JsonValue& JsonValue::operator[](size_t idx) {
    if (type_ == Type::Array && storage_ && idx < storage_->arrayVal.size()) {
        return storage_->arrayVal[idx];
    }
    return nullPlaceholder();
}

const JsonValue& JsonValue::operator[](size_t idx) const {
    if (type_ == Type::Array && storage_ && idx < storage_->arrayVal.size()) {
        return storage_->arrayVal[idx];
    }
    return nullPlaceholder();
}

// =====================================================================
// 对象访问
// =====================================================================

JsonValue& JsonValue::operator[](const std::string& key) {
    if (type_ != Type::Object) {
        makeType(Type::Object);
    }
    return ensureStorage().objectVal[key];
}

const JsonValue& JsonValue::operator[](const std::string& key) const {
    if (type_ == Type::Object && storage_) {
        auto it = storage_->objectVal.find(key);
        if (it != storage_->objectVal.end()) {
            return it->second;
        }
    }
    return nullPlaceholder();
}

const JsonValue* JsonValue::find(const std::string& key) const {
    if (type_ == Type::Object && storage_) {
        auto it = storage_->objectVal.find(key);
        if (it != storage_->objectVal.end()) {
            return &it->second;
        }
    }
    return nullptr;
}

bool JsonValue::isMember(const std::string& key) const { return find(key) != nullptr; }

bool JsonValue::erase(const std::string& key) {
    if (type_ == Type::Object && storage_) {
        return storage_->objectVal.erase(key) > 0;
    }
    return false;
}

const std::map<std::string, JsonValue>& JsonValue::objectItems() const {
    static const std::map<std::string, JsonValue> kEmpty;
    if (type_ == Type::Object && storage_) {
        return storage_->objectVal;
    }
    return kEmpty;
}

// =====================================================================
// 取值
// =====================================================================

bool JsonValue::asBool(bool def) const {
    if (type_ == Type::Bool) {
        return storage_->boolVal;
    }
    if (type_ == Type::Int) {
        return storage_->intVal != 0;
    }
    if (type_ == Type::Double) {
        return storage_->doubleVal != 0.0;
    }
    return def;
}

int64_t JsonValue::asInt(int64_t def) const {
    if (type_ == Type::Int) {
        return storage_->intVal;
    }
    if (type_ == Type::Double) {
        return static_cast<int64_t>(storage_->doubleVal);
    }
    if (type_ == Type::Bool) {
        return storage_->boolVal ? 1 : 0;
    }
    return def;
}

double JsonValue::asDouble(double def) const {
    if (type_ == Type::Double) {
        return storage_->doubleVal;
    }
    if (type_ == Type::Int) {
        return static_cast<double>(storage_->intVal);
    }
    return def;
}

const std::string& JsonValue::asString() const {
    static const std::string kEmpty;
    if (type_ == Type::String && storage_) {
        return storage_->stringVal;
    }
    return kEmpty;
}

std::string JsonValue::asString(const std::string& def) const {
    if (type_ == Type::String && storage_) {
        return storage_->stringVal;
    }
    return def;
}

// =====================================================================
// 相等比较
// =====================================================================

bool JsonValue::operator==(const JsonValue& other) const {
    if (type_ != other.type_) {
        return false;
    }
    switch (type_) {
        case Type::Null:
            return true;
        case Type::Bool:
            return storage_->boolVal == other.storage_->boolVal;
        case Type::Int:
            return storage_->intVal == other.storage_->intVal;
        case Type::Double:
            return storage_->doubleVal == other.storage_->doubleVal;
        case Type::String:
            return storage_->stringVal == other.storage_->stringVal;
        case Type::Array: {
            const auto& a = storage_->arrayVal;
            const auto& b = other.storage_->arrayVal;
            if (a.size() != b.size()) {
                return false;
            }
            for (size_t i = 0; i < a.size(); ++i) {
                if (a[i] != b[i]) {
                    return false;
                }
            }
            return true;
        }
        case Type::Object: {
            const auto& a = storage_->objectVal;
            const auto& b = other.storage_->objectVal;
            if (a.size() != b.size()) {
                return false;
            }
            auto it = a.begin();
            auto jt = b.begin();
            for (; it != a.end() && jt != b.end(); ++it, ++jt) {
                if (it->first != jt->first || it->second != jt->second) {
                    return false;
                }
            }
            return true;
        }
    }
    return false;
}

bool JsonValue::operator!=(const JsonValue& other) const { return !(*this == other); }

// =====================================================================
// 解析器（递归下降）
// =====================================================================

namespace {

// 解析上下文：持有输入文本与扫描位置
struct ParseContext {
    const char* data = nullptr;
    size_t len = 0;
    size_t pos = 0;
    int depth = 0;      // 当前嵌套深度
    std::string error;  // 错误描述（非空表示失败）

    // 记录错误（附带上报位置）
    void fail(const std::string& msg) {
        if (error.empty()) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), " (at offset %zu)", pos);
            error = msg + buf;
        }
    }
    bool failed() const { return !error.empty(); }

    bool eof() const { return pos >= len; }
    char peek() const { return data[pos]; }
    char advance() { return data[pos++]; }

    // 跳过空白字符（空格、制表、换行、回车）
    void skipWhitespace() {
        while (pos < len) {
            char c = data[pos];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                ++pos;
            } else {
                break;
            }
        }
    }
};

// 把 Unicode 码点编码为 UTF-8 追加到 out
void appendUtf8(std::string& out, uint32_t cp) {
    if (cp <= 0x7F) {
        out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

// 解析 4 位十六进制 \uXXXX
bool parseHex4(ParseContext& ctx, uint32_t& out) {
    if (ctx.pos + 4 > ctx.len) {
        ctx.fail("\\u 转义缺少 4 位十六进制数字");
        return false;
    }
    uint32_t v = 0;
    for (int i = 0; i < 4; ++i) {
        char c = ctx.data[ctx.pos++];
        v <<= 4;
        if (c >= '0' && c <= '9') {
            v |= static_cast<uint32_t>(c - '0');
        } else if (c >= 'a' && c <= 'f') {
            v |= static_cast<uint32_t>(c - 'a' + 10);
        } else if (c >= 'A' && c <= 'F') {
            v |= static_cast<uint32_t>(c - 'A' + 10);
        } else {
            ctx.fail("\\u 转义包含非法十六进制字符");
            return false;
        }
    }
    out = v;
    return true;
}

// 前置声明
bool parseValue(ParseContext& ctx, JsonValue& out);

// 解析字符串字面量（进入时 ctx 位于起始双引号处）
bool parseString(ParseContext& ctx, std::string& out) {
    ctx.advance(); // 消耗起始 '"'
    while (!ctx.eof()) {
        char c = ctx.advance();
        if (c == '"') {
            return true; // 正常结束
        }
        if (static_cast<unsigned char>(c) < 0x20) {
            ctx.fail("字符串中包含未转义的控制字符");
            return false;
        }
        if (c != '\\') {
            out.push_back(c); // 普通字符（UTF-8 字节原样透传）
            continue;
        }
        // 处理转义序列
        if (ctx.eof()) {
            ctx.fail("转义序列不完整");
            return false;
        }
        char e = ctx.advance();
        switch (e) {
            case '"':  out.push_back('"');  break;
            case '\\': out.push_back('\\'); break;
            case '/':  out.push_back('/');  break;
            case 'b':  out.push_back('\b'); break;
            case 'f':  out.push_back('\f'); break;
            case 'n':  out.push_back('\n'); break;
            case 'r':  out.push_back('\r'); break;
            case 't':  out.push_back('\t'); break;
            case 'u': {
                uint32_t cp = 0;
                if (!parseHex4(ctx, cp)) {
                    return false;
                }
                // 高位代理：期待紧随的低位代理组成完整码点
                if (cp >= 0xD800 && cp <= 0xDBFF) {
                    if (ctx.pos + 1 < ctx.len && ctx.data[ctx.pos] == '\\' && ctx.data[ctx.pos + 1] == 'u') {
                        ctx.pos += 2; // 消耗 "\u"
                        uint32_t low = 0;
                        if (!parseHex4(ctx, low)) {
                            return false;
                        }
                        if (low >= 0xDC00 && low <= 0xDFFF) {
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                        } else {
                            ctx.fail("非法的低位代理项");
                            return false;
                        }
                    } else {
                        ctx.fail("缺少低位代理项");
                        return false;
                    }
                } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                    ctx.fail("孤立的低位代理项");
                    return false;
                }
                appendUtf8(out, cp);
                break;
            }
            default:
                ctx.fail(std::string("非法转义字符 '\\") + e + "'");
                return false;
        }
    }
    ctx.fail("字符串缺少结束引号");
    return false;
}

// 解析数字字面量：先严格按整数解析，失败再按浮点解析
bool parseNumber(ParseContext& ctx, JsonValue& out) {
    const char* start = ctx.data + ctx.pos;
    char* end = nullptr;

    // 整数优先（strtoll 严格校验：可选符号 + 十进制数字）
    errno = 0;
    long long iv = std::strtoll(start, &end, 10);
    if (end != start && errno != ERANGE && ctx.pos + static_cast<size_t>(end - start) <= ctx.len) {
        // 整数字面量后紧跟的必须是定界符，排除 "123abc" 之类的粘连
        char next = *end;
        if (next == '\0' || next == ',' || next == '}' || next == ']' ||
            next == ' ' || next == '\t' || next == '\n' || next == '\r') {
            ctx.pos += static_cast<size_t>(end - start);
            out = JsonValue(static_cast<int64_t>(iv));
            return true;
        }
    }

    // 浮点解析（strtod 支持 "1.5"、"-2e10"、".5" 之外的合法 JSON 语法）
    errno = 0;
    double dv = std::strtod(start, &end);
    if (end == start) {
        ctx.fail("非法数字字面量");
        return false;
    }
    // 校验浮点字面量仅包含 JSON 合法字符（防 strtod 接受 "inf"/"nan"/十六进制）
    for (const char* p = start; p < end; ++p) {
        char c = *p;
        bool ok = (c >= '0' && c <= '9') || c == '+' || c == '-' || c == '.' || c == 'e' || c == 'E';
        if (!ok) {
            ctx.fail("非法数字字面量");
            return false;
        }
    }
    ctx.pos += static_cast<size_t>(end - start);
    out = JsonValue(dv);
    return true;
}

// 解析对象：{"k":v, ...}
bool parseObject(ParseContext& ctx, JsonValue& out) {
    out = JsonValue::makeObject();
    ctx.advance(); // 消耗 '{'
    ctx.skipWhitespace();
    if (!ctx.eof() && ctx.peek() == '}') {
        ctx.advance();
        return true; // 空对象 {}
    }
    while (true) {
        ctx.skipWhitespace();
        if (ctx.eof() || ctx.peek() != '"') {
            ctx.fail("对象键必须是字符串");
            return false;
        }
        std::string key;
        if (!parseString(ctx, key)) {
            return false;
        }
        ctx.skipWhitespace();
        if (ctx.eof() || ctx.advance() != ':') {
            ctx.fail("对象键后缺少 ':'");
            return false;
        }
        ctx.skipWhitespace();
        JsonValue val;
        if (!parseValue(ctx, val)) {
            return false;
        }
        out[key] = std::move(val); // 重复键后者覆盖前者
        ctx.skipWhitespace();
        if (ctx.eof()) {
            ctx.fail("对象缺少结束符 '}'");
            return false;
        }
        char c = ctx.advance();
        if (c == ',') {
            continue;
        }
        if (c == '}') {
            return true;
        }
        ctx.fail("对象元素后应为 ',' 或 '}'");
        return false;
    }
}

// 解析数组：[v, v, ...]
bool parseArray(ParseContext& ctx, JsonValue& out) {
    out = JsonValue::makeArray();
    ctx.advance(); // 消耗 '['
    ctx.skipWhitespace();
    if (!ctx.eof() && ctx.peek() == ']') {
        ctx.advance();
        return true; // 空数组 []
    }
    while (true) {
        ctx.skipWhitespace();
        JsonValue val;
        if (!parseValue(ctx, val)) {
            return false;
        }
        out.append(std::move(val));
        ctx.skipWhitespace();
        if (ctx.eof()) {
            ctx.fail("数组缺少结束符 ']'");
            return false;
        }
        char c = ctx.advance();
        if (c == ',') {
            continue;
        }
        if (c == ']') {
            return true;
        }
        ctx.fail("数组元素后应为 ',' 或 ']'");
        return false;
    }
}

// 解析任意 JSON 值
bool parseValue(ParseContext& ctx, JsonValue& out) {
    // 嵌套深度保护
    if (++ctx.depth > kMaxParseDepth) {
        ctx.fail("嵌套深度超过上限");
        return false;
    }
    struct DepthGuard {
        ParseContext& ctx;
        ~DepthGuard() { --ctx.depth; }
    } guard{ctx};

    ctx.skipWhitespace();
    if (ctx.eof()) {
        ctx.fail("输入意外结束");
        return false;
    }
    char c = ctx.peek();
    bool ok = false;
    if (c == '{') {
        ok = parseObject(ctx, out);
    } else if (c == '[') {
        ok = parseArray(ctx, out);
    } else if (c == '"') {
        std::string s;
        ok = parseString(ctx, s);
        if (ok) {
            out = JsonValue(std::move(s));
        }
    } else if (c == 't') {
        // true
        if (ctx.len - ctx.pos >= 4 && std::strncmp(ctx.data + ctx.pos, "true", 4) == 0) {
            ctx.pos += 4;
            out = JsonValue(true);
            ok = true;
        } else {
            ctx.fail("非法字面量（期望 true）");
        }
    } else if (c == 'f') {
        // false
        if (ctx.len - ctx.pos >= 5 && std::strncmp(ctx.data + ctx.pos, "false", 5) == 0) {
            ctx.pos += 5;
            out = JsonValue(false);
            ok = true;
        } else {
            ctx.fail("非法字面量（期望 false）");
        }
    } else if (c == 'n') {
        // null
        if (ctx.len - ctx.pos >= 4 && std::strncmp(ctx.data + ctx.pos, "null", 4) == 0) {
            ctx.pos += 4;
            out = JsonValue(nullptr);
            ok = true;
        } else {
            ctx.fail("非法字面量（期望 null）");
        }
    } else if (c == '-' || (c >= '0' && c <= '9')) {
        ok = parseNumber(ctx, out);
    } else {
        ctx.fail(std::string("非法字符 '") + c + "'");
    }
    return ok;
}

} // namespace

// =====================================================================
// 静态解析入口
// =====================================================================

JsonValue JsonValue::parse(const std::string& text, std::string* err) {
    ParseContext ctx;
    ctx.data = text.data();
    ctx.len = text.size();

    JsonValue result;
    if (!parseValue(ctx, result)) {
        if (err) {
            *err = ctx.error.empty() ? "json 解析失败" : ctx.error;
        }
        return JsonValue(); // 失败返回 Null
    }

    // 尾部必须只有空白
    ctx.skipWhitespace();
    if (!ctx.eof()) {
        if (err) {
            *err = "JSON 文本末尾存在多余内容 (at offset " + std::to_string(ctx.pos) + ")";
        }
        return JsonValue();
    }

    if (err) {
        err->clear();
    }
    return result;
}

// =====================================================================
// 序列化（紧凑格式）
// =====================================================================

// 转义并输出字符串字面量（含首尾引号）
static void serializeString(const std::string& s, std::string& out) {
    out.push_back('"');
    for (char ch : s) {
        unsigned char c = static_cast<unsigned char>(ch);
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (c < 0x20) {
                    // 其余控制字符使用 \u00XX 形式
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    // 含 UTF-8 多字节序列在内的字节原样输出
                    out.push_back(ch);
                }
        }
    }
    out.push_back('"');
}

void JsonValue::serialize(std::string& out) const {
    switch (type_) {
        case Type::Null:
            out += "null";
            break;
        case Type::Bool:
            out += (storage_ && storage_->boolVal) ? "true" : "false";
            break;
        case Type::Int: {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(storage_->intVal));
            out += buf;
            break;
        }
        case Type::Double: {
            double d = storage_->doubleVal;
            // JSON 不支持 NaN / 无穷大，序列化为 null
            if (std::isnan(d) || std::isinf(d)) {
                out += "null";
                break;
            }
            // 依次提高精度，取可精确往返的最短表示
            char buf[64];
            for (int prec = 15; prec <= 17; ++prec) {
                std::snprintf(buf, sizeof(buf), "%.*g", prec, d);
                if (std::strtod(buf, nullptr) == d) {
                    break;
                }
            }
            out += buf;
            break;
        }
        case Type::String:
            serializeString(storage_->stringVal, out);
            break;
        case Type::Array: {
            out.push_back('[');
            if (storage_) {
                const auto& arr = storage_->arrayVal;
                for (size_t i = 0; i < arr.size(); ++i) {
                    if (i > 0) {
                        out.push_back(',');
                    }
                    arr[i].serialize(out);
                }
            }
            out.push_back(']');
            break;
        }
        case Type::Object: {
            out.push_back('{');
            if (storage_) {
                const auto& obj = storage_->objectVal;
                bool first = true;
                for (const auto& kv : obj) {
                    if (!first) {
                        out.push_back(',');
                    }
                    first = false;
                    serializeString(kv.first, out);
                    out.push_back(':');
                    kv.second.serialize(out);
                }
            }
            out.push_back('}');
            break;
        }
    }
}

std::string JsonValue::serialize() const {
    std::string out;
    out.reserve(64);
    serialize(out);
    return out;
}

} // namespace xprobe
