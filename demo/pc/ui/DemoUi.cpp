// =====================================================================
// demo/pc/ui/DemoUi.cpp
// 纯 Win32 UI（无 ImGui / GLFW）：布局对齐 Android MainActivity。
// =====================================================================
#include "ui/DemoUi.h"

#ifndef _WIN32
#error "demo/pc UI 仅支持 Windows（Win32）"
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <commctrl.h>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "rtc/RtcManager.h"
#include "xprobe/AutoTestMgr.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

namespace {

constexpr int kMaxLogLines = 300;
constexpr UINT_PTR kTimerId = 1;
constexpr UINT kMsgDrain = WM_APP + 1;
constexpr wchar_t kVideoHostClass[] = L"XprobeVideoHost";

enum CtrlId : int {
    IDC_STATUS = 1001,
    IDC_LOCAL_VIDEO = 1002,
    IDC_REMOTE_VIDEO = 1003,
    IDC_APPID = 1010,
    IDC_SCENEID = 1011,
    IDC_ROOM = 1012,
    IDC_UID = 1013,
    IDC_LBL_APPID = 1014,
    IDC_LBL_SCENEID = 1015,
    IDC_LBL_ROOM = 1016,
    IDC_LBL_UID = 1017,
    IDC_BTN_INIT = 1020,
    IDC_BTN_DESTROY = 1021,
    IDC_BTN_JOIN = 1022,
    IDC_BTN_LEAVE = 1023,
    IDC_BTN_CAMERA = 1024,
    IDC_BTN_AUDIO = 1025,
    IDC_BTN_VIDEO = 1026,
    IDC_LBL_CAMERA = 1027,
    IDC_CAMERA_COMBO = 1028,
    IDC_BTN_ENUM_CAMERA = 1029,
    IDC_LBL_LOG = 1030,
    IDC_LOG = 1031,
};

class UiMainThreadPoster : public xprobe::IMainThreadPoster {
public:
    explicit UiMainThreadPoster(HWND hwnd) : hwnd_(hwnd) {}

    void post(std::function<void()> task) override {
        if (!task) {
            return;
        }
        {
            std::lock_guard<std::mutex> lock(mtx_);
            queue_.push_back(std::move(task));
        }
        if (hwnd_ != nullptr) {
            ::PostMessageW(hwnd_, kMsgDrain, 0, 0);
        }
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

    void setHwnd(HWND hwnd) { hwnd_ = hwnd; }

private:
    HWND hwnd_ = nullptr;
    std::mutex mtx_;
    std::vector<std::function<void()>> queue_;
};

struct UiState {
    bool cameraOn = false;
    bool subAudioOn = false;
    bool subVideoOn = false;
    std::deque<std::string> logs;
    std::mutex logMtx;
    bool logDirty = false;
};

struct DemoUiApp {
    HWND hwnd = nullptr;
    HWND hStatus = nullptr;
    HWND hLocalVideo = nullptr;
    HWND hRemoteVideo = nullptr;
    HWND hAppId = nullptr;
    HWND hSceneId = nullptr;
    HWND hRoom = nullptr;
    HWND hUid = nullptr;
    HWND hBtnInit = nullptr;
    HWND hBtnDestroy = nullptr;
    HWND hBtnJoin = nullptr;
    HWND hBtnLeave = nullptr;
    HWND hBtnCamera = nullptr;
    HWND hBtnAudio = nullptr;
    HWND hBtnVideo = nullptr;
    HWND hCameraCombo = nullptr;
    HWND hBtnEnumCamera = nullptr;
    HWND hLog = nullptr;
    HFONT font = nullptr;
    bool fontOwned = false;
    HBRUSH brBg = nullptr;
    HBRUSH brEdit = nullptr;
    xprobe::AutoTestMgr* mgr = nullptr;
    UiMainThreadPoster* poster = nullptr;
    UiState state;
};

std::wstring utf8ToWide(const std::string& utf8) {
    if (utf8.empty()) {
        return std::wstring();
    }
    const int n = ::MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(),
                                        static_cast<int>(utf8.size()), nullptr, 0);
    if (n <= 0) {
        return std::wstring();
    }
    std::wstring out(static_cast<size_t>(n), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), &out[0], n);
    return out;
}

