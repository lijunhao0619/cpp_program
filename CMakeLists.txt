cmake_minimum_required(VERSION 3.16)
project(rpc_project VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# vcpkg packages
set(VCPKG_PKGS "D:/vcpkg-master/vcpkg-master/packages")
list(APPEND CMAKE_PREFIX_PATH
    "${VCPKG_PKGS}/nlohmann-json_x64-mingw-static"
    "${VCPKG_PKGS}/protobuf_x64-mingw-static"
    "${VCPKG_PKGS}/abseil_x64-mingw-static"
    "${VCPKG_PKGS}/utf8-range_x64-mingw-static"
    "${VCPKG_PKGS}/zstd_x64-mingw-static"
)

find_package(nlohmann_json CONFIG REQUIRED)
find_package(spdlog REQUIRED)
find_package(zstd CONFIG REQUIRED)
find_package(protobuf CONFIG REQUIRED)

# Compiler warnings
if(MSVC)
    add_compile_options(/W4 /permissive- /utf-8)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -DDEBUG")
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG")

# common sources for rpc_server / rpc_client (shared modules)

file(GLOB_RECURSE COMMON_SOURCES src/*.cpp)
list(REMOVE_ITEM COMMON_SOURCES
    "${CMAKE_SOURCE_DIR}/src/server_main.cpp"
    "${CMAKE_SOURCE_DIR}/src/client_main.cpp"
    "${CMAKE_SOURCE_DIR}/environment_test/json_test.cpp"
    "${CMAKE_SOURCE_DIR}/environment_test/spdlog_test.cpp"
    "${CMAKE_SOURCE_DIR}/environment_test/zstd_test.cpp"
    "${CMAKE_SOURCE_DIR}/environment_test/zookeeper_test.cpp"
)

add_executable(rpc_server src/server_main.cpp ${COMMON_SOURCES})
target_include_directories(rpc_server PRIVATE
    include
    src
    /e/msys/ucrt64/include
    ${VCPKG_PKGS}/protobuf_x64-mingw-static/include
    ${VCPKG_PKGS}/abseil_x64-mingw-static/include
    ${VCPKG_PKGS}/utf8-range_x64-mingw-static/include)
target_link_directories(rpc_server PRIVATE /e/msys/ucrt64/lib)
target_link_libraries(rpc_server PRIVATE
    nlohmann_json::nlohmann_json
    spdlog::spdlog
    zstd::libzstd
    protobuf::libprotobuf
    ssl
    crypto
    ws2_32
    wsock32)

add_executable(rpc_client src/client_main.cpp ${COMMON_SOURCES})
target_include_directories(rpc_client PRIVATE
    include
    src
    /e/msys/ucrt64/include
    ${VCPKG_PKGS}/protobuf_x64-mingw-static/include
    ${VCPKG_PKGS}/abseil_x64-mingw-static/include
    ${VCPKG_PKGS}/utf8-range_x64-mingw-static/include)
target_link_directories(rpc_client PRIVATE /e/msys/ucrt64/lib)
target_link_libraries(rpc_client PRIVATE
    nlohmann_json::nlohmann_json
    spdlog::spdlog
    zstd::libzstd
    protobuf::libprotobuf
    ssl
    crypto
    ws2_32
    wsock32)

add_executable(json_tutorial environment_test/json_test.cpp)
target_include_directories(json_tutorial PRIVATE include)
target_link_libraries(json_tutorial PRIVATE nlohmann_json::nlohmann_json spdlog::spdlog)

add_executable(spdlog_tutorial environment_test/spdlog_test.cpp)
target_include_directories(spdlog_tutorial PRIVATE include)
target_link_libraries(spdlog_tutorial PRIVATE nlohmann_json::nlohmann_json spdlog::spdlog)

add_executable(zstd_tutorial environment_test/zstd_test.cpp)
target_include_directories(zstd_tutorial PRIVATE include)
target_link_libraries(zstd_tutorial PRIVATE zstd::libzstd)

add_executable(zookeeper_tutorial environment_test/zookeeper_test.cpp)
target_include_directories(zookeeper_tutorial PRIVATE include)

add_executable(log_test environment_test/log_test.cpp src/load_config/log_init.cpp)
target_include_directories(log_test PRIVATE include src)
target_link_libraries(log_test PRIVATE nlohmann_json::nlohmann_json spdlog::spdlog)

add_executable(config_test environment_test/config_test.cpp src/load_config/load_config.cpp)
target_include_directories(config_test PRIVATE include src)
target_link_libraries(config_test PRIVATE nlohmann_json::nlohmann_json spdlog::spdlog)

add_executable(registry_test environment_test/registry_test.cpp src/registry/service_registry.cpp)
target_include_directories(registry_test PRIVATE include src /e/msys/ucrt64/include)
target_link_libraries(registry_test PRIVATE nlohmann_json::nlohmann_json spdlog::spdlog ws2_32)

add_executable(balancer_test
    environment_test/balancer_test.cpp
    src/conn_balancer/conn_balancer.cpp
    src/registry/service_registry.cpp
    src/registry/node_manager.cpp)
target_include_directories(balancer_test PRIVATE include src /e/msys/ucrt64/include)
target_link_libraries(balancer_test PRIVATE nlohmann_json::nlohmann_json spdlog::spdlog ws2_32)

add_executable(thread_pool_test
    environment_test/thread_pool_test.cpp
    src/thread_pool/thread_pool.cpp
    src/thread_pool/thread_pool_singleton.cpp)
target_include_directories(thread_pool_test PRIVATE include src)
target_link_libraries(thread_pool_test PRIVATE)

add_executable(compress_encrypt_test
    environment_test/compress_encrypt_test.cpp
    src/compress_data/compress.cpp
    src/encrypt/encrypt.cpp)
target_include_directories(compress_encrypt_test PRIVATE include src /e/msys/ucrt64/include)
target_link_directories(compress_encrypt_test PRIVATE /e/msys/ucrt64/lib)
target_link_libraries(compress_encrypt_test PRIVATE zstd::libzstd ssl crypto)

add_executable(serializer_protocol_test
    environment_test/serializer_protocol_test.cpp
    src/protocol/rpc_protocol.cpp
    src/service/service_manager.cpp
    src/protos/message_proto.pb.cc)
target_include_directories(serializer_protocol_test PRIVATE
    include
    src
    ${VCPKG_PKGS}/protobuf_x64-mingw-static/include
    ${VCPKG_PKGS}/abseil_x64-mingw-static/include
    ${VCPKG_PKGS}/utf8-range_x64-mingw-static/include)
target_link_libraries(serializer_protocol_test PRIVATE
    protobuf::libprotobuf
    nlohmann_json::nlohmann_json)

add_executable(network_test
    environment_test/network_test.cpp
    src/network/connection.cpp
    src/network/connection_manager.cpp
    src/network/create_socket.cpp
    src/network/message_cycle.cpp
    src/core/rpc_client.cpp
    src/core/rpc_service.cpp
    src/service/service_manager.cpp
    src/protocol/rpc_protocol.cpp)
target_include_directories(network_test PRIVATE
    include
    src
    /e/msys/ucrt64/include)
target_link_libraries(network_test PRIVATE
    ws2_32 wsock32)

add_executable(integration_test
    environment_test/integration_test.cpp
    src/core/rpc_client.cpp
    src/core/rpc_service.cpp
    src/service/service_manager.cpp
    src/protocol/rpc_protocol.cpp
    src/network/connection.cpp
    src/network/connection_manager.cpp
    src/network/create_socket.cpp
    src/network/message_cycle.cpp
    src/thread_pool/thread_pool.cpp
    src/thread_pool/thread_pool_singleton.cpp)
target_include_directories(integration_test PRIVATE
    include
    src
    /e/msys/ucrt64/include)
target_link_libraries(integration_test PRIVATE
    ws2_32 wsock32)
