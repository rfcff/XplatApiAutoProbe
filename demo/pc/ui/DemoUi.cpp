#include "ui/DemoUi.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>
#if defined(XPROBE_USE_THUNDER) && defined(_WIN32)
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#endif

#include "rtc/RtcManager.h"
#include "xprobe/AutoTestMgr.h"

namespace {

constexpr int kMaxLogLines = 300;

class UiMainThreadPoster : public xprobe::IMainThreadPoster {
public:
    void post(std::function<void()> task) override {
        if (!task) {
            return;
        }
        std::lock_guard<std::mutex> lock(mtx_);
        queue_.push_back(std::move(task));
    }

    void drain() {
        std::vector<std::function<void()>> batch;
        {
            std::lock_guard<std::mutex> lock(mtx_);
            batch.swap(queue_);
        }
        for (auto& fn : batch) {
            if (fn) {
                fn();
            }
        }
    }

private:
    std::mutex mtx_;
    std::vector<std::function<void()>> queue_;
};

struct UiState {
    char appId[32] = "10034";
    char sceneId[16] = "0";
    char room[32] = "82552971";
    char uid[32] = "123456789";

    bool cameraOn = false;
    bool subAudioOn = false;
    bool subVideoOn = false;

    std::deque<std::string> logs;
    std::mutex logMtx;
};

void appendLog(UiState& state, const std::string& msg) {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tmBuf;
#ifdef _WIN32
    localtime_s(&tmBuf, &t);
#else
    localtime_r(&t, &tmBuf);
#endif
    char timeBuf[16];
    std::strftime(timeBuf, sizeof(timeBuf), "%H:%M:%S", &tmBuf);

    std::lock_guard<std::mutex> lock(state.logMtx);
    state.logs.emplace_back(std::string("[") + timeBuf + "] " + msg);
    while (static_cast<int>(state.logs.size()) > kMaxLogLines) {
        state.logs.pop_front();
    }
}

void resetToggles(UiState& state) {
    state.cameraOn = false;
    state.subAudioOn = false;
    state.subVideoOn = false;
}

void drawVideoPlaceholder(const char* label, ImVec4 color, float height) {
    ImGui::TextUnformatted(label);
    ImVec2 avail = ImGui::GetContentRegionAvail();
    ImVec2 size(avail.x, height);
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y),
                        ImGui::ColorConvertFloat4ToU32(color), 4.0f);
    ImGui::Dummy(size);
    ImGui::TextDisabled("(simulated preview)");
}

void toggleSubscribe(UiState& state, bool audio) {
    RtcManager& rtc = RtcManager::getInstance();
    std::string room = state.room[0] != '\0' ? state.room : rtc.getRoomName();
    std::string remoteUid = rtc.getRemoteUid();
    if (remoteUid.empty()) {
        appendLog(state, "尚未收到远端用户（onUserJoined）");
        return;
    }

    bool on = audio ? state.subAudioOn : state.subVideoOn;
    int ret = 0;
    if (on) {
        ret = rtc.removeSubscribe(room, remoteUid);
    } else {
        ret = rtc.addSubscribe(room, remoteUid);
        if (ret == 0 && !audio) {
            rtc.setupRemoteVideo(remoteUid);
        }
    }
    if (ret == 0) {
        if (audio) {
            state.subAudioOn = !state.subAudioOn;
        } else {
            state.subVideoOn = !state.subVideoOn;
        }
    } else {
        appendLog(state, "订阅操作失败: " + std::to_string(ret));
    }
}

} // namespace

