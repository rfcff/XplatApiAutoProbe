#include "rtc/RtcManager.h"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "xprobe/AutoTestMgr.h"

#if defined(XPROBE_USE_THUNDER)
// windows.h 已在 RtcManager.h 中先于 IThunderEngine.h 引入
#include <objbase.h>
using namespace Thunder;

namespace {

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
    // 正常路径应在关窗时已 shutdown；此处兜底，避免 atexit 时仍持有引擎
    shutdown();
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

bool RtcManager::ensureThunderReady() {
    // 已链接 third_party/.../thunderbolt.lib：由加载器解析 thunderbolt.dll（exe 同目录）。
    return true;
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
        if (!ensureThunderReady()) {
            return -1;
        }
    }

    // UI 线程使用 STA，与 ThunderBoltDemo / MFC 一致（采集与 destroy 更稳）
    ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    auto begin = std::chrono::steady_clock::now();
    IThunderEngine* engine = ::createEngine();
    if (engine == nullptr) {
        log("createEngine() 返回 nullptr");
        return -1;
    }

    // 对齐 ThunderBoltDemo：initialize 前设置日志目录（需与 DLL 匹配的头文件虚表）
    engine->setLogFilePath(".");

    int ret = engine->initialize(appId.c_str(), static_cast<int>(sceneId), this);
    if (ret != 0) {
        engine->destroyEngine();
        log("initialize 失败 ret=" + std::to_string(ret));
        return -1;
    }
    engine->setAudioConfig(AUDIO_PROFILE_MUSIC_STANDARD_PR,
                           COMMUT_MODE_DEFAULT,
                           SCENARIO_MODE_DEFAULT);
    // 确保音视频模式（非 only-audio），否则 startVideoPreview 会直接失败
    engine->setMediaMode(PROFILE_NORMAL);
    engine->setRoomMode(ROOM_CONFIG_LIVE);

    auto cost = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - begin)
                    .count();
    {
        std::lock_guard<std::mutex> lock(mtx_);
        engine_ = engine;
        logUnlocked("createEngine 耗时 " + std::to_string(cost) + "ms, appId=" + appId
                    + ", sceneId=" + std::to_string(sceneId));
    }
    return static_cast<long>(cost);
}

void RtcManager::deInitialize() {
    shutdown();
}

