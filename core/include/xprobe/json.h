// =====================================================================
// xprobe/json.h
// 轻量 JSON 库（无外部依赖，C++17）
// 支持 null / bool / number(int64/double) / string / array / object，
// 解析支持全部标准转义（含 \uXXXX 与代理对），序列化为紧凑格式。
// =====================================================================
#ifndef XPROBE_JSON_H
#define XPROBE_JSON_H

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace xprobe {

// JSON 值类型：null / 布尔 / 整数 / 浮点 / 字符串 / 数组 / 对象
class JsonValue {
public:
    // 值类型枚举
    enum class Type { Null, Bool, Int, Double, String, Array, Object };

    // ---- 构造 ----
    JsonValue();                                // 默认构造为 null
    JsonValue(std::nullptr_t);                  // null
    JsonValue(bool b);                          // 布尔
    JsonValue(int i);                           // 32 位整数（内部提升为 int64）
    JsonValue(unsigned int i);                  // 无符号 32 位整数
    JsonValue(long i);                          // long 整数
    JsonValue(unsigned long i);                 // 无符号 long 整数
    JsonValue(long long i);                     // 64 位整数
    JsonValue(unsigned long long i);            // 无符号 64 位整数（超出 int64 范围时转为 double）
    JsonValue(double d);                        // 浮点
    JsonValue(const char* s);                   // C 字符串
    JsonValue(const std::string& s);            // std::string
    JsonValue(std::string&& s);                 // std::string 右值

    // 工厂方法：创建空数组 / 空对象
    static JsonValue makeArray();
    static JsonValue makeObject();

    // 拷贝语义：深拷贝（含嵌套数组 / 对象）；移动语义零拷贝
    JsonValue(const JsonValue& other);
    JsonValue& operator=(const JsonValue& other);
    // 移动后源对象被置为 Null：维持「type_ != Null 时 storage_ 必非空」的类不变量，
    // 否则源对象的 asInt / asBool / serialize 等按 type_ 分支取值时会解引用空 storage_。
    JsonValue(JsonValue&& other) noexcept;
    JsonValue& operator=(JsonValue&& other) noexcept;

    // ---- 类型判断 ----
    Type type() const;
    bool isNull() const;
    bool isBool() const;
    bool isInt() const;                         // 仅整数类型
    bool isDouble() const;                      // 仅浮点类型
    bool isNumber() const;                      // 整数或浮点
    bool isString() const;
    bool isArray() const;
    bool isObject() const;

    // ---- 数组访问 ----
    size_t size() const;                        // 数组元素数 / 对象键数，其余类型为 0
    JsonValue& append(JsonValue v);             // 追加数组元素（非数组调用时自身变为数组）
    JsonValue& operator[](size_t idx);          // 越界访问返回静态 null 占位（可写回写无效）
    const JsonValue& operator[](size_t idx) const;

    // ---- 对象访问 ----
    JsonValue& operator[](const std::string& key);       // 不存在则创建 null 成员（接受字符串字面量）
    const JsonValue& operator[](const std::string& key) const;  // 不存在返回静态 null 占位
    const JsonValue* find(const std::string& key) const; // 查找，不存在返回 nullptr
    bool isMember(const std::string& key) const;         // 是否包含键
    bool erase(const std::string& key);                  // 删除键，返回是否删除
    // 获取对象键值对（键按字典序排列，序列化输出稳定有序）
    const std::map<std::string, JsonValue>& objectItems() const;

    // ---- 取值（带类型转换或默认值） ----
    bool asBool(bool def = false) const;        // bool 直接返回；其余按“是否为 null/0/false”语义
    int64_t asInt(int64_t def = 0) const;       // 整数返回原值；浮点截断；其余返回默认
    double asDouble(double def = 0.0) const;    // 浮点/整数返回数值；其余返回默认
    const std::string& asString() const;        // 字符串返回引用；其余返回静态空串
    std::string asString(const std::string& def) const; // 字符串返回拷贝；其余返回默认

    // ---- 解析 / 序列化 ----
    // 解析 JSON 文本；成功时返回解析结果并把 err（若非空）清空，
    // 失败时返回 Null 值并把错误描述写入 err（若非空）。
    // 注意：文本 "null" 解析成功也返回 Null，调用方应以 err 判定成败。
    static JsonValue parse(const std::string& text, std::string* err = nullptr);

    // 序列化为紧凑 JSON 字符串（无空白；对象键按字典序；非 ASCII 原样输出 UTF-8）
    std::string serialize() const;
    void serialize(std::string& out) const;     // 追加写入 out，避免中间拷贝

    // 相等比较（类型与值均相同；Int 与 Double 数值相等视为不相等，类型必须一致）
    bool operator==(const JsonValue& other) const;
    bool operator!=(const JsonValue& other) const;

private:
    // 具体存储体（按类型只使用其中一个成员）
    struct Storage {
        bool boolVal = false;
        int64_t intVal = 0;
        double doubleVal = 0.0;
        std::string stringVal;
        std::vector<JsonValue> arrayVal;
        std::map<std::string, JsonValue> objectVal;
    };
    Type type_ = Type::Null;
    std::unique_ptr<Storage> storage_;          // null/标量可不分配，惰性创建

    Storage& ensureStorage();                   // 确保存储体存在
    void makeType(Type t);                      // 切换类型（丢弃旧值）
};

} // namespace xprobe

#endif // XPROBE_JSON_H
