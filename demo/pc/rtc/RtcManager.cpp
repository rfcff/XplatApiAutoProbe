#include "rtc/RtcManager.h"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <thread>

#include "xprobe/AutoTestMgr.h"

#if defined(XPROBE_USE_THUNDER)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

using namespace Thunder;

namespace {

using CreateEngineFn = IThunderEngine* (*)();

HMODULE gThunderModule = nullptr;
CreateEngineFn gCreateEngine = nullptr;

void copyUid(char* dest, size_t destLen, const std::string& uid) {
    if (destLen == 0) {
        return;
    }
    std::memset(dest, 0, destLen);
    if (uid.empty()) {
        return;
    }
    std::strncpy(dest, uid.c_str(), destLen - 1);
}

} // namespace
#endif

RtcManager& RtcManager::getInstance() {
    static RtcManager instance;
    return instance;
}

RtcManager::RtcManager() = default;

#if defined(XPROBE_USE_THUNDER)
RtcManager::~RtcManager() {
    deInitialize();
    unloadThunder();
}
#endif

void RtcManager::setAutoTestMgr(xprobe::AutoTestMgr* mgr) {
    std::lock_guard<std::mutex> lock(mtx_);
    mgr_ = mgr;
}

void RtcManager::setUiLogger(UiLogger logger) {
    std::lock_guard<std::mutex> lock(mtx_);
    uiLogger_ = std::move(logger);
}

#if defined(XPROBE_USE_THUNDER)

bool RtcManager::ensureThunderLoaded() {
    if (gCreateEngine != nullptr) {
        return true;
    }
    const char* dllPath = std::getenv("XPROBE_THUNDER_DLL");
    if (dllPath == nullptr || dllPath[0] == '\0') {
        dllPath = "thunderbolt.dll";
    }
    gThunderModule = ::LoadLibraryA(dllPath);
    if (gThunderModule == nullptr) {
        logUnlocked(std::string("LoadLibrary 失败: ") + dllPath
                    + " err=" + std::to_string(static_cast<unsigned long>(::GetLastError())));
        return false;
    }
    gCreateEngine = reinterpret_cast<CreateEngineFn>(
        ::GetProcAddress(gThunderModule, "createEngine"));
    if (gCreateEngine == nullptr) {
        logUnlocked("GetProcAddress(createEngine) 失败");
        unloadThunder();
        return false;
    }
    logUnlocked(std::string("已加载 Thunder DLL: ") + dllPath);
    return true;
}

void RtcManager::unloadThunder() {
    gCreateEngine = nullptr;
    if (gThunderModule != nullptr) {
        ::FreeLibrary(gThunderModule);
        gThunderModule = nullptr;
    }
}

void RtcManager::setVideoHwnds(void* localHwnd, void* remoteHwnd) {
    std::lock_guard<std::mutex> lock(mtx_);
    localHwnd_ = localHwnd;
    remoteHwnd_ = remoteHwnd;
}

IThunderEngine* RtcManager::getEngine() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return engine_;
}

long RtcManager::initialize(const std::string& appId, long sceneId) {
    {
        std::lock_guard<std::mutex> lock(mtx_);
        if (engine_ != nullptr) {
            return 0;
        }
        if (!ensureThunderLoaded()) {
            return -1;
        }
    }

    auto begin = std::chrono::steady_clock::now();
    IThunderEngine* engine = gCreateEngine();
    if (engine == nullptr) {
        log("createEngine() 返回 nullptr");
        return -1;
    }
    int ret = engine->initialize(appId.c_str(), static_cast<int>(sceneId), this);
    if (ret != 0) {
        engine->destroyEngine();
        log("initialize 失败 ret=" + std::to_string(ret));
        return -1;
    }
    engine->setAudioConfig(AUDIO_PROFILE_MUSIC_STANDARD_PR,
                           COMMUT_MODE_DEFAULT,
                           SCENARIO_MODE_DEFAULT);

    auto cost = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - begin)
                    .count();
    {
        std::lock_guard<std::mutex> lock(mtx_);
        engine_ = engine;
        logUnlocked("createEngine 耗时 " + std::to_string(cost) + "ms, appId=" + appId
                    + ", sceneId=" + std::to_string(sceneId));
    }
    return cost;
}

void RtcManager::deInitialize() {
    IThunderEngine* engine = nullptr;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        engine = engine_;
        engine_ = nullptr;
        roomName_.clear();
        uid_.clear();
        remoteUid_.clear();
        previewOn_ = false;
    }
    if (engine != nullptr) {
        engine->destroyEngine();
        log("destroyEngine 完成");
    }
}