std::string wideToUtf8(const std::wstring& wide) {
    if (wide.empty()) {
        return std::string();
    }
    const int n = ::WideCharToMultiByte(CP_UTF8, 0, wide.c_str(),
                                        static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    if (n <= 0) {
        return std::string();
    }
    std::string out(static_cast<size_t>(n), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), &out[0], n,
                          nullptr, nullptr);
    return out;
}

std::string getEditUtf8(HWND edit) {
    const int len = ::GetWindowTextLengthW(edit);
    if (len <= 0) {
        return std::string();
    }
    std::wstring buf(static_cast<size_t>(len) + 1, L'\0');
    ::GetWindowTextW(edit, &buf[0], len + 1);
    buf.resize(static_cast<size_t>(len));
    return wideToUtf8(buf);
}

void setWindowTextUtf8(HWND hwnd, const std::string& utf8) {
    const std::wstring w = utf8ToWide(utf8);
    ::SetWindowTextW(hwnd, w.c_str());
}

HWND createCtrl(LPCWSTR cls, LPCWSTR text, DWORD style, int x, int y, int width, int height,
                HWND parent, int id, HFONT font) {
    HWND hwnd = ::CreateWindowExW(0, cls, text, style | WS_CHILD | WS_VISIBLE, x, y, width, height,
                                  parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                  ::GetModuleHandleW(nullptr), nullptr);
    if (hwnd != nullptr && font != nullptr) {
        ::SendMessageW(hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    }
    return hwnd;
}

// Thunder 渲染目标：空白子窗口（勿用带文字的 STATIC，SDK 会往 HWND 上挂渲染）
LRESULT CALLBACK VideoHostWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_ERASEBKGND: {
            RECT rc{};
            ::GetClientRect(hwnd, &rc);
            HBRUSH br = ::CreateSolidBrush(RGB(20, 20, 24));
            ::FillRect(reinterpret_cast<HDC>(wParam), &rc, br);
            ::DeleteObject(br);
            return 1;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC hdc = ::BeginPaint(hwnd, &ps);
            ::EndPaint(hwnd, &ps);
            (void)hdc;
            return 0;
        }
        default:
            break;
    }
    return ::DefWindowProcW(hwnd, msg, wParam, lParam);
}

void ensureVideoHostClass() {
    static bool registered = false;
    if (registered) {
        return;
    }
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = VideoHostWndProc;
    wc.hInstance = ::GetModuleHandleW(nullptr);
    wc.hCursor = ::LoadCursorW(nullptr, reinterpret_cast<LPCWSTR>(IDC_ARROW));
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kVideoHostClass;
    if (::RegisterClassExW(&wc) != 0 || ::GetLastError() == ERROR_CLASS_ALREADY_EXISTS) {
        registered = true;
    }
}

HWND createVideoHost(HWND parent, int id) {
    ensureVideoHostClass();
    return ::CreateWindowExW(
        0, kVideoHostClass, L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
        0, 0, 10, 10, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        ::GetModuleHandleW(nullptr), nullptr);
}

void appendLog(DemoUiApp* app, const std::string& msg) {
    auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tmBuf{};
    localtime_s(&tmBuf, &t);
    char timeBuf[16];
    std::strftime(timeBuf, sizeof(timeBuf), "%H:%M:%S", &tmBuf);

    {
        std::lock_guard<std::mutex> lock(app->state.logMtx);
        app->state.logs.emplace_back(std::string("[") + timeBuf + "] " + msg);
        while (static_cast<int>(app->state.logs.size()) > kMaxLogLines) {
            app->state.logs.pop_front();
        }
        app->state.logDirty = true;
    }
    if (app->hwnd != nullptr) {
        ::PostMessageW(app->hwnd, kMsgDrain, 0, 0);
    }
}

