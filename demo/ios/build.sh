#!/usr/bin/env bash
# =====================================================================
# XplatApiAutoProbe iOS 测试工程构建脚本
#
# 默认构建 macOS 宿主（AppKit UI + RPC 探针，模拟 RTC，可直接运行）：
#     ./build.sh
#     ./build/xprobe_ios_demo              # GUI 模式
#     ./build/xprobe_ios_demo --headless   # CI / 自动化测试（无窗口）
#
# 可选：链接真实 Thunder SDK（需本机已有 Mac Thunder 头文件 + 库）：
#     XPROBE_USE_THUNDER=1 ./build.sh
#     # 或显式指定路径：
#     XPROBE_USE_THUNDER=1 \
#       XPROBE_THUNDER_INCLUDE=/path/to/thunder/mac/src/include \
#       XPROBE_THUNDER_LDFLAGS="-F/path/to/Frameworks -framework Thunder" \
#       ./build.sh
#
# 指定 SDK 可交叉编译，仅做编译校验（iOS 真机 / 模拟器，headless-only）：
#     ./build.sh iphoneos
#     ./build.sh iphonesimulator
# =====================================================================
set -euo pipefail

cd "$(dirname "$0")"

SDK="${1:-macosx}"
IOS_DIR="../../ios"
OUT="build/xprobe_ios_demo"
USE_THUNDER="${XPROBE_USE_THUNDER:-0}"

DEMO_SOURCES=(
    main.mm
    AppController.mm
    MainView.mm
    rpc/DemoInvocation.mm
    rtc/RtcManager.mm
    probe/DemoCalc.m
    probe/DemoState.m
    probe/DemoInstMgr.m
)

XPROBE_SOURCES=(
    "$IOS_DIR"/xprobe/*.mm
    "$IOS_DIR"/xprobe/connect/*.mm
)

EXTRA_FLAGS=()
INCLUDE_FLAGS=(-I "$IOS_DIR/xprobe" -I "$IOS_DIR/xprobe/connect")
FRAMEWORKS=(-framework Foundation -framework CoreGraphics -lc++)
LDFLAGS=()

if [ "$SDK" = "macosx" ]; then
    CC_BIN="clang"
    FRAMEWORKS+=(-framework AppKit)
else
    CC_BIN="xcrun -sdk $SDK clang"
    EXTRA_FLAGS+=(-DXPROBE_HEADLESS_ONLY)
    if [ "$USE_THUNDER" = "1" ]; then
        echo "[build] error: XPROBE_USE_THUNDER=1 仅支持 macosx（Mac Thunder 头文件使用 NSView/AppKit）" >&2
        exit 1
    fi
fi

if [ "$USE_THUNDER" = "1" ]; then
    REPO_ROOT="$(cd ../.. && pwd)"
    YYINC_ROOT="$(cd "$REPO_ROOT/.." && pwd)"
    DEFAULT_INCLUDE="$YYINC_ROOT/thunder/thunder/mac/src/include"
    THUNDER_INCLUDE="${XPROBE_THUNDER_INCLUDE:-$DEFAULT_INCLUDE}"
    if [ ! -f "$THUNDER_INCLUDE/ThunderEngine.h" ]; then
        echo "[build] error: 找不到 ThunderEngine.h（XPROBE_THUNDER_INCLUDE=$THUNDER_INCLUDE）" >&2
        echo "        请设置 XPROBE_THUNDER_INCLUDE 指向 thunder/mac/src/include" >&2
        exit 1
    fi
    if [ -z "${XPROBE_THUNDER_LDFLAGS:-}" ]; then
        echo "[build] error: 真实 Thunder 需要链接库。请设置 XPROBE_THUNDER_LDFLAGS，例如：" >&2
        echo "        XPROBE_THUNDER_LDFLAGS='-F/path/to/Frameworks -framework Thunder'" >&2
        echo "        或 '-L/path/to/lib -lThunder'" >&2
        exit 1
    fi
    EXTRA_FLAGS+=(-DXPROBE_USE_THUNDER)
    INCLUDE_FLAGS+=(-I "$THUNDER_INCLUDE")
    # shellcheck disable=SC2206
    LDFLAGS+=(${XPROBE_THUNDER_LDFLAGS})
    echo "[build] Thunder ON  include=$THUNDER_INCLUDE"
else
    echo "[build] Thunder OFF (simulator RtcManager)"
fi

mkdir -p build

echo "[build] SDK=$SDK -> $OUT"
# shellcheck disable=SC2086
$CC_BIN -fobjc-arc \
    ${EXTRA_FLAGS[@]+"${EXTRA_FLAGS[@]}"} \
    "${INCLUDE_FLAGS[@]}" \
    "${FRAMEWORKS[@]}" \
    ${LDFLAGS[@]+"${LDFLAGS[@]}"} \
    "${DEMO_SOURCES[@]}" \
    "${XPROBE_SOURCES[@]}" \
    -o "$OUT"

echo "[build] done: $OUT"
