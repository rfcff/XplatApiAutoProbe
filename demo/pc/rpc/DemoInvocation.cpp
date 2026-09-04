#include "rpc/DemoInvocation.h"

#include <chrono>
#include <cstdint>
#include <thread>

#include "rtc/RtcManager.h"

namespace {

std::string strParam(const xprobe::JsonValue& params, const char* key, const std::string& def) {
    const xprobe::JsonValue* v = params.find(key);
    if (v == nullptr || v->isNull()) {
        return def;
    }
    if (v->isString()) {
        return v->asString();
    }
    return v->serialize();
}

int64_t lngParam(const xprobe::JsonValue& params, const char* key, int64_t def) {
    const xprobe::JsonValue* v = params.find(key);
    if (v == nullptr || v->isNull()) {
        return def;
    }
    if (v->isNumber()) {
        return v->asInt(def);
    }
    return def;
}

} // namespace

DemoInvocation::DemoInvocation(xprobe::AutoTestMgr* mgr) : mgr_(mgr) {}

void DemoInvocation::callMethod(const std::string& apiName, const xprobe::JsonValue& params) {
    try {
        if (!apiName.empty() && apiName.front() == '{') {
            handleLegacyPassthrough(apiName);
            return;
        }
        dispatch(apiName, params);
    } catch (const std::exception& e) {
        mgr_->sendError(apiName, e.what());
    } catch (...) {
        mgr_->sendError(apiName, "unknown exception");
    }
}

void DemoInvocation::handleLegacyPassthrough(const std::string& apiName) {
    // C++ Core 无反射：ver!=2 / field 命令仍透传到 CustomInvocation，供协议回归。
    mgr_->sendReturn("legacyApi", apiName);
}

void DemoInvocation::dispatch(const std::string& api, const xprobe::JsonValue& params) {
    RtcManager& rtc = RtcManager::getInstance();

    if (api == "createEngine") {
        std::string appId = strParam(params, "appId", "10034");
        long sceneId = static_cast<long>(lngParam(params, "sceneId", 0));
        long cost = rtc.initialize(appId, sceneId);
        mgr_->sendReturn(api, std::to_string(cost));
    } else if (api == "destroyEngine") {
        rtc.deInitialize();
        mgr_->sendReturn(api, "0");
    } else if (api == "joinRoom") {
        std::string room = strParam(params, "roomName", "82552971");
        std::string uid = strParam(params, "uid", "123456789");
        std::string token = strParam(params, "token", "");
        int ret = rtc.joinRoom(room, uid, token);
        mgr_->sendReturn(api, std::to_string(ret));
    } else if (api == "leaveRoom") {
        int ret = rtc.leaveRoom();
        mgr_->sendReturn(api, std::to_string(ret));
    } else if (api == "addSubscribe") {
        std::string room = strParam(params, "roomName", rtc.getRoomName());
        std::string uid = strParam(params, "uid", rtc.getRemoteUid());
        int ret = rtc.addSubscribe(room, uid);
        mgr_->sendReturn(api, std::to_string(ret));
    } else if (api == "removeSubscribe") {
        std::string room = strParam(params, "roomName", rtc.getRoomName());
        std::string uid = strParam(params, "uid", rtc.getRemoteUid());
        int ret = rtc.removeSubscribe(room, uid);
        mgr_->sendReturn(api, std::to_string(ret));
    } else if (api == "startLocalPreview") {
        int ret = rtc.startLocalPreview();
        mgr_->sendReturn(api, std::to_string(ret));
    } else if (api == "stopLocalPreview") {
        int ret = rtc.stopLocalPreview();
        mgr_->sendReturn(api, std::to_string(ret));
    } else if (api == "setupRemoteVideo") {
        std::string uid = strParam(params, "uid", rtc.getRemoteUid());
        int ret = rtc.setupRemoteVideo(uid);
        mgr_->sendReturn(api, std::to_string(ret));
    } else if (api == "getState") {
        std::string state = "init=" + std::string(rtc.isInitialized() ? "true" : "false")
            + ", room=" + rtc.getRoomName()
            + ", uid=" + rtc.getUid()
            + ", remoteUid=" + rtc.getRemoteUid();
        mgr_->sendReturn(api, state);
    } else if (api == "echo") {
        mgr_->sendReturn(api, "echo api=" + api + " params=" + params.serialize());
    } else if (api == "scheduleCallback") {
        std::string name = strParam(params, "name", "onProbeCallback");
        std::string info = strParam(params, "info", "ok");
        long delayMs = lngParam(params, "delayMs", 200);
        mgr_->sendReturn(api, "0");
        xprobe::AutoTestMgr* mgr = mgr_;
        std::thread([mgr, name, info, delayMs]() {
            if (delayMs > 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
            }
            mgr->sendCallback(name, info);
        }).detach();
    } else {
        mgr_->sendError(api, "unknown api: " + api);
    }
}