void flushLogToEdit(DemoUiApp* app) {
    std::deque<std::string> snapshot;
    {
        std::lock_guard<std::mutex> lock(app->state.logMtx);
        if (!app->state.logDirty) {
            return;
        }
        snapshot = app->state.logs;
        app->state.logDirty = false;
    }
    std::string all;
    all.reserve(snapshot.size() * 64);
    for (const auto& line : snapshot) {
        all += line;
        all += "\r\n";
    }
    setWindowTextUtf8(app->hLog, all);
    ::SendMessageW(app->hLog, EM_SETSEL, static_cast<WPARAM>(-1), static_cast<LPARAM>(-1));
    ::SendMessageW(app->hLog, EM_SCROLLCARET, 0, 0);
}

void resetToggles(DemoUiApp* app) {
    app->state.cameraOn = false;
    app->state.subAudioOn = false;
    app->state.subVideoOn = false;
    setWindowTextUtf8(app->hBtnCamera, "Camera ON");
    setWindowTextUtf8(app->hBtnAudio, "Sub Audio ON");
    setWindowTextUtf8(app->hBtnVideo, "Sub Video ON");
}

/** 对齐 ThunderBoltDemo OnBnClickedBtnEnumvdev：填充 Combo，ItemData 存 device.index */
void refreshCameraList(DemoUiApp* app) {
    if (app->hCameraCombo == nullptr) {
        return;
    }
    ::SendMessageW(app->hCameraCombo, CB_RESETCONTENT, 0, 0);

    RtcManager& rtc = RtcManager::getInstance();
    if (!rtc.isInitialized()) {
        appendLog(app, "enumCameras: SDK 未初始化");
        return;
    }

    const std::vector<CameraDeviceInfo> devices = rtc.enumCameras();
    const int preferred = rtc.getSelectedCameraIndex();
    int selectPos = 0;
    for (size_t i = 0; i < devices.size(); ++i) {
        const std::wstring wname = utf8ToWide(devices[i].name);
        const LRESULT pos =
            ::SendMessageW(app->hCameraCombo, CB_ADDSTRING, 0,
                           reinterpret_cast<LPARAM>(wname.empty() ? L"(unnamed)" : wname.c_str()));
        if (pos >= 0) {
            ::SendMessageW(app->hCameraCombo, CB_SETITEMDATA, static_cast<WPARAM>(pos),
                           static_cast<LPARAM>(devices[i].index));
            if (preferred >= 0 && devices[i].index == preferred) {
                selectPos = static_cast<int>(pos);
            }
        }
    }
    if (!devices.empty()) {
        ::SendMessageW(app->hCameraCombo, CB_SETCURSEL, static_cast<WPARAM>(selectPos), 0);
        const int deviceIndex = static_cast<int>(
            ::SendMessageW(app->hCameraCombo, CB_GETITEMDATA, static_cast<WPARAM>(selectPos), 0));
        rtc.setSelectedCameraIndex(deviceIndex);
    }
    appendLog(app, "enumCameras count=" + std::to_string(devices.size()));
}

void applySelectedCamera(DemoUiApp* app) {
    if (app->hCameraCombo == nullptr) {
        return;
    }
    const int cur = static_cast<int>(::SendMessageW(app->hCameraCombo, CB_GETCURSEL, 0, 0));
    if (cur < 0) {
        return;
    }
    const int deviceIndex =
        static_cast<int>(::SendMessageW(app->hCameraCombo, CB_GETITEMDATA, static_cast<WPARAM>(cur), 0));
    RtcManager& rtc = RtcManager::getInstance();
    const int ret = rtc.selectCamera(deviceIndex);
    appendLog(app, "selectCamera index=" + std::to_string(deviceIndex) + " ret=" + std::to_string(ret));
    if (ret != 0 && app->state.cameraOn) {
        app->state.cameraOn = rtc.isPreviewOn();
        setWindowTextUtf8(app->hBtnCamera, app->state.cameraOn ? "Camera OFF" : "Camera ON");
    }
}

