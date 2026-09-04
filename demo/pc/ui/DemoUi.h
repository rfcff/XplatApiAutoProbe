#pragma once

namespace xprobe {
class AutoTestMgr;
}

// Dear ImGui 主界面：布局对齐 Android MainActivity；返回 false 表示用户关闭窗口。
bool runDemoUi(xprobe::AutoTestMgr* mgr);
