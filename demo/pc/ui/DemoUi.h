#pragma once

namespace xprobe {
class AutoTestMgr;
}

// Win32 主界面：布局对齐 Android MainActivity；返回 false 表示启动失败或用户关闭窗口。
// 仅 Windows 可用（XPROBE_PC_UI=ON 时由 CMake 保证）。
bool runDemoUi(xprobe::AutoTestMgr* mgr);