bool runDemoUi(xprobe::AutoTestMgr* mgr) {
    if (!glfwInit()) {
        std::fprintf(stderr, "[demo] GLFW 初始化失败，无法启动 UI\n");
        return false;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    GLFWwindow* window = glfwCreateWindow(960, 720, "Xprobe PC Demo", nullptr, nullptr);
    if (window == nullptr) {
        std::fprintf(stderr, "[demo] 创建 GLFW 窗口失败\n");
        glfwTerminate();
        return false;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

#if defined(XPROBE_USE_THUNDER) && defined(_WIN32)
    RtcManager::getInstance().setVideoHwnds(glfwGetWin32Window(window),
                                            glfwGetWin32Window(window));
#endif

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    UiMainThreadPoster poster;
    if (mgr != nullptr) {
        mgr->setMainThreadPoster(&poster);
    }

    UiState state;
    RtcManager& rtc = RtcManager::getInstance();
    rtc.setUiLogger([&state](const std::string& msg) { appendLog(state, msg); });
    appendLog(state, "demo 已启动，RPC 服务监听 0.0.0.0:9000");

    while (!glfwWindowShouldClose(window)) {
        poster.drain();
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("Xprobe PC Demo", nullptr,
                     ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
                         | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_MenuBar);

        if (ImGui::BeginMenuBar()) {
#if defined(XPROBE_USE_THUNDER)
            ImGui::Text("Thunder Demo (real SDK)");
#else
            ImGui::Text("Thunder Demo (simulator)");
#endif
            ImGui::SameLine();
            ImGui::TextDisabled("| RPC :9000");
            ImGui::EndMenuBar();
        }

        float panelH = (io.DisplaySize.y - 120.0f) * 0.35f;
        if (ImGui::BeginTable("video", 2, ImGuiTableFlags_SizingStretchSame)) {
            ImGui::TableNextColumn();
            drawVideoPlaceholder("Local Video", ImVec4(0.15f, 0.35f, 0.55f, 1.0f), panelH);
            ImGui::TableNextColumn();
            drawVideoPlaceholder("Remote Video", ImVec4(0.35f, 0.15f, 0.45f, 1.0f), panelH);
            ImGui::EndTable();
        }

        ImGui::Separator();
        ImGui::Text("Engine / Channel");
        ImGui::InputText("appId", state.appId, sizeof(state.appId));
        ImGui::SameLine();
        ImGui::InputText("sceneId", state.sceneId, sizeof(state.sceneId));
        ImGui::InputText("room", state.room, sizeof(state.room));
        ImGui::SameLine();
        ImGui::InputText("uid", state.uid, sizeof(state.uid));

        if (ImGui::Button("Init SDK")) {
            long scene = std::strtol(state.sceneId, nullptr, 10);
            long cost = rtc.initialize(state.appId, scene);
            appendLog(state, std::string("createEngine cost=") + std::to_string(cost) + "ms");
        }
        ImGui::SameLine();
        if (ImGui::Button("Destroy SDK")) {
            rtc.deInitialize();
            resetToggles(state);
            appendLog(state, "destroyEngine");
        }
        ImGui::SameLine();
        if (ImGui::Button("Join Room")) {
            int ret = rtc.joinRoom(state.room, state.uid);
            appendLog(state, "joinRoom ret=" + std::to_string(ret));
        }
        ImGui::SameLine();
        if (ImGui::Button("Leave Room")) {
            int ret = rtc.leaveRoom();
            resetToggles(state);
            appendLog(state, "leaveRoom ret=" + std::to_string(ret));
        }

        const char* cameraLabel = state.cameraOn ? "Camera OFF" : "Camera ON";
        if (ImGui::Button(cameraLabel)) {
            state.cameraOn = !state.cameraOn;
            int ret = state.cameraOn ? rtc.startLocalPreview() : rtc.stopLocalPreview();
            if (ret != 0) {
                state.cameraOn = false;
            }
        }
        ImGui::SameLine();
        const char* audioLabel = state.subAudioOn ? "Sub Audio OFF" : "Sub Audio ON";
        if (ImGui::Button(audioLabel)) {
            toggleSubscribe(state, true);
        }
        ImGui::SameLine();
        const char* videoLabel = state.subVideoOn ? "Sub Video OFF" : "Sub Video ON";
        if (ImGui::Button(videoLabel)) {
            toggleSubscribe(state, false);
        }

        ImGui::Separator();
        ImGui::Text("Log");
        if (ImGui::BeginChild("log", ImVec2(0, 0), true)) {
            std::lock_guard<std::mutex> lock(state.logMtx);
            for (const auto& line : state.logs) {
                ImGui::TextUnformatted(line.c_str());
            }
            if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
                ImGui::SetScrollHereY(1.0f);
            }
        }
        ImGui::EndChild();

        ImGui::End();

        ImGui::Render();
        int displayW = 0;
        int displayH = 0;
        glfwGetFramebufferSize(window, &displayW, &displayH);
        glViewport(0, 0, displayW, displayH);
        glClearColor(0.1f, 0.1f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    if (mgr != nullptr) {
        mgr->setMainThreadPoster(nullptr);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return true;
}