void toggleSubscribe(DemoUiApp* app, bool audio) {
    RtcManager& rtc = RtcManager::getInstance();
    const std::string roomEdit = getEditUtf8(app->hRoom);
    const std::string room = !roomEdit.empty() ? roomEdit : rtc.getRoomName();
    const std::string remoteUid = rtc.getRemoteUid();
    if (remoteUid.empty()) {
        appendLog(app, "尚未收到远端用户（onUserJoined）");
        return;
    }

    const bool on = audio ? app->state.subAudioOn : app->state.subVideoOn;
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
            app->state.subAudioOn = !app->state.subAudioOn;
            setWindowTextUtf8(app->hBtnAudio,
                              app->state.subAudioOn ? "Sub Audio OFF" : "Sub Audio ON");
        } else {
            app->state.subVideoOn = !app->state.subVideoOn;
            setWindowTextUtf8(app->hBtnVideo,
                              app->state.subVideoOn ? "Sub Video OFF" : "Sub Video ON");
        }
    } else {
        appendLog(app, "订阅操作失败: " + std::to_string(ret));
    }
}

void layoutControls(DemoUiApp* app, int cw, int ch) {
    const int pad = 12;
    const int rowH = 28;
    const int gap = 8;
    int y = pad;

    ::MoveWindow(app->hStatus, pad, y, cw - pad * 2, 22, TRUE);
    y += 28;

    const int videoH = (ch > 460) ? ((ch - 320) * 35 / 100) : 160;
    const int videoW = (cw - pad * 3) / 2;
    ::MoveWindow(app->hLocalVideo, pad, y, videoW, videoH, TRUE);
    ::MoveWindow(app->hRemoteVideo, pad * 2 + videoW, y, videoW, videoH, TRUE);
    y += videoH + gap;

    const int colW = (cw - pad * 3 - 70) / 2;
    HWND hLblApp = ::GetDlgItem(app->hwnd, IDC_LBL_APPID);
    HWND hLblScene = ::GetDlgItem(app->hwnd, IDC_LBL_SCENEID);
    HWND hLblRoom = ::GetDlgItem(app->hwnd, IDC_LBL_ROOM);
    HWND hLblUid = ::GetDlgItem(app->hwnd, IDC_LBL_UID);

    ::MoveWindow(hLblApp, pad, y + 4, 50, 20, TRUE);
    ::MoveWindow(app->hAppId, pad + 52, y, colW - 52, rowH, TRUE);
    ::MoveWindow(hLblScene, pad * 2 + colW, y + 4, 55, 20, TRUE);
    ::MoveWindow(app->hSceneId, pad * 2 + colW + 57, y, colW - 57, rowH, TRUE);
    y += rowH + gap;

    ::MoveWindow(hLblRoom, pad, y + 4, 50, 20, TRUE);
    ::MoveWindow(app->hRoom, pad + 52, y, colW - 52, rowH, TRUE);
    ::MoveWindow(hLblUid, pad * 2 + colW, y + 4, 40, 20, TRUE);
    ::MoveWindow(app->hUid, pad * 2 + colW + 42, y, colW - 42, rowH, TRUE);
    y += rowH + gap;

    HWND hLblCam = ::GetDlgItem(app->hwnd, IDC_LBL_CAMERA);
    const int enumBtnW = 72;
    ::MoveWindow(hLblCam, pad, y + 4, 55, 20, TRUE);
    ::MoveWindow(app->hCameraCombo, pad + 57, y, cw - pad * 3 - 57 - enumBtnW, rowH, TRUE);
    ::MoveWindow(app->hBtnEnumCamera, cw - pad - enumBtnW, y, enumBtnW, rowH, TRUE);
    y += rowH + gap;

    const int btnW = (cw - pad * 5) / 4;
    ::MoveWindow(app->hBtnInit, pad, y, btnW, rowH, TRUE);
    ::MoveWindow(app->hBtnDestroy, pad * 2 + btnW, y, btnW, rowH, TRUE);
    ::MoveWindow(app->hBtnJoin, pad * 3 + btnW * 2, y, btnW, rowH, TRUE);
    ::MoveWindow(app->hBtnLeave, pad * 4 + btnW * 3, y, btnW, rowH, TRUE);
    y += rowH + gap;

    const int btnW3 = (cw - pad * 4) / 3;
    ::MoveWindow(app->hBtnCamera, pad, y, btnW3, rowH, TRUE);
    ::MoveWindow(app->hBtnAudio, pad * 2 + btnW3, y, btnW3, rowH, TRUE);
    ::MoveWindow(app->hBtnVideo, pad * 3 + btnW3 * 2, y, btnW3, rowH, TRUE);
    y += rowH + gap;

    HWND hLblLog = ::GetDlgItem(app->hwnd, IDC_LBL_LOG);
    ::MoveWindow(hLblLog, pad, y, 80, 20, TRUE);
    y += 22;
    ::MoveWindow(app->hLog, pad, y, cw - pad * 2, ch - y - pad, TRUE);
}

