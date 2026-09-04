#pragma once

#include <string>

#include "xprobe/AutoTestMgr.h"

// ver=2 RPC 自定义调用：与 Android DemoInvocation 命令表对齐。
class DemoInvocation : public xprobe::ICustomInvocation {
public:
    explicit DemoInvocation(xprobe::AutoTestMgr* mgr);

    void callMethod(const std::string& apiName, const xprobe::JsonValue& params) override;

private:
    void dispatch(const std::string& api, const xprobe::JsonValue& params);
    void handleLegacyPassthrough(const std::string& apiName);

    xprobe::AutoTestMgr* mgr_;
};
