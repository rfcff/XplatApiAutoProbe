// =====================================================================
// demo/pc/main.cpp
// XplatApiAutoProbe PC 测试工程：与 Android demo 测试面对齐（ver=2 RPC +
// RtcManager 模拟器 + 可选 Dear ImGui UI / --headless CI 模式）。
// =====================================================================
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

#include "rpc/DemoInvocation.h"
#include "rtc/RtcManager.h"
#include "xprobe/AutoTestMgr.h"
#include "xprobe/logger.h"

#if defined(XPROBE_PC_UI) && XPROBE_PC_UI
#include "ui/DemoUi.h"
#endif

static std::atomic<bool> gRunning{true};

static void handleSignal(int sig) {
    (void)sig;
    gRunning.store(false);
}

static const char* demoLevelName(int level) {
    switch (level) {
        case xprobe::LogLevel::VERBOSE: return "VERBOSE";
        case xprobe::LogLevel::DEBUG:   return "DEBUG";
        case xprobe::LogLevel::INFO:    return "INFO";
        case xprobe::LogLevel::WARN:    return "WARN";
        case xprobe::LogLevel::ERROR:   return "ERROR";
        default:                        return "UNKNOWN";
    }
}

static void demoLogCallback(int level, const std::string& tag, const std::string& msg) {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    int ms = static_cast<int>(
        std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000);
    std::tm tmBuf;
#ifdef _WIN32
    localtime_s(&tmBuf, &t);
#else
    localtime_r(&t, &tmBuf);
#endif
    char timeBuf[16];
    std::strftime(timeBuf, sizeof(timeBuf), "%H:%M:%S", &tmBuf);
    std::printf("[%s.%03d][%s][%s] %s\n", timeBuf, ms, demoLevelName(level), tag.c_str(), msg.c_str());
}

static bool isHeadlessMode(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--headless") == 0) {
            return true;
        }
    }
    const char* env = std::getenv("XPROBE_PC_HEADLESS");
    return env != nullptr && env[0] == '1' && env[1] == '\0';
}

static void runHeadlessWaitLoop() {
    while (gRunning.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
}

int main(int argc, char** argv) {
    const bool headless = isHeadlessMode(argc, argv);

    std::printf("==== XplatApiAutoProbe PC demo ====\n");
    std::printf("RPC 监听 0.0.0.0:9000");
#if defined(XPROBE_PC_UI) && XPROBE_PC_UI
    if (headless) {
        std::printf("（headless 模式，按 Ctrl+C 退出）\n");
    } else {
        std::printf("（ImGui UI）\n");
    }
#else
    std::printf("（无 UI 构建，按 Ctrl+C 退出）\n");
#endif

    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);

    xprobe::setLogCallback(demoLogCallback);

    xprobe::AutoTestMgr mgr;
    RtcManager::getInstance().setAutoTestMgr(&mgr);
    DemoInvocation invocation(&mgr);
    mgr.start(&invocation, 9000);
    std::printf("[demo] AutoTestMgr 已启动（端口 9000）\n");

#if defined(XPROBE_PC_UI) && XPROBE_PC_UI
    if (!headless) {
        runDemoUi(&mgr);
    } else {
        runHeadlessWaitLoop();
    }
#else
    (void)headless;
    runHeadlessWaitLoop();
#endif

    std::printf("[demo] 正在停止服务...\n");
    mgr.stop();
    std::printf("[demo] 已退出\n");
    return 0;
}