void createChildren(DemoUiApp* app) {
    const DWORD editStyle = WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP;
    const DWORD btnStyle = BS_PUSHBUTTON | WS_TABSTOP;
    const DWORD staticStyle = SS_LEFT;

#if defined(XPROBE_USE_THUNDER)
    const wchar_t* status = L"Thunder Demo (real SDK)  |  RPC :9000";
#else
    const wchar_t* status = L"Thunder Demo (simulator)  |  RPC :9000";
#endif
    app->hStatus = createCtrl(L"STATIC", status, staticStyle, 0, 0, 10, 10, app->hwnd, IDC_STATUS,
                              app->font);
    // 专用渲染宿主窗口：Thunder setLocal/RemoteVideoCanvas 需要可挂渲染的 HWND
    app->hLocalVideo = createVideoHost(app->hwnd, IDC_LOCAL_VIDEO);
    app->hRemoteVideo = createVideoHost(app->hwnd, IDC_REMOTE_VIDEO);

    createCtrl(L"STATIC", L"appId", staticStyle, 0, 0, 10, 10, app->hwnd, IDC_LBL_APPID, app->font);
    app->hAppId =
        createCtrl(L"EDIT", L"10034", editStyle, 0, 0, 10, 10, app->hwnd, IDC_APPID, app->font);
    createCtrl(L"STATIC", L"sceneId", staticStyle, 0, 0, 10, 10, app->hwnd, IDC_LBL_SCENEID,
               app->font);
    app->hSceneId =
        createCtrl(L"EDIT", L"0", editStyle, 0, 0, 10, 10, app->hwnd, IDC_SCENEID, app->font);
    createCtrl(L"STATIC", L"room", staticStyle, 0, 0, 10, 10, app->hwnd, IDC_LBL_ROOM, app->font);
    app->hRoom =
        createCtrl(L"EDIT", L"82552971", editStyle, 0, 0, 10, 10, app->hwnd, IDC_ROOM, app->font);
    createCtrl(L"STATIC", L"uid", staticStyle, 0, 0, 10, 10, app->hwnd, IDC_LBL_UID, app->font);
    app->hUid =
        createCtrl(L"EDIT", L"123456789", editStyle, 0, 0, 10, 10, app->hwnd, IDC_UID, app->font);

    createCtrl(L"STATIC", L"Camera", staticStyle, 0, 0, 10, 10, app->hwnd, IDC_LBL_CAMERA,
               app->font);
    app->hCameraCombo =
        createCtrl(L"COMBOBOX", L"", CBS_DROPDOWNLIST | CBS_HASSTRINGS | WS_VSCROLL | WS_TABSTOP,
                   0, 0, 10, 200, app->hwnd, IDC_CAMERA_COMBO, app->font);
    app->hBtnEnumCamera =
        createCtrl(L"BUTTON", L"刷新", btnStyle, 0, 0, 10, 10, app->hwnd, IDC_BTN_ENUM_CAMERA,
                   app->font);

    app->hBtnInit =
        createCtrl(L"BUTTON", L"Init SDK", btnStyle, 0, 0, 10, 10, app->hwnd, IDC_BTN_INIT,
                   app->font);
    app->hBtnDestroy =
        createCtrl(L"BUTTON", L"Destroy SDK", btnStyle, 0, 0, 10, 10, app->hwnd, IDC_BTN_DESTROY,
                   app->font);
    app->hBtnJoin =
        createCtrl(L"BUTTON", L"Join Room", btnStyle, 0, 0, 10, 10, app->hwnd, IDC_BTN_JOIN,
                   app->font);
    app->hBtnLeave =
        createCtrl(L"BUTTON", L"Leave Room", btnStyle, 0, 0, 10, 10, app->hwnd, IDC_BTN_LEAVE,
                   app->font);
    app->hBtnCamera =
        createCtrl(L"BUTTON", L"Camera ON", btnStyle, 0, 0, 10, 10, app->hwnd, IDC_BTN_CAMERA,
                   app->font);
    app->hBtnAudio =
        createCtrl(L"BUTTON", L"Sub Audio ON", btnStyle, 0, 0, 10, 10, app->hwnd, IDC_BTN_AUDIO,
                   app->font);
    app->hBtnVideo =
        createCtrl(L"BUTTON", L"Sub Video ON", btnStyle, 0, 0, 10, 10, app->hwnd, IDC_BTN_VIDEO,
                   app->font);

    createCtrl(L"STATIC", L"Log", staticStyle, 0, 0, 10, 10, app->hwnd, IDC_LBL_LOG, app->font);
    app->hLog = createCtrl(L"EDIT", L"",
                           WS_BORDER | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL
                               | ES_WANTRETURN,
                           0, 0, 10, 10, app->hwnd, IDC_LOG, app->font);
}

