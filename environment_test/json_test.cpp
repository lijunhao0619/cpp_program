// ============================================================
// nlohmann/json 常用操作教程
// 官方文档: https://json.nlohmann.me/
// ============================================================

#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <map>
#ifdef _WIN32
#include <windows.h>
#endif

using json = nlohmann::json;

int main() {
    // ╔══════════════════════════════════════════════════════╗
    // ║  1. 创建 JSON 对象（多种方式）                       ║
    // ╚══════════════════════════════════════════════════════╝

    // 1a. 字面量初始化 —— 最直观，类似写 JSON 原文
    json j1 = {
        {"name", "rpc_server"},
        {"port", 8080},
        {"debug", false},
        {"timeout", 3.14}
    };

    // 1b. 使用 [] 逐个赋值 —— 适合运行时动态构造
    json j2;
    j2["service"] = "Calculator";
    j2["version"] = "1.0.0";
    j2["max_connections"] = 1000;

    // 1c. 嵌套对象 & 数组
    json j3 = {
        {"service", "MathService"},
        {"methods", {"Add", "Sub", "Mul", "Div"}},                // 数组
        {"config", {                                                // 嵌套对象
            {"timeout_ms", 5000},
            {"retry", 3}
        }}
    };

    std::cout << "=== 1. 创建 ===" << std::endl;
    std::cout << "j1: " << j1.dump() << std::endl;
    std::cout << "j2: " << j2.dump() << std::endl;
    std::cout << "j3: " << j3.dump() << std::endl;

    // ╔══════════════════════════════════════════════════════╗
    // ║  2. 读取值（访问 JSON 字段）                         ║
    // ╚══════════════════════════════════════════════════════╝

    // 2a. 直接通过 key 访问，用 .get<T>() 指定类型
    std::string name = j1["name"].get<std::string>();
    int port = j1["port"].get<int>();
    double timeout = j1["timeout"].get<double>();

    std::cout << "\n=== 2. 读取 ===" << std::endl;
    std::cout << "name=" << name << ", port=" << port
              << ", timeout=" << timeout << std::endl;

    // 2b. 隐式转换 —— key 存在且类型匹配时可用
    bool debug = j1["debug"];           // 自动转 bool
    int conn = j2["max_connections"];   // 自动转 int
    std::cout << "debug=" << debug << ", max_connections=" << conn << std::endl;

    // 2c. .value(key, default) —— 安全读取，key 不存在返回默认值
    std::string desc = j1.value("description", "no description");
    int threads = j1.value("threads", 4);   // "threads" 不存在，返回默认值 4
    std::cout << "description=" << desc << ", threads=" << threads << std::endl;

    // ╔══════════════════════════════════════════════════════╗
    // ║  3. 检查 key 是否存在                               ║
    // ╚══════════════════════════════════════════════════════╝

    std::cout << "\n=== 3. 检查 key ===" << std::endl;
    std::cout << std::boolalpha;   // 让 bool 打印 true/false 而不是 1/0

    // 3a. .contains(key) —— 最常用
    std::cout << "has 'name'? " << j1.contains("name") << std::endl;        // true
    std::cout << "has 'address'? " << j1.contains("address") << std::endl;  // false

    // 3b. .is_xxx() —— 检查值的类型
    std::cout << "port is number? " << j1["port"].is_number() << std::endl;
    std::cout << "debug is boolean? " << j1["debug"].is_boolean() << std::endl;
    std::cout << "methods is array? " << j3["methods"].is_array() << std::endl;
    std::cout << "config is object? " << j3["config"].is_object() << std::endl;
    std::cout << "name is string? " << j1["name"].is_string() << std::endl;

    // ╔══════════════════════════════════════════════════════╗
    // ║  4. 修改值                                          ║
    // ╚══════════════════════════════════════════════════════╝

    json j4 = {
        {"counter", 0},
        {"tags", {"fast", "stable"}}
    };

    // 4a. 更新已有字段 每次操作在最前
    j4["counter"] = 42;

    // 4b. 添加新字段（直接用 [] 赋值就自动创建）
    j4["new_field"] = "hello";
    j4["nested"]["key"] = "deep";   // 自动创建中间层！"nested":{"key":"deep"}zZ

    // 4c. .update() —— 合并另一个对象
    json patch = {{"counter", 100}, {"extra", "merged"}};
    j4.update(patch);  // 相同 key 覆盖，新 key 追加

    std::cout << "\n=== 4. 修改 ===" << std::endl;
    std::cout << "j4: " << j4.dump() << std::endl;

    // ╔══════════════════════════════════════════════════════╗
    // ║  5. 数组操作                                        ║
    // ╚══════════════════════════════════════════════════════╝

    json arr = json::array();   // 创建空数组

    // 5a. 添加元素
    arr.push_back("apple");
    arr.push_back(42);
    arr.push_back({{"key", "value"}});  // 数组里可以放对象

    // 5b. 随机访问
    std::cout << "\n=== 5. 数组 ===" << std::endl;
    std::cout << "arr[0] = " << arr[0] << std::endl;

    // 5c. 遍历数组
    std::cout << "遍历数组: ";
    for (const auto& item : arr) {
        std::cout << item << "  ";
    }
    std::cout << std::endl;

    // 5d. 大小和判空
    std::cout << "arr.size() = " << arr.size() << std::endl;
    std::cout << "arr.empty() = " << arr.empty() << std::endl;

    // ╔══════════════════════════════════════════════════════╗
    // ║  6. 遍历对象                                        ║
    // ╚══════════════════════════════════════════════════════╝

    json obj = {
        {"name", "gateway"},
        {"port", 443},
        {"tls", true}
    };

    std::cout << "\n=== 6. 遍历对象 ===" << std::endl;
    // 6a. 遍历 key-value —— 结构化绑定 (C++17)
    for (auto& [key, value] : obj.items()) {
        std::cout << key << " : " << value << std::endl;
    }

    // 6b. 只遍历值
    std::cout << "遍历值: ";
    for (const auto& val : obj) {
        std::cout << val << "  ";
    }
    std::cout << std::endl;

    // ╔══════════════════════════════════════════════════════╗
    // ║  7. 序列化（JSON → 字符串 / 文件）                  ║
    // ╚══════════════════════════════════════════════════════╝

    json out = {
        {"type", "RpcResponse"},
        {"id", 12345},
        {"result", 42}
    };

    // 7a. dump() —— 转成 std::string（紧凑无空格）
    std::string compact = out.dump();
    std::cout << "\n=== 7. 序列化 ===" << std::endl;
    std::cout << "紧凑: " << compact << std::endl;

    // 7b. dump(4) —— 缩进 4 空格，便于调试
    std::string pretty = out.dump(4);
    std::cout << "格式化:\n" << pretty << std::endl;

    // 7c. 直接写入文件
    // std::ofstream f("output.json");
    // f << out.dump(4);

    // ╔══════════════════════════════════════════════════════╗
    // ║  8. 反序列化（字符串 → JSON）                       ║
    // ╚══════════════════════════════════════════════════════╝

    // 8a. json::parse(字符串) —— 从字符串解析
    std::string raw = R"({"method":"Add","params":[1,2,3]})";
    json parsed = json::parse(raw);
    std::cout << "\n=== 8. 反序列化 ===" << std::endl;
    std::cout << "method: " << parsed["method"] << std::endl;
    std::cout << "params: " << parsed["params"].dump() << std::endl;

    // 8b. json::parse(文件流) —— 从文件读取
    // std::ifstream in("config.json");
    // json cfg = json::parse(in);

    // 8c. 处理解析错误
    try {
        json bad = json::parse("not valid json!!!");
    } catch (json::parse_error& e) {
        std::cout << "解析失败: " << e.what() << std::endl;
    }

    // ╔══════════════════════════════════════════════════════╗
    // ║  9. RPC 场景实战 —— 构造请求 & 解析响应             ║
    // ╚══════════════════════════════════════════════════════╝

    std::cout << "\n=== 9. RPC 实战 ===" << std::endl;

    // 9a. 构造 RPC 请求
    json request = {
        {"version", "1.0"},
        {"id", 1001},
        {"service", "MathService"},
        {"method", "Add"},
        {"params", json::array({1, 2, 3})},
        {"options", {
            {"timeout", 5000},
            {"compress", true}
        }}
    };
    std::cout << "请求: " << request.dump(2) << std::endl;

    // 9b. 模拟解析 RPC 响应
    std::string response_raw = R"({
        "id": 1001,
        "error": null,
        "result": 6
    })";
    json response = json::parse(response_raw);

    // 9c. 安全解析响应
    if (response.contains("error") && !response["error"].is_null()) {
        std::cout << "调用失败: " << response["error"] << std::endl;
    } else if (response.contains("result")) {
        int sum = response["result"];
        std::cout << "调用成功! Add(1,2,3) = " << sum << std::endl;
    }

    // ╔══════════════════════════════════════════════════════╗
    // ║  10. 常用简写 / 技巧                                 ║
    // ╚══════════════════════════════════════════════════════╝

    // 10a. .flatten() —— 把嵌套对象打平
    json nested = {{"a", {{"b", 1}, {"c", 2}}}, {"d", 3}};
    json flat = nested.flatten();
    std::cout << "\n=== 10. 技巧 ===" << std::endl;
    std::cout << "打平: " << flat.dump() << std::endl;
    // 输出: {"/a/b":1,"/a/c":2,"/d":3}

    // 10b. 判断是否为 null
    json nil;
    std::cout << "null? " << nil.is_null() << std::endl;

    // 10c. swap —— 交换两个 json 对象
    json a = {{"x", 1}};
    json b = {{"y", 2}};
    std::swap(a, b);
    std::cout << "swap后: a=" << a.dump() << " b=" << b.dump() << std::endl;

    return 0;
}