void RtcManager::shutdown() {
    // 对齐 ThunderBoltDemo：只调 destroyEngine()。
    // 预览须由调用方先 stopLocalPreview；此处不再 stopVideoPreview，避免多余同步等待。
    IThunderEngine* engine = nullptr;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        engine = engine_;
        engine_ = nullptr;
        previewOn_ = false;
        roomName_.clear();
        uid_.clear();
        remoteUid_.clear();
        selectedCameraIndex_ = -1;
        localHwnd_ = nullptr;
        remoteHwnd_ = nullptr;
    }
    if (engine == nullptr) {
        destroying_.store(false);
        return;
    }

    destroying_.store(true);
    engine->destroyEngine();
    destroying_.store(false);
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
    int preferredIndex = -1;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        if (engine_ == nullptr) {
            return -1;
        }
        engine = engine_;
        hwnd = static_cast<HWND>(localHwnd_);
        localUid = uid_;
        preferredIndex = selectedCameraIndex_;
    }
    if (hwnd == nullptr || !::IsWindow(hwnd)) {
        log("startLocalPreview 失败: 本地预览 HWND 无效（UI 未就绪）");
        return -1;
    }

    // enableLocalVideoCapture 依赖内部 m_videoDeviceIdx；未 startVideoDeviceCapture
    // 前该值为 -1，会把 deviceId 编成 4294967295 导致采集层崩溃。必须先枚举并 start。
    IVideoDeviceManager* videoMgr = engine->getVideoDeviceMgr();
    if (videoMgr == nullptr) {
        log("startLocalPreview 失败: getVideoDeviceMgr() 返回空");
        return -1;
    }

    VideoDeviceList devices{};
    const int deviceCount = videoMgr->enumVideoDevices(devices);
    if (deviceCount <= 0 || devices.count <= 0) {
        log("startLocalPreview 失败: 未枚举到摄像头设备");
        return -1;
    }

    int deviceIdx = devices.device[0].index;
    std::string deviceName = devices.device[0].name;
    if (preferredIndex >= 0) {
        for (int i = 0; i < devices.count; ++i) {
            if (devices.device[i].index == preferredIndex) {
                deviceIdx = preferredIndex;
                deviceName = devices.device[i].name;
                break;
            }
        }
    }
    {
        std::lock_guard<std::mutex> lock(mtx_);
        selectedCameraIndex_ = deviceIdx;
    }
    log(std::string("选用摄像头 index=") + std::to_string(deviceIdx) + " name=" + deviceName);

    VideoCanvas canvas{};
    canvas.hWnd = hwnd;
    canvas.renderMode = VIDEO_RENDER_MODE_ASPECT_FIT;
    // 未进房时 uid_ 可能为空；本地预览仍可绑定 HWND
    copyUid(canvas.uid, sizeof(canvas.uid), localUid);

    int ret = engine->setLocalVideoCanvas(canvas);
    if (ret != 0) {
        log("setLocalVideoCanvas 失败 ret=" + std::to_string(ret));
        return ret;
    }

    ret = videoMgr->startVideoDeviceCapture(deviceIdx);
    if (ret != 0) {
        log("startVideoDeviceCapture 失败 ret=" + std::to_string(ret));
        return ret;
    }

    ret = engine->startVideoPreview();
    {
        std::lock_guard<std::mutex> lock(mtx_);
        if (ret == 0) {
            previewOn_ = true;
        }
    }
    log("startLocalPreview() ret=" + std::to_string(ret));
    return ret;
}

std::vector<CameraDeviceInfo> RtcManager::enumCameras() {
    std::vector<CameraDeviceInfo> out;
    IThunderEngine* engine = nullptr;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        if (engine_ == nullptr) {
            return out;
        }
        engine = engine_;
    }
    IVideoDeviceManager* videoMgr = engine->getVideoDeviceMgr();
    if (videoMgr == nullptr) {
        log("enumCameras 失败: getVideoDeviceMgr() 返回空");
        return out;
    }
    VideoDeviceList devices{};
    videoMgr->enumVideoDevices(devices);
    out.reserve(static_cast<size_t>(devices.count > 0 ? devices.count : 0));
    for (int i = 0; i < devices.count; ++i) {
        CameraDeviceInfo info;
        info.index = devices.device[i].index;
        info.name = devices.device[i].name;
        out.push_back(info);
    }
    log("enumCameras count=" + std::to_string(out.size()));
    return out;
}

void RtcManager::setSelectedCameraIndex(int deviceIndex) {
    std::lock_guard<std::mutex> lock(mtx_);
    selectedCameraIndex_ = deviceIndex;
}

int RtcManager::getSelectedCameraIndex() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return selectedCameraIndex_;
}

int RtcManager::selectCamera(int deviceIndex) {
    bool wasPreview = false;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        selectedCameraIndex_ = deviceIndex;
        wasPreview = previewOn_;
    }
    log("selectCamera index=" + std::to_string(deviceIndex));
    if (!wasPreview) {
        return 0;
    }
    const int stopRet = stopLocalPreview();
    if (stopRet != 0) {
        return stopRet;
    }
    return startLocalPreview();
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
    int retCapture = 0;
    if (IVideoDeviceManager* videoMgr = engine->getVideoDeviceMgr()) {
        retCapture = videoMgr->stopVideoDeviceCapture();
    }
    // 解绑 view，避免窗口销毁后再次 start 访问野指针
    VideoCanvas canvas{};
    canvas.hWnd = nullptr;
    canvas.renderMode = VIDEO_RENDER_MODE_ASPECT_FIT;
    engine->setLocalVideoCanvas(canvas);
    log("stopLocalPreview() preview=" + std::to_string(ret)
        + " capture=" + std::to_string(retCapture));
    return (ret != 0) ? ret : retCapture;
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
    if (destroying_.load()) {
        return;
    }
    std::string msg = std::string("onJoinRoomSuccess: room=") + (roomId ? roomId : "")
                      + " uid=" + (uid ? uid : "") + " elapsed=" + std::to_string(elapsed);
    log(msg);
    fireCallback("onJoinRoomSuccess", msg);
}