void onCommand(DemoUiApp* app, int id, WORD notify) {
    RtcManager& rtc = RtcManager::getInstance();
    if (id == IDC_CAMERA_COMBO && notify == CBN_SELCHANGE) {
        applySelectedCamera(app);
        return;
    }
    if (notify != BN_CLICKED && notify != 0) {
        return;
    }
    switch (id) {
        case IDC_BTN_INIT: {
            const std::string appId = getEditUtf8(app->hAppId);
            const std::string sceneStr = getEditUtf8(app->hSceneId);
            const long scene = std::strtol(sceneStr.c_str(), nullptr, 10);
            const long cost = rtc.initialize(appId, scene);
            appendLog(app, std::string("createEngine cost=") + std::to_string(cost) + "ms");
            // 不自动枚举摄像头；需用户点「刷新」或 RPC enumCameras
            break;
        }
        case IDC_BTN_DESTROY:
            // 先停预览再 destroyEngine，避免 deInitSDK 时采集未停干净
            if (app->state.cameraOn) {
                rtc.stopLocalPreview();
                app->state.cameraOn = false;
            }
            rtc.shutdown();
            resetToggles(app);
            if (app->hCameraCombo != nullptr) {
                ::SendMessageW(app->hCameraCombo, CB_RESETCONTENT, 0, 0);
            }
            appendLog(app, "destroyEngine");
            break;
        case IDC_BTN_JOIN: {
            const int ret = rtc.joinRoom(getEditUtf8(app->hRoom), getEditUtf8(app->hUid));
            appendLog(app, "joinRoom ret=" + std::to_string(ret));
            break;
        }
        case IDC_BTN_LEAVE: {
            const int ret = rtc.leaveRoom();
            resetToggles(app);
            appendLog(app, "leaveRoom ret=" + std::to_string(ret));
            break;
        }
        case IDC_BTN_ENUM_CAMERA:
            refreshCameraList(app);
            break;
        case IDC_BTN_CAMERA: {
            // 开预览前同步当前下拉选择
            if (!app->state.cameraOn && app->hCameraCombo != nullptr) {
                const int cur =
                    static_cast<int>(::SendMessageW(app->hCameraCombo, CB_GETCURSEL, 0, 0));
                if (cur >= 0) {
                    const int deviceIndex = static_cast<int>(
                        ::SendMessageW(app->hCameraCombo, CB_GETITEMDATA, static_cast<WPARAM>(cur),
                                       0));
                    rtc.setSelectedCameraIndex(deviceIndex);
                }
            }
            app->state.cameraOn = !app->state.cameraOn;
            const int ret =
                app->state.cameraOn ? rtc.startLocalPreview() : rtc.stopLocalPreview();
            if (ret != 0) {
                app->state.cameraOn = false;
            }
            setWindowTextUtf8(app->hBtnCamera, app->state.cameraOn ? "Camera OFF" : "Camera ON");
            break;
        }
        case IDC_BTN_AUDIO:
            toggleSubscribe(app, true);
            break;
        case IDC_BTN_VIDEO:
            toggleSubscribe(app, false);
            break;
        default:
            break;
    }
}