int RtcManager::joinRoom(const std::string& roomName, const std::string& uid) {
    return joinRoom(roomName, uid, std::string());
}

int RtcManager::joinRoom(const std::string& roomName,
                         const std::string& uid,
                         const std::string& token) {
    IThunderEngine* engine = nullptr;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        if (engine_ == nullptr) {
            return -1;
        }
        roomName_ = roomName;
        uid_ = uid;
        engine = engine_;
    }
    const char* tok = token.empty() ? nullptr : token.c_str();
    int tokLen = token.empty() ? 0 : static_cast<int>(token.size());
    int ret = engine->joinRoom(tok, tokLen, roomName.c_str(), uid.c_str());
    log("joinRoom(" + roomName + ", " + uid + ") ret=" + std::to_string(ret));
    return ret;
}

int RtcManager::leaveRoom() {
    IThunderEngine* engine = nullptr;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        if (engine_ == nullptr) {
            return -1;
        }
        engine = engine_;
        remoteUid_.clear();
        previewOn_ = false;
    }
    int ret = engine->leaveRoom();
    log("leaveRoom() ret=" + std::to_string(ret));
    return ret;
}

int RtcManager::addSubscribe(const std::string& channelId, const std::string& uid) {
    IThunderEngine* engine = nullptr;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        if (engine_ == nullptr) {
            return -1;
        }
        engine = engine_;
    }
    int ret = engine->addSubscribe(channelId.c_str(), uid.c_str());
    log("addSubscribe(" + channelId + ", " + uid + ") ret=" + std::to_string(ret));
    return ret;
}

int RtcManager::removeSubscribe(const std::string& channelId, const std::string& uid) {
    IThunderEngine* engine = nullptr;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        if (engine_ == nullptr) {
            return -1;
        }
        engine = engine_;
    }
    int ret = engine->removeSubscribe(channelId.c_str(), uid.c_str());
    log("removeSubscribe(" + channelId + ", " + uid + ") ret=" + std::to_string(ret));
    return ret;
}

int RtcManager::startLocalPreview() {
    IThunderEngine* engine = nullptr;
    HWND hwnd = nullptr;
    std::string localUid;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        if (engine_ == nullptr) {
            return -1;
        }
        engine = engine_;
        hwnd = static_cast<HWND>(localHwnd_);
        localUid = uid_;
    }
    VideoCanvas canvas{};
    canvas.hWnd = hwnd;
    canvas.renderMode = VIDEO_RENDER_MODE_ASPECT_FIT;
    copyUid(canvas.uid, sizeof(canvas.uid), localUid);
    int ret = engine->setLocalVideoCanvas(canvas);
    if (ret == 0) {
        ret = engine->enableLocalVideoCapture(true);
        if (ret == 0) {
            ret = engine->startVideoPreview();
        }
    }
    {
        std::lock_guard<std::mutex> lock(mtx_);
        if (ret == 0) {
            previewOn_ = true;
        }
    }
    log("startLocalPreview() ret=" + std::to_string(ret));
    return ret;
}

int RtcManager::stopLocalPreview() {
    IThunderEngine* engine = nullptr;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        if (engine_ == nullptr) {
            return -1;
        }
        engine = engine_;
        previewOn_ = false;
    }
    int ret = engine->stopVideoPreview();
    log("stopLocalPreview() ret=" + std::to_string(ret));
    return ret;
}

int RtcManager::setupRemoteVideo(const std::string& uid) {
    IThunderEngine* engine = nullptr;
    HWND hwnd = nullptr;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        if (engine_ == nullptr) {
            return -1;
        }
        engine = engine_;
        hwnd = static_cast<HWND>(remoteHwnd_);
    }
    VideoCanvas canvas{};
    canvas.hWnd = hwnd;
    canvas.renderMode = VIDEO_RENDER_MODE_ASPECT_FIT;
    copyUid(canvas.uid, sizeof(canvas.uid), uid);
    int ret = engine->setRemoteVideoCanvas(canvas);
    log("setRemoteVideoCanvas(" + uid + ") ret=" + std::to_string(ret));
    return ret;
}

