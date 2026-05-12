# CLAUDE.md

## 项目概述

- **项目名**：CookRPC — C++ RPC 框架
- **目标**：高性能、易用的 RPC 通信框架，支持服务注册发现、多种序列化协议、负载均衡
- **语言标准**：C++20
- **构建系统**：CMake

## 开发环境

- **OS**：Windows 11，MSYS2/MinGW UCRT64
- **编译器**：GCC 15.2.0（路径：`E:/msys/ucrt64/bin/g++.exe`）
- **包管理器**：Vcpkg（`D:/vcpkg-master/vcpkg-master/`）
- **IDE**：VSCode

## 关键依赖

| 库 | 版本 | 用途 |
| --- | --- | --- |
| Boost | 1.90 | 网络 IO（Asio） |
| ZooKeeper C Client | 3.9.5 | 服务注册与发现 |
| Protobuf | 6.33.4 | 序列化 |
| spdlog | 1.17.0 | 日志 |
| zstd | 1.5.7 | 压缩 |
| nlohmann/json | 3.12.0 | JSON 解析 |
| OpenSSL | - | AES-256-GCM 加密 |

### ZK 编译注意事项

- 需定义 `-DTHREADED` 才能使用 ZK 同步 API
- MinGW 需额外定义：`-mno-xop -D_X86INTRIN_H_INCLUDED -D_EMMINTRIN_H_INCLUDED`
- 链接：`-lzookeeper -lhashtable -lws2_32`（及相关 Windows 库）
- 头文件：`-I/e/msys/ucrt64/include`
- 库文件：`-L/e/msys/ucrt64/lib`

## 目录结构

```text
RPC/
├── src/                           # 源码目录
│   ├── core/                      # 核心组装层
│   │   ├── rpc_client.h/cpp       #   RPC 客户端
│   │   ├── rpc_service.h/cpp      #   RPC 服务端
│   │   └── error_code.h           #   错误码枚举
│   ├── network/                   # 网络通信层
│   │   ├── connection.h/cpp       #   单连接异步读写
│   │   ├── connection_manager.h/cpp # 连接池管理
│   │   ├── message_cycle.h/cpp    #   事件循环 + accept
│   │   └── create_socket.h/cpp    #   Socket 创建工具
│   ├── protocol/                  # 协议层
│   │   └── rpc_protocol.h/cpp     #   帧头/请求/响应 + CRC32
│   ├── service/                   # 服务管理
│   │   ├── service.h              #   Service 抽象基类
│   │   └── service_manager.h/cpp  #   服务注册/分发（单例）
│   ├── serializer/                # 序列化模块
│   │   └── serializer.h           #   Protobuf / JSON 实现
│   ├── compress_data/             # 数据压缩模块
│   │   └── compress.h/cpp         #   zstd 压缩/解压
│   ├── encrypt/                   # 数据加密模块
│   │   └── encrypt.h/cpp          #   AES-256-GCM 加密/解密
│   ├── thread_pool/               # 线程池模块
│   │   ├── thread_pool.h/cpp      #   任务队列 + worker 线程
│   │   └── thread_pool_singleton.h/cpp # 全局单例
│   ├── registry/                  # 服务注册与发现
│   │   ├── service_registry.h/cpp #   ZooKeeper 服务注册
│   │   └── node_manager.h/cpp     #   节点管理
│   ├── conn_balancer/             # 连接负载均衡
│   │   └── conn_balancer.h/cpp    #   可插拔策略
│   ├── load_config/               # 配置加载模块
│   │   ├── load_config.h/cpp      #   JSON 配置解析
│   │   └── log_init.h/cpp         #   日志初始化
│   ├── protos/                    # Protobuf 定义
│   │   ├── message_proto.pb.h/cc  #   生成的 C++ 代码
│   │   └── message_pb.h/cpp       #   业务消息封装
│   ├── config/                    # 配置文件
│   │   └── server_config.json
│   ├── tools/                     # 工具类（header-only）
│   │   └── tools.h
│   ├── utils/                     # 通用工具（header-only）
│   │   └── utils.h
│   ├── server_main.cpp            # 服务端入口
│   └── client_main.cpp            # 客户端入口
├── environment_test/              # 测试文件
│   ├── integration_test.cpp       #   完整集成测试
│   ├── network_test.cpp           #   网络模块测试
│   ├── serializer_protocol_test.cpp # 序列化+协议测试
│   ├── compress_encrypt_test.cpp  #   压缩+加密测试
│   ├── thread_pool_test.cpp       #   线程池测试
│   ├── registry_test.cpp          #   服务注册测试
│   ├── balancer_test.cpp          #   负载均衡测试
│   ├── config_test.cpp            #   配置加载测试
│   ├── log_test.cpp               #   日志测试
│   ├── json_test.cpp              #   JSON 环境测试
│   ├── protobuf_test.cpp          #   Protobuf 环境测试
│   ├── spdlog_test.cpp            #   spdlog 环境测试
│   ├── zstd_test.cpp              #   zstd 环境测试
│   └── zookeeper_test.cpp         #   ZK 环境测试
├── md/                            # 模块设计文档
│   ├── core.md
│   ├── network.md
│   ├── rpc_protocol.md
│   ├── serializer.md
│   ├── service_registration.md
│   ├── compress.md
│   ├── encrypt.md
│   ├── thread_pool.md
│   ├── load_config.md
│   ├── spdlog.md
│   ├── service_registry.md
│   ├── conn_balancer.md
│   └── node_manager.md
├── docs/                          # 需求文档
│   └── requirements/
│       └── TEMPLATE.md
├── include/                       # 第三方头文件
│   └── zk_client/
│       ├── zk_client.hpp          #   自定义 ZK 客户端
│       └── zk_proto.hpp           #   ZK 协议实现
├── build/                         # 构建输出（CMake）
├── logs/                          # 运行日志输出
├── CMakeLists.txt                 # CMake 构建配置
├── readme.md                      # 项目说明
├── rpc_build.sh                   # 构建脚本
├── build.sh                       # 简化构建脚本
└── stress_test.sh                 # 压力测试脚本
```