void RtcManager::onLeaveRoom() {
    if (destroying_.load()) {
        return;
    }
    std::string msg = "onLeaveRoom: status=0";
    log(msg);
    fireCallback("onLeaveRoom", msg);
}

void RtcManager::onUserJoined(const char* uid, int elapsed) {
    if (destroying_.load()) {
        return;
    }
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
    if (destroying_.load()) {
        return;
    }
    std::string msg = "onConnectionStatus: status=" + std::to_string(static_cast<int>(status));
    log(msg);
    fireCallback("onConnectionStatus", msg);
}

void RtcManager::onRemoteVideoPlay(const char* uid, int width, int height, int elapsed) {
    if (destroying_.load()) {
        return;
    }
    std::string msg = std::string("onRemoteVideoPlay: uid=") + (uid ? uid : "") + " "
                      + std::to_string(width) + "x" + std::to_string(height)
                      + " elapsed=" + std::to_string(elapsed);
    log(msg);
    fireCallback("onRemoteVideoPlay", msg);
}

void RtcManager::onRemoteAudioPlay(const char* uid, int elapsed) {
    if (destroying_.load()) {
        return;
    }
    std::string msg = std::string("onRemoteAudioPlay: uid=") + (uid ? uid : "")
                      + " elapsed=" + std::to_string(elapsed);
    log(msg);
    fireCallback("onRemoteAudioPlay", msg);
}

void RtcManager::onRemoteAudioStopped(const char* uid, bool stop) {
    if (destroying_.load()) {
        return;
    }
    std::string msg = std::string("onRemoteAudioStopped: uid=") + (uid ? uid : "")
                      + " stopped=" + (stop ? "true" : "false");
    log(msg);
    fireCallback("onRemoteAudioStopped", msg);
}

void RtcManager::onRemoteVideoStopped(const char* uid, bool stop) {
    if (destroying_.load()) {
        return;
    }
    std::string msg = std::string("onRemoteVideoStopped: uid=") + (uid ? uid : "")
                      + " stopped=" + (stop ? "true" : "false");
    log(msg);
    fireCallback("onRemoteVideoStopped", msg);
}

void RtcManager::onSdkAuthResult(AUTH_RESULT result) {
    if (destroying_.load()) {
        return;
    }
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
    return static_cast<long>(cost);
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
        selectedCameraIndex_ = -1;
    }
    log("destroyEngine 完成");
}

void RtcManager::shutdown() {
    deInitialize();
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

std::vector<CameraDeviceInfo> RtcManager::enumCameras() {
    std::lock_guard<std::mutex> lock(mtx_);
    if (!initialized_) {
        return {};
    }
    // 模拟器：返回一台虚拟摄像头，便于 UI / RPC 联调
    CameraDeviceInfo fake;
    fake.index = 0;
    fake.name = "Simulated Camera";
    logUnlocked("enumCameras count=1 (simulator)");
    return {fake};
}

void RtcManager::setSelectedCameraIndex(int deviceIndex) {
    std::lock_guard<std::mutex> lock(mtx_);
    selectedCameraIndex_ = deviceIndex;
}

int RtcManager::getSelectedCameraIndex() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return selectedCameraIndex_;
}

int RtcManager::selectCamera(int deviceIndex) {
    std::lock_guard<std::mutex> lock(mtx_);
    if (!initialized_) {
        return -1;
    }
    selectedCameraIndex_ = deviceIndex;
    logUnlocked("selectCamera index=" + std::to_string(deviceIndex));
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