void RtcManager::onJoinRoomSuccess(const char* roomId, const char* uid, int elapsed) {
    std::string msg = std::string("onJoinRoomSuccess: room=") + (roomId ? roomId : "")
                      + " uid=" + (uid ? uid : "") + " elapsed=" + std::to_string(elapsed);
    log(msg);
    fireCallback("onJoinRoomSuccess", msg);
}

void RtcManager::onLeaveRoom() {
    std::string msg = "onLeaveRoom: status=0";
    log(msg);
    fireCallback("onLeaveRoom", msg);
}

void RtcManager::onUserJoined(const char* uid, int elapsed) {
    {
        std::lock_guard<std::mutex> lock(mtx_);
        remoteUid_ = uid ? uid : "";
    }
    std::string msg = std::string("onUserJoined: uid=") + (uid ? uid : "")
                      + " elapsed=" + std::to_string(elapsed);
    log(msg);
    fireCallback("onUserJoined", msg);
}

void RtcManager::onConnectionStatus(ThunderConnectionStatus status) {
    std::string msg = "onConnectionStatus: status=" + std::to_string(static_cast<int>(status));
    log(msg);
    fireCallback("onConnectionStatus", msg);
}

void RtcManager::onRemoteVideoPlay(const char* uid, int width, int height, int elapsed) {
    std::string msg = std::string("onRemoteVideoPlay: uid=") + (uid ? uid : "") + " "
                      + std::to_string(width) + "x" + std::to_string(height)
                      + " elapsed=" + std::to_string(elapsed);
    log(msg);
    fireCallback("onRemoteVideoPlay", msg);
}

void RtcManager::onRemoteAudioPlay(const char* uid, int elapsed) {
    std::string msg = std::string("onRemoteAudioPlay: uid=") + (uid ? uid : "")
                      + " elapsed=" + std::to_string(elapsed);
    log(msg);
    fireCallback("onRemoteAudioPlay", msg);
}

void RtcManager::onRemoteAudioStopped(const char* uid, bool stop) {
    std::string msg = std::string("onRemoteAudioStopped: uid=") + (uid ? uid : "")
                      + " stopped=" + (stop ? "true" : "false");
    log(msg);
    fireCallback("onRemoteAudioStopped", msg);
}

void RtcManager::onRemoteVideoStopped(const char* uid, bool stop) {
    std::string msg = std::string("onRemoteVideoStopped: uid=") + (uid ? uid : "")
                      + " stopped=" + (stop ? "true" : "false");
    log(msg);
    fireCallback("onRemoteVideoStopped", msg);
}

void RtcManager::onSdkAuthResult(AUTH_RESULT result) {
    std::string msg = "sdkAuthResult: auth=" + std::to_string(static_cast<int>(result));
    log(msg);
    fireCallback("sdkAuthResult", msg);
}

#else // !XPROBE_USE_THUNDER — simulator

long RtcManager::initialize(const std::string& appId, long sceneId) {
    {
        std::lock_guard<std::mutex> lock(mtx_);
        if (initialized_) {
            return 0;
        }
    }
    auto begin = std::chrono::steady_clock::now();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    auto cost = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - begin)
                    .count();
    {
        std::lock_guard<std::mutex> lock(mtx_);
        initialized_ = true;
        logUnlocked("createEngine 耗时 " + std::to_string(cost) + "ms, appId=" + appId
                    + ", sceneId=" + std::to_string(sceneId));
    }
    return cost;
}

void RtcManager::deInitialize() {
    {
        std::lock_guard<std::mutex> lock(mtx_);
        if (!initialized_) {
            return;
        }
        initialized_ = false;
        roomName_.clear();
        uid_.clear();
        remoteUid_.clear();
        previewOn_ = false;
    }
    log("destroyEngine 完成");
}

int RtcManager::joinRoom(const std::string& roomName, const std::string& uid) {
    return joinRoom(roomName, uid, std::string());
}

int RtcManager::joinRoom(const std::string& roomName,
                         const std::string& uid,
                         const std::string& /*token*/) {
    {
        std::lock_guard<std::mutex> lock(mtx_);
        if (!initialized_) {
            return -1;
        }
        roomName_ = roomName;
        uid_ = uid;
    }
    log("joinRoom(" + roomName + ", " + uid + ") ret=0");

    scheduleCallback("onJoinRoomSuccess",
                     "onJoinRoomSuccess: room=" + roomName + " uid=" + uid + " elapsed=200",
                     200);
    scheduleCallback("onConnectionStatus", "onConnectionStatus: status=0", 200);
    scheduleCallback("onUserJoined",
                     std::string("onUserJoined: uid=") + kSimulatedRemoteUid + " elapsed=350",
                     350);

    {
        std::lock_guard<std::mutex> lock(mtx_);
        remoteUid_ = kSimulatedRemoteUid;
    }
    return 0;
}