## 代码规范

- 命名风格使用驼峰命名法，其余无特殊要求，同一风格即可
- 头文件使用 `#pragma once`
- 命名空间：`rpc`，按模块划分子目录

## 现有模块（已实现）

| 模块 | 目录 | 状态 |
| --- | --- | --- |
| 配置加载 | `src/load_config/` | 完成 |
| 日志初始化 | `src/load_config/` | 完成 |
| 序列化器 | `src/serializer/` | 完成（Protobuf + JSON） |
| RPC 协议 | `src/protocol/` | 完成 |
| 服务管理 | `src/service/` | 完成 |
| 网络通信 | `src/network/` | 完成 |
| 线程池 | `src/thread_pool/` | 完成 |
| 数据压缩 | `src/compress_data/` | 完成 |
| 数据加密 | `src/encrypt/` | 完成 |
| 服务注册发现 | `src/registry/` | 完成（ZK 集成） |
| 负载均衡 | `src/conn_balancer/` | 完成 |
| 核心组装 | `src/core/` | 完成 |
| ZK 客户端 | `include/zk_client/` | 完成（纯 C++ 实现） |

## 工作流

1. 需求文档放在 `docs/requirements/` 下
2. 实现前先出计划，经审批后编码
3. 每个模块完成后编译验证
4. 提交代码

## 注意事项

- 不允许私自修改任何环境变量
- Windows 上 `ERROR` 是预定义宏，代码中使用 `RPC_ERROR` 避免冲突
- 日志宏使用 `RPC_INFO`/`RPC_WARN` 等（定义在 `log_init.h`）
- Boost.Asio 在 Windows 上需链接 `ws2_32` 和 `wsock32`