LRESULT CALLBACK DemoWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    DemoUiApp* app = reinterpret_cast<DemoUiApp*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {
        case WM_NCCREATE: {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
            app = static_cast<DemoUiApp*>(cs->lpCreateParams);
            ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
            app->hwnd = hwnd;
            return TRUE;
        }
        case WM_CREATE: {
            INITCOMMONCONTROLSEX icc{};
            icc.dwSize = sizeof(icc);
            icc.dwICC = ICC_STANDARD_CLASSES;
            ::InitCommonControlsEx(&icc);

            app->font = ::CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                      DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                      CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                                      L"Microsoft YaHei UI");
            if (app->font != nullptr) {
                app->fontOwned = true;
            } else {
                app->font = static_cast<HFONT>(::GetStockObject(DEFAULT_GUI_FONT));
                app->fontOwned = false;
            }
            app->brBg = ::CreateSolidBrush(RGB(32, 32, 36));
            app->brEdit = ::CreateSolidBrush(RGB(45, 45, 50));
            createChildren(app);
            RECT rc{};
            ::GetClientRect(hwnd, &rc);
            layoutControls(app, rc.right - rc.left, rc.bottom - rc.top);
            ::SetTimer(hwnd, kTimerId, 50, nullptr);

#if defined(XPROBE_USE_THUNDER)
            RtcManager::getInstance().setVideoHwnds(app->hLocalVideo, app->hRemoteVideo);
#endif
            return 0;
        }
        case WM_SIZE: {
            layoutControls(app, LOWORD(lParam), HIWORD(lParam));
            return 0;
        }
        case WM_TIMER:
            if (wParam == kTimerId && app != nullptr && app->poster != nullptr) {
                app->poster->drain();
                flushLogToEdit(app);
            }
            return 0;
        case kMsgDrain:
            if (app != nullptr && app->poster != nullptr) {
                app->poster->drain();
                flushLogToEdit(app);
            }
            return 0;
        case WM_COMMAND:
            if (app != nullptr) {
                onCommand(app, LOWORD(wParam), HIWORD(wParam));
            }
            return 0;
        case WM_CTLCOLORSTATIC: {
            HDC hdc = reinterpret_cast<HDC>(wParam);
            if (app != nullptr && app->brBg != nullptr) {
                ::SetTextColor(hdc, RGB(220, 220, 220));
                ::SetBkColor(hdc, RGB(32, 32, 36));
                return reinterpret_cast<LRESULT>(app->brBg);
            }
            break;
        }
        case WM_CTLCOLOREDIT: {
            if (app != nullptr && app->brEdit != nullptr) {
                HDC hdc = reinterpret_cast<HDC>(wParam);
                ::SetTextColor(hdc, RGB(230, 230, 230));
                ::SetBkColor(hdc, RGB(45, 45, 50));
                return reinterpret_cast<LRESULT>(app->brEdit);
            }
            break;
        }
        case WM_ERASEBKGND: {
            if (app != nullptr && app->brBg != nullptr) {
                RECT rc{};
                ::GetClientRect(hwnd, &rc);
                ::FillRect(reinterpret_cast<HDC>(wParam), &rc, app->brBg);
                return 1;
            }
            break;
        }
        case WM_CLOSE: {
            // 先停预览并立刻关窗，避免 destroyEngine（网络/监控线程收尾）卡住 UI
            if (app != nullptr) {
                if (app->mgr != nullptr) {
                    app->mgr->setMainThreadPoster(nullptr);
                }
                RtcManager& rtc = RtcManager::getInstance();
                if (app->state.cameraOn || rtc.isPreviewOn()) {
                    rtc.stopLocalPreview();
                    app->state.cameraOn = false;
                }
                rtc.setUiLogger(nullptr);
                rtc.setAutoTestMgr(nullptr);
#if defined(XPROBE_USE_THUNDER)
                rtc.setVideoHwnds(nullptr, nullptr);
#endif
            }
            ::DestroyWindow(hwnd);
            return 0;
        }
        case WM_DESTROY:
            ::KillTimer(hwnd, kTimerId);
            if (app != nullptr) {
                if (app->fontOwned && app->font != nullptr) {
                    ::DeleteObject(app->font);
                    app->font = nullptr;
                    app->fontOwned = false;
                }
                if (app->brBg) {
                    ::DeleteObject(app->brBg);
                    app->brBg = nullptr;
                }
                if (app->brEdit) {
                    ::DeleteObject(app->brEdit);
                    app->brEdit = nullptr;
                }
            }
            ::PostQuitMessage(0);
            return 0;
        default:
            break;
    }
    return ::DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace

bool runDemoUi(xprobe::AutoTestMgr* mgr) {
    DemoUiApp app;
    app.mgr = mgr;
    UiMainThreadPoster poster(nullptr);
    app.poster = &poster;

    if (mgr != nullptr) {
        mgr->setMainThreadPoster(&poster);
    }

    RtcManager& rtc = RtcManager::getInstance();
    rtc.setUiLogger([&app](const std::string& msg) { appendLog(&app, msg); });

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = DemoWndProc;
    wc.hInstance = ::GetModuleHandleW(nullptr);
    wc.hCursor = ::LoadCursorW(nullptr, reinterpret_cast<LPCWSTR>(IDC_ARROW));
    wc.hbrBackground = nullptr;
    wc.lpszClassName = L"XprobePcDemoWnd";
    wc.hIcon = ::LoadIconW(nullptr, reinterpret_cast<LPCWSTR>(IDI_APPLICATION));
    wc.hIconSm = wc.hIcon;

    const ATOM atom = ::RegisterClassExW(&wc);
    if (atom == 0 && ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        if (mgr != nullptr) {
            mgr->setMainThreadPoster(nullptr);
        }
        return false;
    }

    HWND hwnd = ::CreateWindowExW(
        0, L"XprobePcDemoWnd", L"Xprobe PC Demo",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, 980, 760, nullptr, nullptr, wc.hInstance, &app);
    if (hwnd == nullptr) {
        if (mgr != nullptr) {
            mgr->setMainThreadPoster(nullptr);
        }
        return false;
    }
    poster.setHwnd(hwnd);
    appendLog(&app, "demo 已启动，RPC 服务监听 0.0.0.0:9000");
    flushLogToEdit(&app);

    ::ShowWindow(hwnd, SW_SHOW);
    ::UpdateWindow(hwnd);

    MSG msg{};
    while (::GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!::IsDialogMessageW(hwnd, &msg)) {
            ::TranslateMessage(&msg);
            ::DispatchMessageW(&msg);
        }
    }

    if (mgr != nullptr) {
        mgr->setMainThreadPoster(nullptr);
    }
    rtc.setUiLogger(nullptr);
    rtc.shutdown();
#if defined(XPROBE_USE_THUNDER)
    rtc.setVideoHwnds(nullptr, nullptr);
#endif
    return true;
}
