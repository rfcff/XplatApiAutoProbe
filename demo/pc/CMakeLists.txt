# =====================================================================
# XplatApiAutoProbe PC 测试工程（Windows / macOS / Linux）
#
# 构建：
#     cmake -S demo/pc -B demo/pc/build
#     cmake --build demo/pc/build
#     ./demo/pc/build/xprobe_demo              # ImGui UI（默认）
#     ./demo/pc/build/xprobe_demo --headless   # CI / 无窗口模式
#
# 可选真实 Thunder（仅 Windows，运行时 LoadLibrary DLL）：
#     cmake -S demo/pc -B demo/pc/build -DXPROBE_USE_THUNDER=ON \
#       -DXPROBE_THUNDER_INCLUDE=/path/to/thunder/win/src/include
#     # 运行前将 thunderbolt.dll 放到 PATH，或设置环境变量：
#     #   set XPROBE_THUNDER_DLL=C:\path\to\thunderbolt.dll
#
# 产物：demo/pc/build/xprobe_demo —— 链接 core/ 产出的 xprobe 静态库
# =====================================================================
cmake_minimum_required(VERSION 3.14)
project(XprobePcDemo CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(XPROBE_PC_UI "Build Dear ImGui + GLFW UI (disable for headless-only CI)" ON)
option(XPROBE_USE_THUNDER "Use real Thunder PC SDK (Windows only, LoadLibrary)" OFF)

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

if(XPROBE_USE_THUNDER)
    if(NOT WIN32)
        message(FATAL_ERROR "XPROBE_USE_THUNDER=ON 仅支持 Windows（IThunderEngine / HWND / LoadLibrary）")
    endif()
    set(_default_thunder_include "")
    get_filename_component(_yyinc_root "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
    if(EXISTS "${_yyinc_root}/thunder/thunder/win/src/include/IThunderEngine.h")
        set(_default_thunder_include "${_yyinc_root}/thunder/thunder/win/src/include")
    endif()
    set(XPROBE_THUNDER_INCLUDE "${_default_thunder_include}" CACHE PATH
        "Path to Thunder Windows SDK include dir (IThunderEngine.h)")
    if(NOT XPROBE_THUNDER_INCLUDE OR NOT EXISTS "${XPROBE_THUNDER_INCLUDE}/IThunderEngine.h")
        message(FATAL_ERROR
            "找不到 IThunderEngine.h。请设置 -DXPROBE_THUNDER_INCLUDE=/path/to/thunder/win/src/include")
    endif()
    target_compile_definitions(xprobe_demo PRIVATE XPROBE_USE_THUNDER=1)
    target_include_directories(xprobe_demo PRIVATE "${XPROBE_THUNDER_INCLUDE}")
    message(STATUS "Thunder ON  include=${XPROBE_THUNDER_INCLUDE} (runtime LoadLibrary)")
else()
    message(STATUS "Thunder OFF (simulator RtcManager)")
endif()

if(XPROBE_PC_UI)
    target_compile_definitions(xprobe_demo PRIVATE XPROBE_PC_UI=1)

    include(FetchContent)
    set(FETCHCONTENT_QUIET OFF)

    FetchContent_Declare(glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG        3.3.9
        GIT_SHALLOW    TRUE
    )
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)

    FetchContent_Declare(imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG        v1.90.9
        GIT_SHALLOW    TRUE
    )

    FetchContent_MakeAvailable(glfw imgui)

    add_library(imgui_demo_lib STATIC
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
    )
    target_include_directories(imgui_demo_lib PUBLIC
        ${imgui_SOURCE_DIR}
        ${imgui_SOURCE_DIR}/backends
    )
    target_link_libraries(imgui_demo_lib PUBLIC glfw)

    target_link_libraries(xprobe_demo PRIVATE imgui_demo_lib)

    if(APPLE)
        target_link_libraries(xprobe_demo PRIVATE
            "-framework OpenGL"
            "-framework Cocoa"
            "-framework IOKit"
            "-framework CoreVideo"
        )
    elseif(UNIX)
        find_package(OpenGL REQUIRED)
        target_link_libraries(xprobe_demo PRIVATE OpenGL::GL ${CMAKE_DL_LIBS})
    elseif(WIN32)
        target_link_libraries(xprobe_demo PRIVATE opengl32)
    endif()
else()
    target_compile_definitions(xprobe_demo PRIVATE XPROBE_PC_UI=0)
endif()

if(MSVC)
    target_compile_options(xprobe_demo PRIVATE /W4)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(xprobe_demo PRIVATE -Wall -Wextra -Wpedantic)
endif()
