# 服务注册与管理模块设计文档

## 概述

服务注册模块提供 RPC 服务的定义、注册、查找和调用分发功能。所有 RPC 服务继承统一的 `Service` 基类，通过 `ServiceManager` 单例进行集中管理，支持线程安全的注册/注销/查找/分发。

## 文件位置

| 文件 | 用途 |
|------|------|
| `src/service/service_manager.h` | `Service` 基类 + `ServiceManager` 单例声明 |
| `src/service/service_manager.cpp` | `ServiceManager` 实现 |

---

## 架构设计

```
                     ServiceManager (单例)
                    ┌──────────────────────────┐
                    │ services_: unordered_map │
                    │ shared_mutex             │
                    ├──────────────────────────┤
                    │ register_service()       │
                    │ unregister_service()     │
                    │ get_service()            │
                    │ has_service()            │
                    │ list_services()          │
                    │ dispatch()               │
                    └──────────┬───────────────┘
                               │
          ┌────────────────────┼────────────────────┐
          ▼                    ▼                    ▼
   ┌─────────────┐    ┌─────────────┐    ┌─────────────┐
   │ UserService │    │ CalcService │    │ AuthService │
   ├─────────────┤    ├─────────────┤    ├─────────────┤
   │ name()      │    │ name()      │    │ name()      │
   │ methods()   │    │ methods()   │    │ methods()   │
   │ handle()    │    │ handle()    │    │ handle()    │
   └─────────────┘    └─────────────┘    └─────────────┘
```

### 线程安全模型

```
多个 Worker 线程同时调用 dispatch()
        │
        ▼
   std::shared_lock (读) → get_service() → handle()
        多个线程可并发读取

   register_service() / unregister_service()
        │
        ▼
   std::unique_lock (写) → 修改 services_ map
        独占写入，等待所有读锁释放
```

- `std::shared_mutex`：读写锁
- 读操作（get_service、has_service、list_services、dispatch）：使用 `shared_lock`，多线程可并发
- 写操作（register_service、unregister_service）：使用 `unique_lock`，互斥写入

---

## API 设计

### Service 基类

```cpp
class Service {
public:
    virtual ~Service() = default;

    // 服务名称（全局唯一标识）
    virtual std::string name() const = 0;

    // 本服务提供的所有方法名列表
    virtual std::vector<std::string> methods() const = 0;

    // 处理远程方法调用
    // method: 方法名（由 methods() 中的某一项）
    // params: 序列化后的参数（protobuf binary 或 JSON string）
    // 返回:   序列化后的结果
    virtual std::string handle(const std::string& method,
                               const std::string& params) = 0;
};
```

### ServiceManager 单例

```cpp
class ServiceManager {
public:
    // 获取单例
    static ServiceManager& instance();

    // 注册服务（同名服务会覆盖旧服务）
    void register_service(std::shared_ptr<Service> service);

    // 注销服务
    void unregister_service(const std::string& name);

    // 查找服务（不存在返回 nullptr）
    std::shared_ptr<Service> get_service(const std::string& name);

    // 检查服务是否存在
    bool has_service(const std::string& name);

    // 列出所有已注册的服务名称（按字母排序）
    std::vector<std::string> list_services();

    // 分发方法调用
    // 如果服务/方法不存在，抛出 std::runtime_error
    std::string dispatch(const std::string& service_name,
                         const std::string& method_name,
                         const std::string& params);
};
```

---

## 如何注册新服务

### 步骤 1：定义 Protobuf 消息

```protobuf
// user.proto
syntax = "proto3";
package user;

message LoginRequest {
    string username = 1;
    string password = 2;
}

message LoginResponse {
    int32  error_code = 1;
    string token      = 2;
}
```

### 步骤 2：实现 Service 子类

```cpp
#include "service/service_manager.h"
#include "user.pb.h"  // 生成的 protobuf 代码

class UserService : public rpc::Service {
public:
    std::string name() const override {
        return "UserService";
    }

    std::vector<std::string> methods() const override {
        return {"Login", "Logout", "GetProfile"};
    }

    std::string handle(const std::string& method,
                       const std::string& params) override {
        if (method == "Login") {
            return handle_login(params);
        } else if (method == "Logout") {
            return handle_logout(params);
        } else if (method == "GetProfile") {
            return handle_getProfile(params);
        }
        throw std::runtime_error("unknown method: " + method);
    }

private:
    std::string handle_login(const std::string& params) {
        // 1. 反序列化请求参数
        user::LoginRequest req;
        req.ParseFromString(params);

        // 2. 业务逻辑
        user::LoginResponse resp;
        if (req.username() == "admin" && req.password() == "123456") {
            resp.set_error_code(0);
            resp.set_token("session_token_xxx");
        } else {
            resp.set_error_code(-1);
        }

        // 3. 序列化返回值
        return resp.SerializeAsString();
    }

    std::string handle_logout(const std::string& params) {
        // ...
        return {};
    }

    std::string handle_getProfile(const std::string& params) {
        // ...
        return {};
    }
};
```

### 步骤 3：注册到 ServiceManager

```cpp
// 服务端启动时
int main() {
    // 注册服务
    auto user_svc = std::make_shared<UserService>();
    rpc::ServiceManager::instance().register_service(user_svc);

    // 验证
    auto services = rpc::ServiceManager::instance().list_services();
    for (auto& name : services) {
        std::cout << "Registered: " << name << std::endl;
    }
    // 输出: Registered: UserService

    // ... 启动网络监听 ...
}
```

### 步骤 4：在 RPC 请求处理中调用分发

```cpp
// 网络层收到 RPC 请求后
void on_rpc_request(const rpc::RpcRequest& req) {
    try {
        std::string result = rpc::ServiceManager::instance().dispatch(
            req.service_name,
            req.method_name,
            req.params
        );
        // 返回成功响应...
    } catch (const std::runtime_error& e) {
        // 返回错误响应...
    }
}
```

### 完整示例：注册多个服务

```cpp
// 创建和注册所有服务
auto user_svc = std::make_shared<UserService>();
auto calc_svc = std::make_shared<CalcService>();
auto auth_svc = std::make_shared<AuthService>();

auto& mgr = rpc::ServiceManager::instance();
mgr.register_service(user_svc);
mgr.register_service(calc_svc);
mgr.register_service(auth_svc);

// 列出所有服务
// → ["AuthService", "CalcService", "UserService"]  (按字母排序)
for (auto& name : mgr.list_services()) {
    auto svc = mgr.get_service(name);
    std::cout << name << " provides: ";
    for (auto& m : svc->methods()) {
        std::cout << m << " ";
    }
    std::cout << "\n";
}
```

---

## 测试结果

| 测试 | 内容 | 结果 |
|------|------|------|
| register service | 注册后 has_service 返回 true | OK |
| get service | 获取已注册服务 | OK |
| get non-existent | 不存在的服务返回 nullptr | OK |
| has_service | 正确判断服务存在性 | OK |
| list services | 列出并排序所有服务名 | OK |
| dispatch Echo | 调用 Echo 方法返回原值 | OK |
| dispatch Greet | 调用 Greet 方法拼接字符串 | OK |
| dispatch bad service | 不存在的服务抛异常 | OK |
| dispatch bad method | 不存在的方法抛异常 | OK |
| unregister service | 注销后服务不存在 | OK |
| re-register | 覆盖注册更新 | OK |

---

## 编译依赖

- C++17（`std::shared_mutex`）
- 纯标准库，无外部依赖
