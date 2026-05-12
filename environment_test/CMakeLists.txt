cmake_minimum_required(VERSION 3.16)
project(ProtobufTest LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# vcpkg packages - use explicit mingw-static paths (NOT toolchain, which defaults to MSVC)
set(VCPKG_PKGS "D:/vcpkg-master/vcpkg-master/packages")
list(APPEND CMAKE_PREFIX_PATH
    "${VCPKG_PKGS}/protobuf_x64-mingw-static"
    "${VCPKG_PKGS}/abseil_x64-mingw-static"
    "${VCPKG_PKGS}/utf8-range_x64-mingw-static"
)

find_package(protobuf CONFIG REQUIRED)

add_executable(protobuf_test
    protobuf_test.cpp
    test_msg.pb.cc
)

target_include_directories(protobuf_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(protobuf_test PRIVATE
    protobuf::libprotobuf
)
