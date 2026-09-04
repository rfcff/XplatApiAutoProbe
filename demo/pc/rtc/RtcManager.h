#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace xprobe {
class AutoTestMgr;
}

#if defined(XPROBE_USE_THUNDER)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
// Thunder 头文件中虚函数默认实现有大量未使用参数
#pragma warning(push)
#pragma warning(disable : 4100)
#include "IThunderEngine.h"
#pragma warning(pop)
#endif

/** 摄像头设备描述（对齐 ThunderBoltDemo enumVideoDevices） */
struct CameraDeviceInfo {
    int index = -1;
    std::string name;
};

// Thunder RTC 极简封装：与 Android / iOS demo 的 RtcManager 公共面对齐。
// 默认模拟器（无 Thunder 二进制）；编译定义 XPROBE_USE_THUNDER 时接 Windows Thunder SDK。
class RtcManager
#if defined(XPROBE_USE_THUNDER)
    : public Thunder::IThunderEventHandler
#endif
{
public:
    using UiLogger = std::function<void(const std::string& msg)>;

    static RtcManager& getInstance();

    void setAutoTestMgr(xprobe::AutoTestMgr* mgr);
    void setUiLogger(UiLogger logger);

    long initialize(const std::string& appId, long sceneId);
    void deInitialize();
    /** 退出前安全收尾：停预览、退房、解绑 HWND、destroyEngine（可重复调用） */
    void shutdown();

    int joinRoom(const std::string& roomName, const std::string& uid);
    /** token 可空：模拟器忽略；真实 Thunder 空 token 表示无鉴权进房 */
    int joinRoom(const std::string& roomName, const std::string& uid, const std::string& token);
    int leaveRoom();

    int addSubscribe(const std::string& channelId, const std::string& uid);
    int removeSubscribe(const std::string& channelId, const std::string& uid);

    int startLocalPreview();
    int stopLocalPreview();
    int setupRemoteVideo(const std::string& uid);

    /** 枚举摄像头（需已 initialize；对齐 ThunderBoltDemo OnBnClickedBtnEnumvdev） */
    std::vector<CameraDeviceInfo> enumCameras();
    /** 选择摄像头 index（来自 CameraDeviceInfo::index）；-1 表示自动选第一台 */
    void setSelectedCameraIndex(int deviceIndex);
    int getSelectedCameraIndex() const;
    /**
     * 切换到指定摄像头；若正在预览则停采后按新设备重启。
     * @return 0 成功；预览未开时仅记录选择也返回 0
     */
    int selectCamera(int deviceIndex);

    bool isInitialized() const;
    std::string getRoomName() const;
    std::string getUid() const;
    std::string getRemoteUid() const;
    bool isPreviewOn() const;

#if defined(XPROBE_USE_THUNDER)
    /** 本地/远端渲染 HWND（可为 nullptr；headless 可不设） */
    void setVideoHwnds(void* localHwnd, void* remoteHwnd);
    Thunder::IThunderEngine* getEngine() const;
#endif

    static std::string safe(const std::string& s, const std::string& def);

#if defined(XPROBE_USE_THUNDER)
    void onJoinRoomSuccess(const char* roomId, const char* uid, int elapsed) override;
    void onLeaveRoom() override;
    void onUserJoined(const char* uid, int elapsed) override;
    void onConnectionStatus(Thunder::ThunderConnectionStatus status) override;
    void onRemoteVideoPlay(const char* uid, int width, int height, int elapsed) override;
    void onRemoteAudioPlay(const char* uid, int elapsed) override;
    void onRemoteAudioStopped(const char* uid, bool stop) override;
    void onRemoteVideoStopped(const char* uid, bool stop) override;
    void onSdkAuthResult(Thunder::AUTH_RESULT result) override;
#endif

private:
    RtcManager();
#if defined(XPROBE_USE_THUNDER)
    ~RtcManager() override;
#else
    ~RtcManager() = default;
#endif

    RtcManager(const RtcManager&) = delete;
    RtcManager& operator=(const RtcManager&) = delete;

    void log(const std::string& msg);
    void logUnlocked(const std::string& msg);
    void fireCallbackUnlocked(const std::string& name, const std::string& info);
    void fireCallback(const std::string& name, const std::string& info);
    void scheduleCallback(const std::string& name, const std::string& info, int delayMs);

#if defined(XPROBE_USE_THUNDER)
    bool ensureThunderReady();
#endif

    /** destroyEngine 过程中置位，回调一律空操作，避免 SDK 清理时重入崩溃 */
    std::atomic<bool> destroying_{false};

    xprobe::AutoTestMgr* mgr_ = nullptr;
    UiLogger uiLogger_;

    mutable std::mutex mtx_;
#if defined(XPROBE_USE_THUNDER)
    Thunder::IThunderEngine* engine_ = nullptr;
#else
    bool initialized_ = false;
#endif
    std::string roomName_;
    std::string uid_;
    std::string remoteUid_;
    bool previewOn_ = false;
    /** 用户选择的摄像头 index；-1=尚未选择，开预览时取枚举列表第一台 */
    int selectedCameraIndex_ = -1;

#if defined(XPROBE_USE_THUNDER)
    void* localHwnd_ = nullptr;
    void* remoteHwnd_ = nullptr;
#endif

    static constexpr const char* kSimulatedRemoteUid = "987654321";
};
