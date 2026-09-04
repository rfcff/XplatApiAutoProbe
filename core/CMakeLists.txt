# =====================================================================
# XplatApiAutoProbe C++ Core 构建脚本
# 目标：
#   xprobe      —— 跨平台 RPC 核心静态库（C++17，无外部依赖）
#   xprobe_test —— 单元/集成测试（ctest 注册，可通过 XPROBE_BUILD_TESTS 关闭）
#
# include/ 布局：
#   xprobe/                      —— 核心公开头
#   third_party/pthread/         —— Windows 用 pthread-win32 公共头
#
# 各平台的测试工程已统一收敛到仓库根目录的 demo/ 下：
#   demo/pc        —— PC 宿主程序（自带 CMakeLists，通过 add_subdirectory 复用本文件）
#   demo/android   —— Android App（Gradle，直接引用 android/xprobe 模块）
#   demo/ios       —— iOS / macOS 宿主程序（clang 直接编译，见 demo/ios/README.md）
# =====================================================================
cmake_minimum_required(VERSION 3.14)
project(XplatApiAutoProbeCore CXX)

# ---- 可选开关：被 demo/pc 作为子目录引入时关闭单测，只构建核心库 ----
option(XPROBE_BUILD_TESTS "构建 xprobe_test 单元测试" ON)

# ---- 全局编译选项：C++17，禁用编译器扩展，尽量消除警告 ----
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 依据编译器追加告警选项（MSVC 用 /W4，Clang/GCC 用 -Wall -Wextra -Wpedantic）
# MSVC 必须加 /utf-8：源文件含中文注释/字符串，否则在中文 Windows（代码页 936）
# 下会把 UTF-8 按系统代码页误读，触发 C4819 并级联出大量语法错误。
function(xprobe_apply_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /utf-8)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

# ---- 核心静态库 xprobe ----
add_library(xprobe STATIC
    src/json.cpp
    src/logger.cpp
    src/ThreadPool.cpp
    src/TcpServer.cpp
    src/protocol.cpp
    src/AutoTestMgr.cpp
)
target_include_directories(xprobe PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(xprobe PUBLIC cxx_std_17)
xprobe_apply_warnings(xprobe)

# Windows：暴露 third_party/pthread，使 #include <pthread.h> 可用（勿在非 Windows
# 上加入，以免覆盖系统 pthread 头）
if(WIN32)
    target_include_directories(xprobe PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include/third_party/pthread)
    target_link_libraries(xprobe PUBLIC ws2_32)
endif()

# ---- 测试程序（ctest 注册）；demo 宿主程序见 demo/pc/CMakeLists.txt ----
if(XPROBE_BUILD_TESTS)
    enable_testing()
    add_executable(xprobe_test test/TestCore.cpp)
    target_link_libraries(xprobe_test PRIVATE xprobe)
    xprobe_apply_warnings(xprobe_test)
    add_test(NAME xprobe_test COMMAND xprobe_test)
endif()
