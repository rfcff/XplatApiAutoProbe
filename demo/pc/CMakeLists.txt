# =====================================================================
# XplatApiAutoProbe PC 测试工程（Windows / macOS / Linux）
#
# 构建（Windows + Thunder，默认 third_party/thunderbolt）：
#     cmake -S demo/pc -B demo/pc/build -A x64
#     cmake --build demo/pc/build --config Debug
#
# Headless：
#     ./demo/pc/build/Debug/xprobe_demo --headless
# =====================================================================
cmake_minimum_required(VERSION 3.14)
project(XprobePcDemo CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(XPROBE_PC_UI "Build Win32 UI (Windows only; disable for headless-only CI)" ON)
option(XPROBE_USE_THUNDER "Use real Thunder PC SDK (Windows only)" ON)

# thunderboltyy | thunderbolt —— 对应 third_party/thunderbolt/lib/<variant>/release/<arch>
set(XPROBE_THUNDER_VARIANT "thunderboltyy" CACHE STRING
    "Import-lib variant under third_party/thunderbolt/lib")
set_property(CACHE XPROBE_THUNDER_VARIANT PROPERTY STRINGS thunderboltyy thunderbolt)

if(XPROBE_PC_UI AND NOT WIN32)
    message(FATAL_ERROR "XPROBE_PC_UI=ON 仅支持 Windows（纯 Win32 UI，无第三方 GUI 框架）")
endif()

set(XPROBE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(XPROBE_CORE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../core")
add_subdirectory(${XPROBE_CORE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/xprobe_core)

set(DEMO_SOURCES
    main.cpp
    rpc/DemoInvocation.cpp
    rtc/RtcManager.cpp
)

if(XPROBE_PC_UI)
    list(APPEND DEMO_SOURCES ui/DemoUi.cpp)
endif()

add_executable(xprobe_demo ${DEMO_SOURCES})
target_include_directories(xprobe_demo PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(xprobe_demo PRIVATE xprobe)

# Windows + UI：子系统设为 WINDOWS，双击启动不弹黑色控制台；保留 main() 入口
if(WIN32 AND XPROBE_PC_UI)
    set_target_properties(xprobe_demo PROPERTIES WIN32_EXECUTABLE TRUE)
    if(MSVC)
        target_link_options(xprobe_demo PRIVATE "/ENTRY:mainCRTStartup")
    endif()
endif()

if(XPROBE_USE_THUNDER)
    if(NOT WIN32)
        message(FATAL_ERROR "XPROBE_USE_THUNDER=ON 仅支持 Windows（IThunderEngine / HWND）")
    endif()

    set(_thunder_sdk_root "${CMAKE_CURRENT_SOURCE_DIR}/third_party/thunderbolt")

    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(_thunder_arch "x64")
    else()
        set(_thunder_arch "x86")
    endif()

    # ---- include（3.5.10 头文件，须与 DLL 虚表匹配）----
    set(_default_thunder_include "${_thunder_sdk_root}/include")
    if(NOT EXISTS "${_default_thunder_include}/IThunderEngine.h")
        message(FATAL_ERROR
            "缺少 ${_default_thunder_include}/IThunderEngine.h\n"
            "请将 Thunderbolt 3.5.10 头文件放到 third_party/thunderbolt/include/")
    endif()
    set(XPROBE_THUNDER_INCLUDE "${_default_thunder_include}" CACHE PATH
        "Thunderbolt include (IThunderEngine.h); must match DLL ABI")
    if(NOT EXISTS "${XPROBE_THUNDER_INCLUDE}/IThunderEngine.h")
        message(FATAL_ERROR "找不到 IThunderEngine.h: ${XPROBE_THUNDER_INCLUDE}")
    endif()

    # ---- import lib ----
    set(_default_thunder_lib
        "${_thunder_sdk_root}/lib/${XPROBE_THUNDER_VARIANT}/release/${_thunder_arch}/thunderbolt.lib")
    if(NOT EXISTS "${_default_thunder_lib}")
        # 变体缺失时回退另一套
        if(XPROBE_THUNDER_VARIANT STREQUAL "thunderboltyy")
            set(_fallback_variant "thunderbolt")
        else()
            set(_fallback_variant "thunderboltyy")
        endif()
        set(_alt_lib
            "${_thunder_sdk_root}/lib/${_fallback_variant}/release/${_thunder_arch}/thunderbolt.lib")
        if(EXISTS "${_alt_lib}")
            set(_default_thunder_lib "${_alt_lib}")
        endif()
    endif()
    set(XPROBE_THUNDER_LIB "${_default_thunder_lib}" CACHE FILEPATH
        "Thunderbolt import library (thunderbolt.lib)")
    if(NOT EXISTS "${XPROBE_THUNDER_LIB}")
        message(FATAL_ERROR
            "找不到 thunderbolt.lib: ${XPROBE_THUNDER_LIB}\n"
            "请确认 third_party/thunderbolt/lib/<variant>/release/${_thunder_arch}/")
    endif()

    # ---- runtime DLL：third_party/thunderbolt/release/<arch> ----
    set(_default_thunder_bin "${_thunder_sdk_root}/release/${_thunder_arch}")
    set(XPROBE_THUNDER_BIN "${_default_thunder_bin}" CACHE PATH
        "Thunderbolt runtime dir (thunderbolt.dll)")
    if(NOT EXISTS "${XPROBE_THUNDER_BIN}/thunderbolt.dll")
        message(FATAL_ERROR
            "找不到 thunderbolt.dll: ${XPROBE_THUNDER_BIN}\n"
            "请将运行时放到 third_party/thunderbolt/release/${_thunder_arch}/\n"
            "或 -DXPROBE_THUNDER_BIN=...\n"
            "x64 构建: cmake -S demo/pc -B demo/pc/build -A x64")
    endif()

    target_compile_definitions(xprobe_demo PRIVATE
        XPROBE_USE_THUNDER=1
        XPROBE_THUNDER_LINK_LIB=1
    )
    target_include_directories(xprobe_demo PRIVATE "${XPROBE_THUNDER_INCLUDE}")
    target_link_libraries(xprobe_demo PRIVATE "${XPROBE_THUNDER_LIB}")

    # 构建后拷贝运行时到 exe 同目录
    file(GLOB _thunder_runtime_files
        "${XPROBE_THUNDER_BIN}/*.dll"
        "${XPROBE_THUNDER_BIN}/*.vnmodel"
    )
    foreach(_f IN LISTS _thunder_runtime_files)
        add_custom_command(TARGET xprobe_demo POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_f}" "$<TARGET_FILE_DIR:xprobe_demo>"
            COMMENT "Copy Thunder runtime: ${_f}"
        )
    endforeach()
    # x64 发行包视频库名为 VideoSdk_x64.dll，运行时部分版本仍加载 VideoSdk.dll
    if(EXISTS "${XPROBE_THUNDER_BIN}/VideoSdk_x64.dll")
        add_custom_command(TARGET xprobe_demo POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${XPROBE_THUNDER_BIN}/VideoSdk_x64.dll"
                "$<TARGET_FILE_DIR:xprobe_demo>/VideoSdk.dll"
            COMMENT "Copy VideoSdk_x64.dll as VideoSdk.dll"
        )
    endif()

    message(STATUS "Thunder ON  include=${XPROBE_THUNDER_INCLUDE}")
    message(STATUS "Thunder ON  lib=${XPROBE_THUNDER_LIB}")
    message(STATUS "Thunder ON  bin=${XPROBE_THUNDER_BIN} (copy -> exe dir)")
else()
    message(STATUS "Thunder OFF (simulator RtcManager)")
endif()

if(XPROBE_PC_UI)
    target_compile_definitions(xprobe_demo PRIVATE XPROBE_PC_UI=1)
    target_link_libraries(xprobe_demo PRIVATE user32 gdi32 comctl32 ole32)
    message(STATUS "PC UI: Win32 native")
else()
    target_compile_definitions(xprobe_demo PRIVATE XPROBE_PC_UI=0)
endif()

if(MSVC)
    target_compile_options(xprobe_demo PRIVATE /W4 /utf-8 /wd4819)
    target_compile_definitions(xprobe_demo PRIVATE _CRT_SECURE_NO_WARNINGS)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(xprobe_demo PRIVATE -Wall -Wextra -Wpedantic)
endif()