int RtcManager::leaveRoom() {
    {
        std::lock_guard<std::mutex> lock(mtx_);
        if (!initialized_) {
            return -1;
        }
        remoteUid_.clear();
        previewOn_ = false;
    }
    log("leaveRoom() ret=0");
    fireCallback("onLeaveRoom", "onLeaveRoom: status=0");
    return 0;
}

int RtcManager::addSubscribe(const std::string& channelId, const std::string& uid) {
    std::lock_guard<std::mutex> lock(mtx_);
    if (!initialized_) {
        return -1;
    }
    logUnlocked("addSubscribe(" + channelId + ", " + uid + ") ret=0");
    fireCallbackUnlocked("onRemoteAudioPlay",
                         "onRemoteAudioPlay: uid=" + uid + " elapsed=50");
    fireCallbackUnlocked("onRemoteVideoPlay",
                         "onRemoteVideoPlay: uid=" + uid + " 640x480 elapsed=80");
    return 0;
}

int RtcManager::removeSubscribe(const std::string& channelId, const std::string& uid) {
    std::lock_guard<std::mutex> lock(mtx_);
    if (!initialized_) {
        return -1;
    }
    logUnlocked("removeSubscribe(" + channelId + ", " + uid + ") ret=0");
    fireCallbackUnlocked("onRemoteAudioStopped",
                         "onRemoteAudioStopped: uid=" + uid + " stopped=true");
    fireCallbackUnlocked("onRemoteVideoStopped",
                         "onRemoteVideoStopped: uid=" + uid + " stopped=true");
    return 0;
}

int RtcManager::startLocalPreview() {
    std::lock_guard<std::mutex> lock(mtx_);
    if (!initialized_) {
        return -1;
    }
    previewOn_ = true;
    logUnlocked("startLocalPreview() ret=0");
    return 0;
}

int RtcManager::stopLocalPreview() {
    std::lock_guard<std::mutex> lock(mtx_);
    if (!initialized_) {
        return -1;
    }
    previewOn_ = false;
    logUnlocked("stopLocalPreview() ret=0");
    return 0;
}

int RtcManager::setupRemoteVideo(const std::string& uid) {
    std::lock_guard<std::mutex> lock(mtx_);
    if (!initialized_) {
        return -1;
    }
    logUnlocked("setRemoteVideoCanvas(" + uid + ") ret=0");
    return 0;
}

#endif // XPROBE_USE_THUNDER

bool RtcManager::isInitialized() const {
    std::lock_guard<std::mutex> lock(mtx_);
#if defined(XPROBE_USE_THUNDER)
    return engine_ != nullptr;
#else
    return initialized_;
#endif
}

std::string RtcManager::getRoomName() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return roomName_;
}

std::string RtcManager::getUid() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return uid_;
}

std::string RtcManager::getRemoteUid() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return remoteUid_;
}

bool RtcManager::isPreviewOn() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return previewOn_;
}

std::string RtcManager::safe(const std::string& s, const std::string& def) {
    return s.empty() ? def : s;
}

void RtcManager::log(const std::string& msg) {
    UiLogger logger;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        logger = uiLogger_;
    }
    if (logger) {
        logger(msg);
    }
}

void RtcManager::logUnlocked(const std::string& msg) {
    if (uiLogger_) {
        uiLogger_(msg);
    }
}

void RtcManager::fireCallbackUnlocked(const std::string& name, const std::string& info) {
    if (mgr_ != nullptr) {
        mgr_->sendCallback(name, info);
    }
}

void RtcManager::fireCallback(const std::string& name, const std::string& info) {
    xprobe::AutoTestMgr* mgr = nullptr;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        mgr = mgr_;
    }
    if (mgr != nullptr) {
        mgr->sendCallback(name, info);
    }
}

void RtcManager::scheduleCallback(const std::string& name,
                                  const std::string& info,
                                  int delayMs) {
    xprobe::AutoTestMgr* mgr = nullptr;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        mgr = mgr_;
    }
    if (mgr == nullptr) {
        return;
    }
    std::thread([mgr, name, info, delayMs]() {
        if (delayMs > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
        }
        mgr->sendCallback(name, info);
    }).detach();
}
